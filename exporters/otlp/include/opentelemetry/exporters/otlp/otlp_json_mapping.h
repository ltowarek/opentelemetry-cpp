// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * The parts of OTLP/JSON that every signal shares: resources, instrumentation
 * scopes and attributes. Traces, metrics and logs differ only in what they wrap
 * these in, so the mapping lives here once rather than three times.
 *
 * Nothing here references protobuf. The functions emit straight into a
 * JsonWriter, and they reproduce what the reflection-based converter produces
 * for the equivalent protobuf message, including the encodings OTLP/JSON
 * deviates from proto3 JSON on:
 *
 * - 64-bit integers are decimal strings, because as JSON numbers they exceed
 *   double precision and are silently truncated.
 * - Enums are integers rather than names.
 * - Keys are lowerCamelCase.
 *
 * Trace and span IDs are hex rather than base64, but that is a per-field
 * decision the caller makes by choosing WriteHexId over the writer's
 * WriteBytes, because only the caller knows which field it is emitting.
 *
 * Proto3 omission is part of the mapping: a field holding its default value is
 * not emitted at all, and a message with no fields set is emitted as null
 * rather than as an empty object. Callers therefore must not emit a key before
 * calling these, except where the function name says it writes the key itself.
 */
namespace json_mapping
{

/** Writes `size` bytes of `data` as a lowercase hex string. */
void WriteHexId(JsonWriter &writer, const std::uint8_t *data, std::size_t size) noexcept;

/** Writes a 64-bit value as a decimal string, as OTLP/JSON requires. */
void WriteUInt64String(JsonWriter &writer, std::uint64_t value) noexcept;

/**
 * Writes an AnyValue object for `value`.
 *
 * Values arrive already truncated to whatever attribute_value_length_limit
 * applied, because both paths convert -- and so truncate -- when the value is
 * recorded rather than when it is written.
 */
void WriteAnyValue(JsonWriter &writer,
                   const opentelemetry::sdk::common::OwnedAttributeValue &value) noexcept;

/** Writes one KeyValue object: the key, and the AnyValue it maps to. */
void WriteKeyValue(JsonWriter &writer,
                   nostd::string_view key,
                   const opentelemetry::sdk::common::OwnedAttributeValue &value) noexcept;

/**
 * Writes the `attributes` array, and nothing at all when there are none: a
 * repeated field holding no elements is absent rather than empty.
 *
 * A template because the attributes of a resource or a scope arrive in the
 * SDK's map while a span's arrive in the recording order OTLP puts on the
 * wire; both yield key/value pairs, which is all this needs.
 */
template <typename AttributeContainer>
void WriteAttributes(JsonWriter &writer, const AttributeContainer &attributes) noexcept
{
  if (attributes.empty())
  {
    return;
  }

  writer.Key("attributes");
  writer.BeginArray();
  for (const auto &attribute : attributes)
  {
    WriteKeyValue(writer, attribute.first, attribute.second);
  }
  writer.EndArray();
}

/**
 * Writes the Resource message for `resource`, or null when it has no
 * attributes and so no fields are set.
 */
void WriteResource(JsonWriter &writer,
                   const opentelemetry::sdk::resource::Resource &resource) noexcept;

/**
 * Writes the InstrumentationScope message for `scope`, or null when its name
 * and version are empty and it carries no attributes.
 */
void WriteInstrumentationScope(
    JsonWriter &writer,
    const opentelemetry::sdk::instrumentationscope::InstrumentationScope &scope) noexcept;

}  // namespace json_mapping
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
