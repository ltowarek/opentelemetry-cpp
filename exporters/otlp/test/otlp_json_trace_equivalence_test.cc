// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Proves the protobuf-free OTLP/JSON trace mapping and the reflection-based
// protobuf one encode the same spans to the same bytes.
//
// Both paths are driven through the sdk::trace::Recordable interface with an
// identical call sequence, which is what the SDK itself does, so anything the
// two disagree on is a difference in mapping rather than in input. The
// assertion is on the full body, because a mapping that drifts in one field is
// exactly what this is here to catch.

#include "opentelemetry/exporters/otlp/otlp_json_trace_mapping.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_converter.h"
#include "opentelemetry/exporters/otlp/otlp_json_span_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/trace/trace_state.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h" // IWYU pragma: keep
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h" // IWYU pragma: keep
// clang-format on

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace
{

namespace trace_api = opentelemetry::trace;
namespace sdk_trace = opentelemetry::sdk::trace;

constexpr std::uint8_t kTraceIdBytes[trace_api::TraceId::kSize] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
constexpr std::uint8_t kSpanIdBytes[trace_api::SpanId::kSize]       = {0x11, 0x12, 0x13, 0x14,
                                                                      0x15, 0x16, 0x17, 0x18};
constexpr std::uint8_t kParentSpanIdBytes[trace_api::SpanId::kSize] = {0x21, 0x22, 0x23, 0x24,
                                                                      0x25, 0x26, 0x27, 0x28};
constexpr std::uint8_t kLinkTraceIdBytes[trace_api::TraceId::kSize] = {
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40};
constexpr std::uint8_t kLinkSpanIdBytes[trace_api::SpanId::kSize] = {0x41, 0x42, 0x43, 0x44,
                                                                    0x45, 0x46, 0x47, 0x48};

std::string ProtobufPathJson(
    const nostd::span<std::unique_ptr<sdk_trace::Recordable>> &recordables)
{
  proto::collector::trace::v1::ExportTraceServiceRequest request;
  OtlpRecordableUtils::PopulateRequest(recordables, &request);

  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  // The defaults the OTLP HTTP and file exporters use for JSON bodies.
  ConvertGenericMessageToJson(*writer, request,
                              JsonConverterOptions{false, JsonBytesMappingKind::kHexId});
  EXPECT_TRUE(writer->ok());
  return writer->ToString();
}

std::string ProtobufFreePathJson(
    const nostd::span<std::unique_ptr<sdk_trace::Recordable>> &recordables)
{
  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  ConvertSpansToJson(*writer, recordables);
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
BothEncodings EncodeBothWays(std::size_t span_count, Record &&record)
{
  std::vector<std::unique_ptr<sdk_trace::Recordable>> protobuf_recordables;
  std::vector<std::unique_ptr<sdk_trace::Recordable>> protobuf_free_recordables;
  for (std::size_t i = 0; i < span_count; ++i)
  {
    protobuf_recordables.emplace_back(new OtlpRecordable());
    protobuf_free_recordables.emplace_back(new OtlpJsonSpanRecordable());
    record(i, *protobuf_recordables.back());
    record(i, *protobuf_free_recordables.back());
  }

  return BothEncodings{
      ProtobufPathJson(nostd::span<std::unique_ptr<sdk_trace::Recordable>>(
          protobuf_recordables.data(), protobuf_recordables.size())),
      ProtobufFreePathJson(nostd::span<std::unique_ptr<sdk_trace::Recordable>>(
          protobuf_free_recordables.data(), protobuf_free_recordables.size()))};
}

trace_api::SpanContext MakeSpanContext(const std::uint8_t *trace_id_bytes,
                                       const std::uint8_t *span_id_bytes,
                                       nostd::string_view trace_state_header = "")
{
  auto trace_state = trace_api::TraceState::GetDefault();
  if (!trace_state_header.empty())
  {
    trace_state = trace_api::TraceState::FromHeader(trace_state_header);
  }
  return trace_api::SpanContext(trace_api::TraceId(nostd::span<const std::uint8_t,
                                                               trace_api::TraceId::kSize>(
                                    trace_id_bytes, trace_api::TraceId::kSize)),
                                trace_api::SpanId(nostd::span<const std::uint8_t,
                                                              trace_api::SpanId::kSize>(
                                    span_id_bytes, trace_api::SpanId::kSize)),
                                trace_api::TraceFlags(trace_api::TraceFlags::kIsSampled), false,
                                trace_state);
}

TEST(OtlpJsonTraceEquivalence, EmptyBatch)
{
  auto encodings = EncodeBothWays(0, [](std::size_t, sdk_trace::Recordable &) {});
  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

TEST(OtlpJsonTraceEquivalence, MinimalSpan)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_trace::Recordable &recordable) {
    recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());
  });
  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

