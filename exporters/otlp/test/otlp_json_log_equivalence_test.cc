// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Proves the protobuf-free OTLP/JSON log mapping and the reflection-based
// protobuf one encode the same records to the same bytes.
//
// Both paths are driven through the sdk::logs::Recordable interface with an
// identical call sequence, which is what the SDK itself does, so anything the
// two disagree on is a difference in mapping rather than in input. The
// assertion is on the full body, because a mapping that drifts in one field is
// exactly what this is here to catch.

#include "opentelemetry/exporters/otlp/otlp_json_log_mapping.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_converter.h"
#include "opentelemetry/exporters/otlp/otlp_json_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/exporters/otlp/otlp_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/log_record_limits.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h" // IWYU pragma: keep
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h" // IWYU pragma: keep
// clang-format on

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace
{

namespace logs_api = opentelemetry::logs;
namespace sdk_logs = opentelemetry::sdk::logs;
namespace trace_api = opentelemetry::trace;

constexpr std::uint8_t kTraceIdBytes[trace_api::TraceId::kSize] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
constexpr std::uint8_t kSpanIdBytes[trace_api::SpanId::kSize] = {0x11, 0x12, 0x13, 0x14,
                                                                0x15, 0x16, 0x17, 0x18};

trace_api::TraceId MakeTraceId()
{
  return trace_api::TraceId(
      nostd::span<const std::uint8_t, trace_api::TraceId::kSize>(kTraceIdBytes,
                                                                 trace_api::TraceId::kSize));
}

trace_api::SpanId MakeSpanId()
{
  return trace_api::SpanId(
      nostd::span<const std::uint8_t, trace_api::SpanId::kSize>(kSpanIdBytes,
                                                                trace_api::SpanId::kSize));
}

std::string ProtobufPathJson(const nostd::span<std::unique_ptr<sdk_logs::Recordable>> &recordables)
{
  proto::collector::logs::v1::ExportLogsServiceRequest request;
  OtlpRecordableUtils::PopulateRequest(recordables, &request);

  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  // The defaults the OTLP HTTP and file exporters use for JSON bodies.
  ConvertGenericMessageToJson(*writer, request,
                              JsonConverterOptions{false, JsonBytesMappingKind::kHexId});
  EXPECT_TRUE(writer->ok());
  return writer->ToString();
}

std::string ProtobufFreePathJson(
    const nostd::span<std::unique_ptr<sdk_logs::Recordable>> &recordables)
{
  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  ConvertLogsToJson(*writer, recordables);
  EXPECT_TRUE(writer->ok());
  return writer->ToString();
}

// Records the same script into both recordable types and returns what each
// path encodes, so a test only has to compare the two.
struct BothEncodings
{
  std::string protobuf;
  std::string protobuf_free;
};

template <typename Record>
BothEncodings EncodeBothWays(std::size_t record_count, Record &&record)
{
  std::vector<std::unique_ptr<sdk_logs::Recordable>> protobuf_recordables;
  std::vector<std::unique_ptr<sdk_logs::Recordable>> protobuf_free_recordables;
  for (std::size_t i = 0; i < record_count; ++i)
  {
    protobuf_recordables.emplace_back(new OtlpLogRecordable());
    protobuf_free_recordables.emplace_back(new OtlpJsonLogRecordable());
    record(i, *protobuf_recordables.back());
    record(i, *protobuf_free_recordables.back());
  }

  return BothEncodings{
      ProtobufPathJson(nostd::span<std::unique_ptr<sdk_logs::Recordable>>(
          protobuf_recordables.data(), protobuf_recordables.size())),
      ProtobufFreePathJson(nostd::span<std::unique_ptr<sdk_logs::Recordable>>(
          protobuf_free_recordables.data(), protobuf_free_recordables.size()))};
}

TEST(OtlpJsonLogEquivalence, EmptyBatch)
{
  auto encodings = EncodeBothWays(0, [](std::size_t, sdk_logs::Recordable &) {});
  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

// A record nothing was ever put into is a message with no field set, and so
// encodes as null rather than as an empty object. It still gets a resource and
// a scope, which is where a log record differs from a span: a recordable given
// neither reports the SDK's defaults, which carry the telemetry.sdk attributes
// and the scope name, so neither key can be absent or null here.
TEST(OtlpJsonLogEquivalence, UntouchedRecordIsNullUnderDefaultResourceAndScope)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &) {});
  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_NE(encodings.protobuf_free.find("\"logRecords\":[null]"), std::string::npos)
      << encodings.protobuf_free;
  EXPECT_NE(encodings.protobuf_free.find("\"telemetry.sdk.language\""), std::string::npos)
      << encodings.protobuf_free;
  EXPECT_NE(encodings.protobuf_free.find("\"scope\":{"), std::string::npos)
      << encodings.protobuf_free;
}

