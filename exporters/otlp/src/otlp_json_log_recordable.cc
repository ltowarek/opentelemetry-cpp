// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_log_recordable.h"

#include <cstdint>
#include <string>
#include <utility>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/macros.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/log_record_limits.h"
#include "opentelemetry/sdk/logs/readable_log_record.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

// One severity as it goes on the wire: the OTLP SeverityNumber and the text
// the protobuf recordable sets alongside it. The two are written out rather
// than derived from the SDK enum, so that a value outside the enumeration
// lands on the same INVALID/unspecified pair the protobuf path produces
// instead of indexing SeverityNumToText out of range.
struct SeverityEncoding
{
  std::int32_t number;
  const char *text;
};

SeverityEncoding MapSeverity(opentelemetry::logs::Severity severity) noexcept
{
  switch (severity)
  {
    case opentelemetry::logs::Severity::kTrace:
      return {1, "TRACE"};
    case opentelemetry::logs::Severity::kTrace2:
      return {2, "TRACE2"};
    case opentelemetry::logs::Severity::kTrace3:
      return {3, "TRACE3"};
    case opentelemetry::logs::Severity::kTrace4:
      return {4, "TRACE4"};
    case opentelemetry::logs::Severity::kDebug:
      return {5, "DEBUG"};
    case opentelemetry::logs::Severity::kDebug2:
      return {6, "DEBUG2"};
    case opentelemetry::logs::Severity::kDebug3:
      return {7, "DEBUG3"};
    case opentelemetry::logs::Severity::kDebug4:
      return {8, "DEBUG4"};
    case opentelemetry::logs::Severity::kInfo:
      return {9, "INFO"};
    case opentelemetry::logs::Severity::kInfo2:
      return {10, "INFO2"};
    case opentelemetry::logs::Severity::kInfo3:
      return {11, "INFO3"};
    case opentelemetry::logs::Severity::kInfo4:
      return {12, "INFO4"};
    case opentelemetry::logs::Severity::kWarn:
      return {13, "WARN"};
    case opentelemetry::logs::Severity::kWarn2:
      return {14, "WARN2"};
    case opentelemetry::logs::Severity::kWarn3:
      return {15, "WARN3"};
    case opentelemetry::logs::Severity::kWarn4:
      return {16, "WARN4"};
    case opentelemetry::logs::Severity::kError:
      return {17, "ERROR"};
    case opentelemetry::logs::Severity::kError2:
      return {18, "ERROR2"};
    case opentelemetry::logs::Severity::kError3:
      return {19, "ERROR3"};
    case opentelemetry::logs::Severity::kError4:
      return {20, "ERROR4"};
    case opentelemetry::logs::Severity::kFatal:
      return {21, "FATAL"};
    case opentelemetry::logs::Severity::kFatal2:
      return {22, "FATAL2"};
    case opentelemetry::logs::Severity::kFatal3:
      return {23, "FATAL3"};
    case opentelemetry::logs::Severity::kFatal4:
      return {24, "FATAL4"};
    default:
      return {0, "INVALID"};
  }
}

}  // namespace

const opentelemetry::sdk::resource::Resource &OtlpJsonLogRecordable::GetResource() const noexcept
{
  if OPENTELEMETRY_LIKELY_CONDITION (nullptr != resource_)
  {
    return *resource_;
  }

  return opentelemetry::sdk::logs::ReadableLogRecord::GetDefaultResource();
}

const opentelemetry::sdk::instrumentationscope::InstrumentationScope &
OtlpJsonLogRecordable::GetInstrumentationScope() const noexcept
{
  if OPENTELEMETRY_LIKELY_CONDITION (nullptr != instrumentation_scope_)
  {
    return *instrumentation_scope_;
  }

  return opentelemetry::sdk::logs::ReadableLogRecord::GetDefaultInstrumentationScope();
}

void OtlpJsonLogRecordable::SetTimestamp(
    opentelemetry::common::SystemTimestamp timestamp) noexcept
{
  time_unix_nano_ = static_cast<std::uint64_t>(timestamp.time_since_epoch().count());
}

void OtlpJsonLogRecordable::SetObservedTimestamp(
    opentelemetry::common::SystemTimestamp timestamp) noexcept
{
  observed_time_unix_nano_ = static_cast<std::uint64_t>(timestamp.time_since_epoch().count());
}

void OtlpJsonLogRecordable::SetSeverity(opentelemetry::logs::Severity severity) noexcept
{
  const SeverityEncoding encoding = MapSeverity(severity);
  severity_number_                = encoding.number;
  severity_text_                  = encoding.text;
}

void OtlpJsonLogRecordable::SetBody(const opentelemetry::common::AttributeValue &message) noexcept
{
  // No length limit: the protobuf path populates the body with the default
  // converter options, so attribute_value_length_limit does not reach it.
  auto converted =
      opentelemetry::sdk::common::VisitVariant(opentelemetry::sdk::common::AttributeConverter{},
                                               message);
  has_body_ = true;
  if (converted.second)
  {
    body_ = std::move(converted.first);
  }
}

void OtlpJsonLogRecordable::SetEventId(std::int64_t /* id */,
                                       nostd::string_view event_name) noexcept
{
  event_name_ = std::string(event_name);
}

void OtlpJsonLogRecordable::SetTraceId(const opentelemetry::trace::TraceId &trace_id) noexcept
{
  trace_id_ = trace_id;
}

void OtlpJsonLogRecordable::SetSpanId(const opentelemetry::trace::SpanId &span_id) noexcept
{
  span_id_ = span_id;
}

void OtlpJsonLogRecordable::SetTraceFlags(
    const opentelemetry::trace::TraceFlags &trace_flags) noexcept
{
  // Unmasked, unlike a span's: LogRecord.flags carries the whole byte.
  flags_ = trace_flags.flags();
}

void OtlpJsonLogRecordable::SetAttribute(
    nostd::string_view key,
    const opentelemetry::common::AttributeValue &value) noexcept
{
  if (key.empty())
  {
    return;
  }

  if (attributes_.size() >= limits_.attribute_count_limit)
  {
    ++dropped_attributes_count_;
    return;
  }

  auto converted = opentelemetry::sdk::common::VisitVariant(
      opentelemetry::sdk::common::AttributeConverter{limits_.attribute_value_length_limit}, value);
  if (converted.second)
  {
    attributes_.emplace_back(std::string(key), std::move(converted.first));
  }
}

void OtlpJsonLogRecordable::SetLogRecordLimits(
    const opentelemetry::sdk::logs::LogRecordLimits &limits) noexcept
{
  limits_ = limits;
}

void OtlpJsonLogRecordable::SetResource(
    const opentelemetry::sdk::resource::Resource &resource) noexcept
{
  resource_ = &resource;
}

void OtlpJsonLogRecordable::SetInstrumentationScope(
    const opentelemetry::sdk::instrumentationscope::InstrumentationScope
        &instrumentation_scope) noexcept
{
  instrumentation_scope_ = &instrumentation_scope;
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