TEST(OtlpJsonTraceEquivalence, FullySpecifiedSpan)
{
  static auto resource = opentelemetry::sdk::resource::Resource::Create(
      {{"service.name", "equivalence"}, {"host.id", static_cast<std::int64_t>(7)}},
      "https://example.com/resource-schema");
  static auto scope = opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create(
      "equivalence_scope", "1.2.3", "https://example.com/scope-schema",
      {{"scope.attr", "scope value"}});

  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_trace::Recordable &recordable) {
    recordable.SetResource(resource);
    recordable.SetInstrumentationScope(*scope);
    recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes, "vendor=value"),
                           trace_api::SpanId(nostd::span<const std::uint8_t,
                                                         trace_api::SpanId::kSize>(
                               kParentSpanIdBytes, trace_api::SpanId::kSize)));
    recordable.SetName("equivalence span");
    recordable.SetSpanKind(trace_api::SpanKind::kServer);
    recordable.SetTraceFlags(trace_api::TraceFlags(trace_api::TraceFlags::kIsSampled));
    recordable.SetStartTime(
        opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1000000000)));
    recordable.SetDuration(std::chrono::nanoseconds(500000000));
    recordable.SetStatus(trace_api::StatusCode::kError, "it went wrong");

    recordable.SetAttribute("bool_attr", true);
    recordable.SetAttribute("int32_attr", static_cast<std::int32_t>(-7));
    recordable.SetAttribute("uint32_attr", static_cast<std::uint32_t>(7));
    recordable.SetAttribute("int64_attr", static_cast<std::int64_t>(-42));
    recordable.SetAttribute("uint64_attr", static_cast<std::uint64_t>(42));
    // Above INT64_MAX, where OTLP switches from a number to a decimal string.
    recordable.SetAttribute("big_uint64_attr", static_cast<std::uint64_t>(18446744073709551615ULL));
    recordable.SetAttribute("double_attr", 3.5);
    recordable.SetAttribute("string_attr", "a string value");

    const bool bool_array[]                = {true, false};
    const std::int64_t int64_array[]       = {-1, 0, 1};
    const double double_array[]            = {1.5, 2.5};
    const nostd::string_view string_array[] = {"one", "two"};
    const std::uint8_t byte_array[]        = {0xde, 0xad, 0xbe, 0xef};
    recordable.SetAttribute("bool_array", nostd::span<const bool>(bool_array, 2));
    recordable.SetAttribute("int64_array", nostd::span<const std::int64_t>(int64_array, 3));
    recordable.SetAttribute("double_array", nostd::span<const double>(double_array, 2));
    recordable.SetAttribute("string_array", nostd::span<const nostd::string_view>(string_array, 2));
    recordable.SetAttribute("byte_array", nostd::span<const std::uint8_t>(byte_array, 4));

    std::map<std::string, std::string> event_attributes = {{"event_key", "event_value"}};
    recordable.AddEvent(
        "an event", opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1000500000)),
        opentelemetry::common::MakeAttributes(event_attributes));
    // A second event, so their order on the wire is observable.
    std::map<std::string, std::string> other_event_attributes = {{"other_key", "other_value"}};
    recordable.AddEvent(
        "another event",
        opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1000600000)),
        opentelemetry::common::MakeAttributes(other_event_attributes));

    std::map<std::string, std::string> link_attributes = {{"link_key", "link_value"}};
    recordable.AddLink(MakeSpanContext(kLinkTraceIdBytes, kLinkSpanIdBytes, "link=state"),
                       opentelemetry::common::MakeAttributes(link_attributes));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

TEST(OtlpJsonTraceEquivalence, StatusVariants)
{
  for (const auto code : {trace_api::StatusCode::kUnset, trace_api::StatusCode::kOk,
                          trace_api::StatusCode::kError})
  {
    auto encodings = EncodeBothWays(1, [code](std::size_t, sdk_trace::Recordable &recordable) {
      recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());
      // The description only survives on an error, so this also pins that.
      recordable.SetStatus(code, "a description");
    });
    EXPECT_EQ(encodings.protobuf, encodings.protobuf_free)
        << "status code " << static_cast<int>(code);
  }
}

TEST(OtlpJsonTraceEquivalence, SpanKinds)
{
  for (const auto kind : {trace_api::SpanKind::kInternal, trace_api::SpanKind::kServer,
                          trace_api::SpanKind::kClient, trace_api::SpanKind::kProducer,
                          trace_api::SpanKind::kConsumer})
  {
    auto encodings = EncodeBothWays(1, [kind](std::size_t, sdk_trace::Recordable &recordable) {
      recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());
      recordable.SetSpanKind(kind);
    });
    EXPECT_EQ(encodings.protobuf, encodings.protobuf_free) << "span kind " << static_cast<int>(kind);
  }
}

