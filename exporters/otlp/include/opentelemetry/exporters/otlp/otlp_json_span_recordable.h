// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/span_limits.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * A span recorded in plain C++ types, holding exactly what OTLP/JSON needs to
 * emit and nothing protobuf.
 *
 * The SDK already ships SpanData, which is also protobuf-free, but it keeps
 * attributes in an unordered_map. OTLP puts attributes in a JSON array, whose
 * order is preserved on the wire, so an unordered container cannot reproduce
 * the order the protobuf path emits and the two encodings would disagree for
 * any span carrying more than one attribute. Attributes are therefore kept in
 * insertion order here, the way a protobuf repeated field keeps them.
 *
 * The recording semantics -- when a value is dropped, when a count is
 * incremented, which limit applies where -- mirror OtlpRecordable exactly.
 * Any divergence would show up as a difference in the emitted JSON rather than
 * as a local bug, so the two must stay in step.
 */
class OtlpJsonSpanRecordable final : public opentelemetry::sdk::trace::Recordable
{
public:
  /** A key and its value, in the order the span recorded them. */
  using OrderedAttributes =
      std::vector<std::pair<std::string, opentelemetry::sdk::common::OwnedAttributeValue>>;

  struct Event
  {
    std::string name;
    std::uint64_t time_unix_nano = 0;
    OrderedAttributes attributes;
    std::uint32_t dropped_attributes_count = 0;
  };

  struct Link
  {
    opentelemetry::trace::TraceId trace_id;
    opentelemetry::trace::SpanId span_id;
    std::string trace_state;
    OrderedAttributes attributes;
    std::uint32_t dropped_attributes_count = 0;
  };

  /** The status a span carries, absent until SetStatus is called. */
  struct Status
  {
    bool is_set = false;
    opentelemetry::trace::StatusCode code = opentelemetry::trace::StatusCode::kUnset;
    std::string message;
  };

  const opentelemetry::trace::TraceId &GetTraceId() const noexcept { return trace_id_; }
  const opentelemetry::trace::SpanId &GetSpanId() const noexcept { return span_id_; }
  const opentelemetry::trace::SpanId &GetParentSpanId() const noexcept { return parent_span_id_; }
  const std::string &GetTraceState() const noexcept { return trace_state_; }
  const std::string &GetName() const noexcept { return name_; }
  opentelemetry::trace::SpanKind GetSpanKind() const noexcept { return span_kind_; }

  /** False until SetSpanKind is called. The SDK enumeration has no member for
   * OTLP's unspecified kind, and a span that was never given one must encode
   * as unspecified rather than as the kInternal this defaults to. */
  bool HasSpanKind() const noexcept { return has_span_kind_; }
  std::uint64_t GetStartTimeUnixNano() const noexcept { return start_time_unix_nano_; }
  std::uint64_t GetEndTimeUnixNano() const noexcept { return end_time_unix_nano_; }
  std::uint32_t GetFlags() const noexcept { return flags_; }
  const OrderedAttributes &GetAttributes() const noexcept { return attributes_; }
  std::uint32_t GetDroppedAttributesCount() const noexcept { return dropped_attributes_count_; }
  const std::vector<Event> &GetEvents() const noexcept { return events_; }
  std::uint32_t GetDroppedEventsCount() const noexcept { return dropped_events_count_; }
  const std::vector<Link> &GetLinks() const noexcept { return links_; }
  std::uint32_t GetDroppedLinksCount() const noexcept { return dropped_links_count_; }
  const Status &GetStatus() const noexcept { return status_; }

  /** Null when the span was recorded without one, in which case the enclosing
   * message omits the field entirely rather than emitting an empty one. */
  const opentelemetry::sdk::resource::Resource *GetResource() const noexcept { return resource_; }

  const opentelemetry::sdk::instrumentationscope::InstrumentationScope *GetInstrumentationScope()
      const noexcept
  {
    return instrumentation_scope_;
  }

  void SetIdentity(const opentelemetry::trace::SpanContext &span_context,
                   opentelemetry::trace::SpanId parent_span_id) noexcept override;

  void SetAttribute(nostd::string_view key,
                    const opentelemetry::common::AttributeValue &value) noexcept override;

  void AddEvent(nostd::string_view name,
                opentelemetry::common::SystemTimestamp timestamp,
                const opentelemetry::common::KeyValueIterable &attributes) noexcept override;

  void AddLink(const opentelemetry::trace::SpanContext &span_context,
               const opentelemetry::common::KeyValueIterable &attributes) noexcept override;

  void SetStatus(opentelemetry::trace::StatusCode code,
                 nostd::string_view description) noexcept override;

  void SetName(nostd::string_view name) noexcept override;

  void SetTraceFlags(opentelemetry::trace::TraceFlags flags) noexcept override;

  void SetSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept override;

  void SetResource(const opentelemetry::sdk::resource::Resource &resource) noexcept override;

  void SetStartTime(opentelemetry::common::SystemTimestamp start_time) noexcept override;

  void SetDuration(std::chrono::nanoseconds duration) noexcept override;

  void SetSpanLimits(const opentelemetry::sdk::trace::SpanLimits &limits) noexcept override;

  void SetInstrumentationScope(const opentelemetry::sdk::instrumentationscope::InstrumentationScope
                                   &instrumentation_scope) noexcept override;

private:
  opentelemetry::trace::TraceId trace_id_;
  opentelemetry::trace::SpanId span_id_;
  opentelemetry::trace::SpanId parent_span_id_;
  std::string trace_state_;
  std::string name_;
  opentelemetry::trace::SpanKind span_kind_ = opentelemetry::trace::SpanKind::kInternal;
  bool has_span_kind_                       = false;
  std::uint64_t start_time_unix_nano_       = 0;
  std::uint64_t end_time_unix_nano_        = 0;
  std::uint32_t flags_                     = 0;
  OrderedAttributes attributes_;
  std::uint32_t dropped_attributes_count_ = 0;
  std::vector<Event> events_;
  std::uint32_t dropped_events_count_ = 0;
  std::vector<Link> links_;
  std::uint32_t dropped_links_count_ = 0;
  Status status_;
  const opentelemetry::sdk::resource::Resource *resource_ = nullptr;
  const opentelemetry::sdk::instrumentationscope::InstrumentationScope *instrumentation_scope_ =
      nullptr;
  opentelemetry::sdk::trace::SpanLimits span_limits_{
      opentelemetry::sdk::trace::SpanLimits::NoLimits()};
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
