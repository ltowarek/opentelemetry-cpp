// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_span_recordable.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/key_value_iterable.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/span_limits.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

// The bits of TraceFlags that OTLP carries in Span.flags. The proto defines
// this as SPAN_FLAGS_TRACE_FLAGS_MASK; it is spelled out here because this
// translation unit does not include the generated headers.
constexpr std::uint32_t kSpanFlagsTraceFlagsMask = 0x000000FF;

// Copies the attributes an event or link was given, stopping at `count_limit`
// and reporting how many were turned away, exactly as the protobuf recordable
// does for its repeated field.
//
// Values are truncated here rather than at emit time because that is the one
// point both paths share: the protobuf path truncates as it converts into the
// message, so a value stored untruncated would still have to be cut later and
// the limit carried along to do it.
void CollectAttributes(const opentelemetry::common::KeyValueIterable &attributes,
                       std::uint32_t count_limit,
                       std::size_t value_length_limit,
                       OtlpJsonSpanRecordable::OrderedAttributes &out,
                       std::uint32_t &dropped_count) noexcept
{
  attributes.ForEachKeyValue(
      [&](nostd::string_view key, opentelemetry::common::AttributeValue value) noexcept {
        if (static_cast<std::uint32_t>(out.size()) >= count_limit)
        {
          ++dropped_count;
          return true;
        }
        auto converted = opentelemetry::sdk::common::VisitVariant(
            opentelemetry::sdk::common::AttributeConverter{value_length_limit}, value);
        if (converted.second)
        {
          out.emplace_back(std::string(key), std::move(converted.first));
        }
        return true;
      });
}

}  // namespace

void OtlpJsonSpanRecordable::SetIdentity(const opentelemetry::trace::SpanContext &span_context,
                                         opentelemetry::trace::SpanId parent_span_id) noexcept
{
  trace_id_ = span_context.trace_id();
  span_id_  = span_context.span_id();
  if (parent_span_id.IsValid())
  {
    parent_span_id_ = parent_span_id;
  }
  trace_state_ = span_context.trace_state()->ToHeader();
}

void OtlpJsonSpanRecordable::SetAttribute(nostd::string_view key,
                                          const opentelemetry::common::AttributeValue &value) noexcept
{
  if (key.empty())
  {
    return;
  }

  if (static_cast<std::uint32_t>(attributes_.size()) >= span_limits_.attribute_count_limit)
  {
    ++dropped_attributes_count_;
    return;
  }

  auto converted = opentelemetry::sdk::common::VisitVariant(
      opentelemetry::sdk::common::AttributeConverter{span_limits_.attribute_value_length_limit},
      value);
  if (converted.second)
  {
    attributes_.emplace_back(std::string(key), std::move(converted.first));
  }
}

void OtlpJsonSpanRecordable::AddEvent(
    nostd::string_view name,
    opentelemetry::common::SystemTimestamp timestamp,
    const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  if (static_cast<std::uint32_t>(events_.size()) >= span_limits_.event_count_limit)
  {
    ++dropped_events_count_;
    return;
  }

  Event event;
  event.name = std::string(name);
  event.time_unix_nano =
      static_cast<std::uint64_t>(timestamp.time_since_epoch().count());
  CollectAttributes(attributes, span_limits_.event_attribute_count_limit,
                    span_limits_.attribute_value_length_limit, event.attributes,
                    event.dropped_attributes_count);
  events_.emplace_back(std::move(event));
}

void OtlpJsonSpanRecordable::AddLink(
    const opentelemetry::trace::SpanContext &span_context,
    const opentelemetry::common::KeyValueIterable &attributes) noexcept
{
  if (static_cast<std::uint32_t>(links_.size()) >= span_limits_.link_count_limit)
  {
    ++dropped_links_count_;
    return;
  }

  Link link;
  link.trace_id    = span_context.trace_id();
  link.span_id     = span_context.span_id();
  link.trace_state = span_context.trace_state()->ToHeader();
  CollectAttributes(attributes, span_limits_.link_attribute_count_limit,
                    span_limits_.attribute_value_length_limit, link.attributes,
                    link.dropped_attributes_count);
  links_.emplace_back(std::move(link));
}

void OtlpJsonSpanRecordable::SetStatus(opentelemetry::trace::StatusCode code,
                                       nostd::string_view description) noexcept
{
  status_.is_set = true;
  status_.code   = code;
  // A description only travels with an error; the proto path drops it for any
  // other code rather than emitting a message the receiver would ignore.
  if (code == opentelemetry::trace::StatusCode::kError)
  {
    status_.message = std::string(description);
  }
}

void OtlpJsonSpanRecordable::SetName(nostd::string_view name) noexcept
{
  name_ = std::string(name);
}

void OtlpJsonSpanRecordable::SetTraceFlags(opentelemetry::trace::TraceFlags flags) noexcept
{
  flags_ = flags.flags() & kSpanFlagsTraceFlagsMask;
}

void OtlpJsonSpanRecordable::SetSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept
{
  span_kind_     = span_kind;
  has_span_kind_ = true;
}

void OtlpJsonSpanRecordable::SetResource(
    const opentelemetry::sdk::resource::Resource &resource) noexcept
{
  resource_ = &resource;
}

void OtlpJsonSpanRecordable::SetStartTime(
    opentelemetry::common::SystemTimestamp start_time) noexcept
{
  start_time_unix_nano_ = static_cast<std::uint64_t>(start_time.time_since_epoch().count());
}

void OtlpJsonSpanRecordable::SetDuration(std::chrono::nanoseconds duration) noexcept
{
  end_time_unix_nano_ = start_time_unix_nano_ + static_cast<std::uint64_t>(duration.count());
}

void OtlpJsonSpanRecordable::SetSpanLimits(
    const opentelemetry::sdk::trace::SpanLimits &limits) noexcept
{
  // Take the most restrictive of what is already set and what is arriving, the
  // same way the protobuf recordable reconciles its deprecated constructor
  // limits with the configured ones.
  span_limits_ = {
      (std::min)(span_limits_.attribute_count_limit, limits.attribute_count_limit),
      (std::min)(span_limits_.attribute_value_length_limit, limits.attribute_value_length_limit),
      (std::min)(span_limits_.event_count_limit, limits.event_count_limit),
      (std::min)(span_limits_.link_count_limit, limits.link_count_limit),
      (std::min)(span_limits_.event_attribute_count_limit, limits.event_attribute_count_limit),
      (std::min)(span_limits_.link_attribute_count_limit, limits.link_attribute_count_limit),
  };
}

void OtlpJsonSpanRecordable::SetInstrumentationScope(
    const opentelemetry::sdk::instrumentationscope::InstrumentationScope
        &instrumentation_scope) noexcept
{
  instrumentation_scope_ = &instrumentation_scope;
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
