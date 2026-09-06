// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/otlp_json_mapping.h"
#include "opentelemetry/logs/severity.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/log_record_limits.h"
#include "opentelemetry/sdk/logs/recordable.h"
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

/**
 * A log record recorded in plain C++ types, holding exactly what OTLP/JSON
 * needs to emit and nothing protobuf.
 *
 * Attributes are kept in insertion order for the same reason
 * OtlpJsonSpanRecordable keeps them so: OTLP puts them in a JSON array, whose
 * order is on the wire, and the SDK's own ReadWriteLogRecord stores them in an
 * unordered map.
 *
 * The recording semantics -- which limit applies where, when a count is
 * incremented, what the severity maps to -- mirror OtlpLogRecordable exactly.
 * Any divergence would show up as a difference in the emitted JSON rather than
 * as a local bug, so the two must stay in step.
 */
class OtlpJsonLogRecordable final : public opentelemetry::sdk::logs::Recordable
{
public:
  using OrderedAttributes = json_mapping::OrderedAttributes;

  std::uint64_t GetTimestamp() const noexcept { return time_unix_nano_; }
  std::uint64_t GetObservedTimestamp() const noexcept { return observed_time_unix_nano_; }

  /** The OTLP SeverityNumber, zero when the record was never given a severity
   * or was given one outside the enumeration. */
  std::int32_t GetSeverityNumber() const noexcept { return severity_number_; }
  const std::string &GetSeverityText() const noexcept { return severity_text_; }

  /** Whether SetBody was called. The body is a message slot the protobuf path
   * creates on the first call, so it is on the wire from then on even when the
   * value it holds encodes as null. */
  bool HasBody() const noexcept { return has_body_; }
  const opentelemetry::sdk::common::OwnedAttributeValue &GetBody() const noexcept { return body_; }

  const OrderedAttributes &GetAttributes() const noexcept { return attributes_; }
  std::uint32_t GetDroppedAttributesCount() const noexcept { return dropped_attributes_count_; }
  std::uint32_t GetFlags() const noexcept { return flags_; }
  const opentelemetry::trace::TraceId &GetTraceId() const noexcept { return trace_id_; }
  const opentelemetry::trace::SpanId &GetSpanId() const noexcept { return span_id_; }
  const std::string &GetEventName() const noexcept { return event_name_; }

  /** The resource this record was given, or the SDK's default one. Unlike a
   * span, a log record always has both a resource and a scope, so the
   * enclosing message always carries them. */
  const opentelemetry::sdk::resource::Resource &GetResource() const noexcept;

  const opentelemetry::sdk::instrumentationscope::InstrumentationScope &GetInstrumentationScope()
      const noexcept;

  void SetTimestamp(opentelemetry::common::SystemTimestamp timestamp) noexcept override;

  void SetObservedTimestamp(opentelemetry::common::SystemTimestamp timestamp) noexcept override;

  void SetSeverity(opentelemetry::logs::Severity severity) noexcept override;

  void SetBody(const opentelemetry::common::AttributeValue &message) noexcept override;

  void SetEventId(std::int64_t id, nostd::string_view event_name) noexcept override;

  void SetTraceId(const opentelemetry::trace::TraceId &trace_id) noexcept override;

  void SetSpanId(const opentelemetry::trace::SpanId &span_id) noexcept override;

  void SetTraceFlags(const opentelemetry::trace::TraceFlags &trace_flags) noexcept override;

  void SetAttribute(nostd::string_view key,
                    const opentelemetry::common::AttributeValue &value) noexcept override;

  void SetLogRecordLimits(
      const opentelemetry::sdk::logs::LogRecordLimits &limits) noexcept override;

  void SetResource(const opentelemetry::sdk::resource::Resource &resource) noexcept override;

  void SetInstrumentationScope(const opentelemetry::sdk::instrumentationscope::InstrumentationScope
                                   &instrumentation_scope) noexcept override;

private:
  std::uint64_t time_unix_nano_          = 0;
  std::uint64_t observed_time_unix_nano_ = 0;
  std::int32_t severity_number_          = 0;
  std::string severity_text_;
  opentelemetry::sdk::common::OwnedAttributeValue body_;
  bool has_body_ = false;
  OrderedAttributes attributes_;
  std::uint32_t dropped_attributes_count_ = 0;
  std::uint32_t flags_                    = 0;
  opentelemetry::trace::TraceId trace_id_;
  opentelemetry::trace::SpanId span_id_;
  std::string event_name_;
  const opentelemetry::sdk::resource::Resource *resource_ = nullptr;
  const opentelemetry::sdk::instrumentationscope::InstrumentationScope *instrumentation_scope_ =
      nullptr;
  opentelemetry::sdk::logs::LogRecordLimits limits_ =
      opentelemetry::sdk::logs::LogRecordLimits::NoLimits();
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