TEST(OtlpJsonLogEquivalence, FullySpecifiedRecord)
{
  static auto resource = opentelemetry::sdk::resource::Resource::Create(
      {{"service.name", "equivalence"}, {"host.id", static_cast<std::int64_t>(7)}},
      "https://example.com/resource-schema");
  static auto scope = opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create(
      "equivalence_scope", "1.2.3", "https://example.com/scope-schema",
      {{"scope.attr", "scope value"}});

  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetResource(resource);
    recordable.SetInstrumentationScope(*scope);
    recordable.SetTimestamp(
        opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1000000000)));
    recordable.SetObservedTimestamp(
        opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1000000500)));
    recordable.SetSeverity(logs_api::Severity::kError2);
    recordable.SetBody("a log body");
    recordable.SetEventId(42, "an.event.name");
    recordable.SetTraceId(MakeTraceId());
    recordable.SetSpanId(MakeSpanId());
    recordable.SetTraceFlags(trace_api::TraceFlags(trace_api::TraceFlags::kIsSampled));

    recordable.SetAttribute("bool_attr", true);
    recordable.SetAttribute("int32_attr", static_cast<std::int32_t>(-7));
    recordable.SetAttribute("uint32_attr", static_cast<std::uint32_t>(7));
    recordable.SetAttribute("int64_attr", static_cast<std::int64_t>(-42));
    recordable.SetAttribute("uint64_attr", static_cast<std::uint64_t>(42));
    // Above INT64_MAX, where OTLP switches from a number to a decimal string.
    recordable.SetAttribute("big_uint64_attr", static_cast<std::uint64_t>(18446744073709551615ULL));
    recordable.SetAttribute("double_attr", 3.5);
    recordable.SetAttribute("string_attr", "a string value");

    const bool bool_array[]                 = {true, false};
    const std::int64_t int64_array[]        = {-1, 0, 1};
    const double double_array[]             = {1.5, 2.5};
    const nostd::string_view string_array[] = {"one", "two"};
    const std::uint8_t byte_array[]         = {0xde, 0xad, 0xbe, 0xef};
    recordable.SetAttribute("bool_array", nostd::span<const bool>(bool_array, 2));
    recordable.SetAttribute("int64_array", nostd::span<const std::int64_t>(int64_array, 3));
    recordable.SetAttribute("double_array", nostd::span<const double>(double_array, 2));
    recordable.SetAttribute("string_array", nostd::span<const nostd::string_view>(string_array, 2));
    recordable.SetAttribute("byte_array", nostd::span<const std::uint8_t>(byte_array, 4));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

// Every severity the enumeration has, plus two it does not: the protobuf path
// maps anything unrecognised to the text INVALID and the unspecified number,
// so severityText is on the wire while severityNumber, being zero, is not.
TEST(OtlpJsonLogEquivalence, SeverityVariants)
{
  std::vector<logs_api::Severity> severities;
  for (std::uint8_t value = 0; value <= 24; ++value)
  {
    severities.push_back(static_cast<logs_api::Severity>(value));
  }
  severities.push_back(static_cast<logs_api::Severity>(200));

  for (const auto severity : severities)
  {
    auto encodings = EncodeBothWays(1, [severity](std::size_t, sdk_logs::Recordable &recordable) {
      recordable.SetSeverity(severity);
    });
    EXPECT_EQ(encodings.protobuf, encodings.protobuf_free)
        << "severity " << static_cast<int>(severity);
  }

  auto invalid = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetSeverity(logs_api::Severity::kInvalid);
  });
  EXPECT_NE(invalid.protobuf_free.find("\"severityText\":\"INVALID\""), std::string::npos)
      << invalid.protobuf_free;
  EXPECT_EQ(invalid.protobuf_free.find("\"severityNumber\""), std::string::npos)
      << invalid.protobuf_free;
}

