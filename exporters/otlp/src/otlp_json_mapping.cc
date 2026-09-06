// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#if defined(ENABLE_OTLP_UTF8_VALIDITY)
#  include <utf8_validity.h>
#endif

#include "opentelemetry/exporters/otlp/otlp_json_mapping.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace json_mapping
{

namespace
{

char HexDigit(std::uint8_t nibble) noexcept
{
  return nibble >= 10 ? static_cast<char>(nibble - 10 + 'a') : static_cast<char>(nibble + '0');
}

// Per the OpenTelemetry spec a uint64 above INT64_MAX is a decimal string
// rather than a value wrapped into a negative int64, and below that it takes
// the same int_value encoding as any other integer.
// https://opentelemetry.io/docs/specs/otel/common/attribute-type-mapping/#integer-values
void WriteUInt64AnyValue(JsonWriter &writer, std::uint64_t value) noexcept
{
  writer.BeginObject();
  if (value <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
  {
    writer.Key("intValue");
    WriteUInt64String(writer, value);
  }
  else
  {
    writer.Key("stringValue");
    writer.WriteString(std::to_string(value));
  }
  writer.EndObject();
}

// Emits the string alternative of an AnyValue. Where the build checks UTF-8,
// input that is not valid falls back to bytesValue.
void WriteStringAnyValue(JsonWriter &writer, const std::string &value) noexcept
{
  writer.BeginObject();
#if defined(ENABLE_OTLP_UTF8_VALIDITY)
  if (!utf8_range::IsStructurallyValid({value.data(), value.size()}))
  {
    // A JSON string cannot carry bytes that are not valid UTF-8.
    writer.Key("bytesValue");
    writer.WriteBytes(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
    writer.EndObject();
    return;
  }
#endif
  writer.Key("stringValue");
  writer.WriteString(value);
  writer.EndObject();
}

// Writes an ArrayValue-wrapped AnyValue: {"arrayValue":{"values":[...]}}.
// `write_element` emits one complete AnyValue per element.
template <typename Container, typename WriteElement>
void WriteArrayAnyValue(JsonWriter &writer,
                        const Container &values,
                        WriteElement write_element) noexcept
{
  // An array holding nothing sets no field on the ArrayValue, and a message
  // with no field set is null. The alternative is still chosen, so the
  // arrayValue key is there either way.
  if (values.empty())
  {
    writer.BeginObject();
    writer.Key("arrayValue");
    writer.WriteNull();
    writer.EndObject();
    return;
  }

  writer.BeginObject();
  writer.Key("arrayValue");
  writer.BeginObject();
  writer.Key("values");
  writer.BeginArray();
  for (const auto &value : values)
  {
    write_element(value);
  }
  writer.EndArray();
  writer.EndObject();
  writer.EndObject();
}

// Writes a scalar AnyValue whose alternative needs no encoding decision.
template <typename WriteScalar>
void WriteScalarAnyValue(JsonWriter &writer, nostd::string_view key, WriteScalar write) noexcept
{
  writer.BeginObject();
  writer.Key(key);
  write();
  writer.EndObject();
}

struct OwnedAttributeValueVisitor
{
  JsonWriter &writer;

  void operator()(bool value) const noexcept
  {
    WriteScalarAnyValue(writer, "boolValue", [&] { writer.WriteBool(value); });
  }

  void operator()(std::int32_t value) const noexcept
  {
    WriteScalarAnyValue(writer, "intValue",
                        [&] { writer.WriteString(std::to_string(value)); });
  }

  void operator()(std::uint32_t value) const noexcept
  {
    WriteScalarAnyValue(writer, "intValue",
                        [&] { writer.WriteString(std::to_string(value)); });
  }

  void operator()(std::int64_t value) const noexcept
  {
    WriteScalarAnyValue(writer, "intValue",
                        [&] { writer.WriteString(std::to_string(value)); });
  }

  void operator()(std::uint64_t value) const noexcept { WriteUInt64AnyValue(writer, value); }

  void operator()(double value) const noexcept
  {
    WriteScalarAnyValue(writer, "doubleValue", [&] { writer.WriteDouble(value); });
  }

  void operator()(const std::string &value) const noexcept
  {
    WriteStringAnyValue(writer, value);
  }

  void operator()(const std::vector<bool> &values) const noexcept
  {
    WriteArrayAnyValue(writer, values, [&](bool value) {
      WriteScalarAnyValue(writer, "boolValue", [&] { writer.WriteBool(value); });
    });
  }

  void operator()(const std::vector<std::int32_t> &values) const noexcept
  {
    WriteArrayAnyValue(writer, values, [&](std::int32_t value) {
      WriteScalarAnyValue(writer, "intValue", [&] { writer.WriteString(std::to_string(value)); });
    });
  }

  void operator()(const std::vector<std::uint32_t> &values) const noexcept
  {
    WriteArrayAnyValue(writer, values, [&](std::uint32_t value) {
      WriteScalarAnyValue(writer, "intValue", [&] { writer.WriteString(std::to_string(value)); });
    });
  }

  void operator()(const std::vector<std::int64_t> &values) const noexcept
  {
    WriteArrayAnyValue(writer, values, [&](std::int64_t value) {
      WriteScalarAnyValue(writer, "intValue", [&] { writer.WriteString(std::to_string(value)); });
    });
  }

  void operator()(const std::vector<std::uint64_t> &values) const noexcept
  {
    WriteArrayAnyValue(writer, values,
                       [&](std::uint64_t value) { WriteUInt64AnyValue(writer, value); });
  }

  void operator()(const std::vector<double> &values) const noexcept
  {
    WriteArrayAnyValue(writer, values, [&](double value) {
      WriteScalarAnyValue(writer, "doubleValue", [&] { writer.WriteDouble(value); });
    });
  }

  void operator()(const std::vector<std::string> &values) const noexcept
  {
    WriteArrayAnyValue(writer, values,
                       [&](const std::string &value) { WriteStringAnyValue(writer, value); });
  }

  void operator()(const std::vector<std::uint8_t> &values) const noexcept
  {
    // A byte array is one bytesValue, not an array of them.
    WriteScalarAnyValue(writer, "bytesValue",
                        [&] { writer.WriteBytes(values.data(), values.size()); });
  }
};

}  // namespace

void WriteHexId(JsonWriter &writer, const std::uint8_t *data, std::size_t size) noexcept
{
  std::string hex;
  hex.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i)
  {
    hex.push_back(HexDigit(static_cast<std::uint8_t>(data[i] >> 4)));
    hex.push_back(HexDigit(static_cast<std::uint8_t>(data[i] & 0x0f)));
  }
  writer.WriteString(hex);
}

void WriteUInt64String(JsonWriter &writer, std::uint64_t value) noexcept
{
  writer.WriteString(std::to_string(value));
}

void WriteAnyValue(JsonWriter &writer,
                   const opentelemetry::sdk::common::OwnedAttributeValue &value) noexcept
{
  opentelemetry::sdk::common::VisitVariant(OwnedAttributeValueVisitor{writer}, value);
}

void WriteKeyValue(JsonWriter &writer,
                   nostd::string_view key,
                   const opentelemetry::sdk::common::OwnedAttributeValue &value) noexcept
{
  writer.BeginObject();
  writer.Key("key");
  writer.WriteString(key);
  writer.Key("value");
  WriteAnyValue(writer, value);
  writer.EndObject();
}

void WriteResource(JsonWriter &writer,
                   const opentelemetry::sdk::resource::Resource &resource) noexcept
{
  const auto &attributes = resource.GetAttributes();
  if (attributes.empty())
  {
    writer.WriteNull();
    return;
  }

  writer.BeginObject();
  WriteAttributes(writer, attributes);
  writer.EndObject();
}

void WriteInstrumentationScope(
    JsonWriter &writer,
    const opentelemetry::sdk::instrumentationscope::InstrumentationScope &scope) noexcept
{
  const std::string &name    = scope.GetName();
  const std::string &version = scope.GetVersion();
  const auto &attributes     = scope.GetAttributes();

  if (name.empty() && version.empty() && attributes.empty())
  {
    writer.WriteNull();
    return;
  }

  writer.BeginObject();
  if (!name.empty())
  {
    writer.Key("name");
    writer.WriteString(name);
  }
  if (!version.empty())
  {
    writer.Key("version");
    writer.WriteString(version);
  }
  WriteAttributes(writer, attributes);
  writer.EndObject();
}

}  // namespace json_mapping
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