// Several spans across two resources and two scopes, which is what exercises
// the grouping and the order the groups come out in.
TEST(OtlpJsonTraceEquivalence, GroupsByResourceAndScope)
{
  static auto resource_a =
      opentelemetry::sdk::resource::Resource::Create({{"service.name", "a"}});
  static auto resource_b =
      opentelemetry::sdk::resource::Resource::Create({{"service.name", "b"}});
  static auto scope_one =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("scope_one", "1");
  static auto scope_two =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("scope_two", "2");

  auto encodings = EncodeBothWays(6, [](std::size_t index, sdk_trace::Recordable &recordable) {
    recordable.SetResource((index % 2 == 0) ? resource_a : resource_b);
    recordable.SetInstrumentationScope((index % 3 == 0) ? *scope_one : *scope_two);
    recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());
    recordable.SetName("span " + std::to_string(index));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

// A resource with no attributes and a scope with no name, version or
// attributes each have no field set, and a message with no field set encodes
// as null rather than as an empty object. These are the emptiest inputs the
// mapping has to agree on.
TEST(OtlpJsonTraceEquivalence, EmptyResourceAndScopeAreNull)
{
  static auto scope =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("", "");

  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_trace::Recordable &recordable) {
    recordable.SetResource(opentelemetry::sdk::resource::Resource::GetEmpty());
    recordable.SetInstrumentationScope(*scope);
    recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_NE(encodings.protobuf_free.find("\"resource\":null"), std::string::npos)
      << encodings.protobuf_free;
  EXPECT_NE(encodings.protobuf_free.find("\"scope\":null"), std::string::npos)
      << encodings.protobuf_free;
}

// An array attribute holding no elements: the protobuf path creates the
// ArrayValue and never puts a value in it, leaving a message with no field set.
TEST(OtlpJsonTraceEquivalence, EmptyArrayAttributes)
{
  auto encodings = EncodeBothWays(1, [](std::size_t, sdk_trace::Recordable &recordable) {
    recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());
    recordable.SetAttribute("empty_bool_array",
                            nostd::span<const bool>(nullptr, static_cast<std::size_t>(0)));
    recordable.SetAttribute("empty_string_array", nostd::span<const nostd::string_view>(
                                                      nullptr, static_cast<std::size_t>(0)));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

TEST(OtlpJsonTraceEquivalence, NanosecondTimestampsSurviveAsStrings)
{
  // Beyond 2^53, where a JSON number would lose the low bits to double
  // precision. The assertion is not only that the two agree but that the value
  // is present in full.
  constexpr std::uint64_t kStartNanos = 1700000000123456789ULL;

  auto encodings = EncodeBothWays(1, [kStartNanos](std::size_t, sdk_trace::Recordable &recordable) {
    recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());
    recordable.SetStartTime(opentelemetry::common::SystemTimestamp(
        std::chrono::nanoseconds(static_cast<std::int64_t>(kStartNanos))));
    recordable.SetDuration(std::chrono::nanoseconds(1));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
  EXPECT_NE(encodings.protobuf_free.find("\"1700000000123456789\""), std::string::npos)
      << encodings.protobuf_free;
  EXPECT_NE(encodings.protobuf_free.find("\"1700000000123456790\""), std::string::npos)
      << encodings.protobuf_free;
}

TEST(OtlpJsonTraceEquivalence, DroppedCountsFromSpanLimits)
{
  sdk_trace::SpanLimits limits;
  limits.attribute_count_limit       = 1;
  limits.event_count_limit           = 1;
  limits.link_count_limit            = 1;
  limits.event_attribute_count_limit = 1;
  limits.link_attribute_count_limit  = 1;

  auto encodings = EncodeBothWays(1, [limits](std::size_t, sdk_trace::Recordable &recordable) {
    recordable.SetSpanLimits(limits);
    recordable.SetIdentity(MakeSpanContext(kTraceIdBytes, kSpanIdBytes), trace_api::SpanId());

    recordable.SetAttribute("kept", "yes");
    recordable.SetAttribute("dropped", "no");

    std::map<std::string, std::string> event_attributes = {{"kept", "yes"}, {"dropped", "no"}};
    recordable.AddEvent(
        "kept event", opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1)),
        opentelemetry::common::MakeAttributes(event_attributes));
    recordable.AddEvent(
        "dropped event", opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(2)),
        opentelemetry::common::MakeAttributes(event_attributes));

    std::map<std::string, std::string> link_attributes = {{"kept", "yes"}, {"dropped", "no"}};
    recordable.AddLink(MakeSpanContext(kLinkTraceIdBytes, kLinkSpanIdBytes),
                       opentelemetry::common::MakeAttributes(link_attributes));
    recordable.AddLink(MakeSpanContext(kLinkTraceIdBytes, kLinkSpanIdBytes),
                       opentelemetry::common::MakeAttributes(link_attributes));
  });

  EXPECT_EQ(encodings.protobuf, encodings.protobuf_free);
}

}  // namespace
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