// Every alternative a body can hold. The body takes no length limit and, once
// set, is on the wire even when the value it holds encodes as null.
TEST(OtlpJsonLogEquivalence, BodyVariants)
{
  const bool bool_array[]                 = {true, false};
  const std::int64_t int64_array[]        = {-1, 0, 1};
  const double double_array[]             = {1.5, 2.5};
  const nostd::string_view string_array[] = {"one", "two"};
  const std::uint8_t byte_array[]         = {0xde, 0xad, 0xbe, 0xef};

  const std::vector<opentelemetry::common::AttributeValue> bodies = {
      opentelemetry::common::AttributeValue(true),
      opentelemetry::common::AttributeValue(static_cast<std::int32_t>(-7)),
      opentelemetry::common::AttributeValue(static_cast<std::uint32_t>(7)),
      opentelemetry::common::AttributeValue(static_cast<std::int64_t>(-42)),
      opentelemetry::common::AttributeValue(static_cast<std::uint64_t>(42)),
      opentelemetry::common::AttributeValue(static_cast<std::uint64_t>(18446744073709551615ULL)),
      opentelemetry::common::AttributeValue(3.5),
      opentelemetry::common::AttributeValue("a body"),
      // An empty string is a set oneof alternative, not an absent body.
      opentelemetry::common::AttributeValue(""),
      opentelemetry::common::AttributeValue(nostd::span<const bool>(bool_array, 2)),
      opentelemetry::common::AttributeValue(nostd::span<const std::int64_t>(int64_array, 3)),
      opentelemetry::common::AttributeValue(nostd::span<const double>(double_array, 2)),
      opentelemetry::common::AttributeValue(
          nostd::span<const nostd::string_view>(string_array, 2)),
      opentelemetry::common::AttributeValue(nostd::span<const std::uint8_t>(byte_array, 4)),
  };

  for (std::size_t i = 0; i < bodies.size(); ++i)
  {
    const auto &body = bodies[i];
    auto encodings   = EncodeBothWays(1, [&body](std::size_t, sdk_logs::Recordable &recordable) {
      recordable.SetBody(body);
    });
    EXPECT_EQ(encodings.protobuf, encodings.protobuf_free) << "body alternative " << i;
  }
}

// An array attribute or body holding no elements: the protobuf path creates
// the ArrayValue and never puts a value in it, leaving a message with no field
// set.
TEST(OtlpJsonLogEquivalence, EmptyArrayValues)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetBody(nostd::span<const std::int64_t>(nullptr, static_cast<std::size_t>(0)));
    recordable.SetAttribute("empty_bool_array",
                            nostd::span<const bool>(nullptr, static_cast<std::size_t>(0)));
    recordable.SetAttribute("empty_string_array", nostd::span<const nostd::string_view>(
                                                      nullptr, static_cast<std::size_t>(0)));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

