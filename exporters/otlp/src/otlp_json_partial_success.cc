// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_partial_success.h"

#include <cstddef>
#include <string>

#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/global_log_handler.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{
constexpr const char *kPartialSuccessKey = "partialSuccess";
constexpr const char *kErrorMessageKey   = "errorMessage";
}  // namespace

const OtlpPartialSuccessSignal &OtlpTracePartialSuccessSignal() noexcept
{
  static const OtlpPartialSuccessSignal signal{"[OTLP TRACE HTTP Exporter]", "rejectedSpans",
                                               "span(s)", "trace span(s)"};
  return signal;
}

const OtlpPartialSuccessSignal &OtlpMetricPartialSuccessSignal() noexcept
{
  static const OtlpPartialSuccessSignal signal{"[OTLP METRIC HTTP Exporter]", "rejectedDataPoints",
                                               "data point(s)", "metric(s)"};
  return signal;
}

const OtlpPartialSuccessSignal &OtlpLogPartialSuccessSignal() noexcept
{
  static const OtlpPartialSuccessSignal signal{"[OTLP LOG HTTP Exporter]", "rejectedLogRecords",
                                               "log record(s)", "log(s)"};
  return signal;
}

bool ParseOtlpJsonPartialSuccess(JsonReader &reader,
                                 nostd::string_view body,
                                 const OtlpPartialSuccessSignal &signal,
                                 OtlpPartialSuccess &partial_success) noexcept
{
  partial_success = OtlpPartialSuccess{};

  if (body.empty())
  {
    return true;
  }
  if (!reader.Parse(body))
  {
    return false;
  }
  if (!reader.EnterObject(kPartialSuccessKey))
  {
    return true;
  }

  reader.GetInt64(signal.rejected_count_key, partial_success.rejected_count);
  reader.GetString(kErrorMessageKey, partial_success.error_message);
  reader.LeaveObject();
  return true;
}

bool LogOtlpJsonPartialSuccess(JsonReader &reader,
                               nostd::string_view body,
                               const OtlpPartialSuccessSignal &signal,
                               std::size_t exported_count) noexcept
{
  OtlpPartialSuccess partial_success;
  if (!ParseOtlpJsonPartialSuccess(reader, body, signal, partial_success))
  {
    OTEL_INTERNAL_LOG_ERROR(signal.log_prefix << " Failed to parse JSON response body");
    return false;
  }

  if (partial_success.rejected_count != 0 || !partial_success.error_message.empty())
  {
    OTEL_INTERNAL_LOG_ERROR(signal.log_prefix
                            << " Export partial success: " << partial_success.rejected_count << " "
                            << signal.rejected_noun << " rejected: \""
                            << partial_success.error_message << "\"");
  }
  else
  {
    OTEL_INTERNAL_LOG_DEBUG(signal.log_prefix << " Export " << exported_count << " "
                                              << signal.exported_noun << " success");
  }
  return true;
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