// Several records across two resources and two scopes, which is what exercises
// the grouping and the order the groups come out in.
TEST(OtlpJsonLogEquivalence, GroupsByResourceAndScope)
{
  static auto resource_a = opentelemetry::sdk::resource::Resource::Create({{"service.name", "a"}});
  static auto resource_b = opentelemetry::sdk::resource::Resource::Create({{"service.name", "b"}});
  static auto scope_one =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("scope_one", "1");
  static auto scope_two =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("scope_two", "2");

  auto encodings = EncodeBothWays(6, [](std::size_t index, sdk_logs::Recordable &recordable) {
    recordable.SetResource((index % 2 == 0) ? resource_a : resource_b);
    recordable.SetInstrumentationScope((index % 3 == 0) ? *scope_one : *scope_two);
    recordable.SetBody("record " + std::to_string(index));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

// An id that is all zero bytes is invalid, and the protobuf path clears the
// field rather than putting sixteen zeroes on the wire.
TEST(OtlpJsonLogEquivalence, InvalidTraceContextIsAbsent)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetTraceId(trace_api::TraceId());
    recordable.SetSpanId(trace_api::SpanId());
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  // Clearing the only two fields that were touched leaves the record with none
  // set at all, so it encodes as null.
  EXPECT_NE(encodings.protobuf_free.find("\"logRecords\":[null]"), std::string::npos)
      << encodings.protobuf_free;
}

// LogRecord.flags carries the whole trace-flags byte, where Span.flags masks
// it. A value with a bit outside the sampled flag pins the difference.
TEST(OtlpJsonLogEquivalence, TraceFlagsAreUnmasked)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetTraceFlags(trace_api::TraceFlags(static_cast<std::uint8_t>(0x83)));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_NE(encodings.protobuf_free.find("\"flags\":131"), std::string::npos)
      << encodings.protobuf_free;
}

TEST(OtlpJsonLogEquivalence, NanosecondTimestampsSurviveAsStrings)
{
  // Beyond 2^53, where a JSON number would lose the low bits to double
  // precision. The assertion is not only that the two agree but that the value
  // is present in full.
  constexpr std::uint64_t kTimeNanos = 1700000000123456789ULL;

  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetTimestamp(opentelemetry::common::SystemTimestamp(
        std::chrono::nanoseconds(static_cast<std::int64_t>(kTimeNanos))));
    recordable.SetObservedTimestamp(opentelemetry::common::SystemTimestamp(
        std::chrono::nanoseconds(static_cast<std::int64_t>(kTimeNanos + 1))));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_NE(encodings.protobuf_free.find("\"1700000000123456789\""), std::string::npos)
      << encodings.protobuf_free;
  EXPECT_NE(encodings.protobuf_free.find("\"1700000000123456790\""), std::string::npos)
      << encodings.protobuf_free;
}

// A zero timestamp is the field's default and so is absent, which is the
// presence edge a record written before the clock was set lands on.
TEST(OtlpJsonLogEquivalence, ZeroTimestampsAreAbsent)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetTimestamp(opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(0)));
    recordable.SetObservedTimestamp(
        opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(0)));
    recordable.SetBody("no clock yet");
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_EQ(encodings.protobuf_free.find("UnixNano"), std::string::npos)
      << encodings.protobuf_free;
}

TEST(OtlpJsonLogEquivalence, DroppedCountsFromLogRecordLimits)
{
  sdk_logs::LogRecordLimits limits;
  limits.attribute_count_limit = 1;

  auto encodings = EncodeBothWays(1, [limits](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetLogRecordLimits(limits);
    recordable.SetAttribute("kept", "yes");
    recordable.SetAttribute("dropped", "no");
    recordable.SetAttribute("also_dropped", "no");
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_NE(encodings.protobuf_free.find("\"droppedAttributesCount\":2"), std::string::npos)
      << encodings.protobuf_free;
}

// The value-length limit truncates attributes on a UTF-8 boundary, and leaves
// the body alone: the protobuf path populates the body with default converter
// options, so no limit reaches it.
TEST(OtlpJsonLogEquivalence, ValueLengthLimitTruncatesAttributesButNotBody)
{
  sdk_logs::LogRecordLimits limits;
  limits.attribute_value_length_limit = 5;

  auto encodings = EncodeBothWays(1, [limits](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetLogRecordLimits(limits);
    recordable.SetAttribute("ascii", "abcdefghij");
    // Four two-byte characters: a raw cut at five bytes would split the third.
    recordable.SetAttribute("multibyte", "\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3");
    recordable.SetBody("a body that is longer than the limit");
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_NE(encodings.protobuf_free.find("a body that is longer than the limit"),
            std::string::npos)
      << encodings.protobuf_free;
}

// An attribute whose key is empty is not recorded at all, rather than recorded
// under the empty key or counted as dropped.
TEST(OtlpJsonLogEquivalence, EmptyAttributeKeyIsIgnored)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_logs::Recordable &recordable) {
    recordable.SetAttribute("", "ignored");
    recordable.SetBody("kept");
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  // The only attributes in the body belong to the resource; the record carries
  // none, and no dropped count either.
  EXPECT_NE(encodings.protobuf_free.find(
                "\"logRecords\":[{\"body\":{\"stringValue\":\"kept\"}}]"),
            std::string::npos)
      << encodings.protobuf_free;
}

// Several records under one scope, where the array order is what the wire
// carries.
TEST(OtlpJsonLogEquivalence, SeveralRecordsKeepTheirOrder)
{
  auto encodings = EncodeBothWays(3, [](std::size_t index, sdk_logs::Recordable &recordable) {
    recordable.SetSeverity(logs_api::Severity::kInfo);
    recordable.SetBody("record " + std::to_string(index));
    recordable.SetAttribute("index", static_cast<std::int64_t>(index));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

}  // namespace
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
