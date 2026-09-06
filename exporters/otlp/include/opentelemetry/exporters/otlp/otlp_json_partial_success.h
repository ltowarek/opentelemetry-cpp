// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * The partial-success field of an OTLP export response, which is the whole
 * of what an exporter reads back. Both members carry their proto3 default
 * when the response reports nothing rejected.
 */
struct OtlpPartialSuccess
{
  std::int64_t rejected_count = 0;
  std::string error_message;
};

/**
 * What separates one signal's response from another: the key holding the
 * rejected count, and the wording of the two log lines. Everything else
 * about the three responses is identical, so they share one parser.
 */
struct OtlpPartialSuccessSignal
{
  nostd::string_view log_prefix;          // "[OTLP TRACE HTTP Exporter]"
  nostd::string_view rejected_count_key;  // "rejectedSpans"
  nostd::string_view rejected_noun;       // "span(s)"
  nostd::string_view exported_noun;       // "trace span(s)"
};

OPENTELEMETRY_EXPORT const OtlpPartialSuccessSignal &OtlpTracePartialSuccessSignal() noexcept;
OPENTELEMETRY_EXPORT const OtlpPartialSuccessSignal &OtlpMetricPartialSuccessSignal() noexcept;
OPENTELEMETRY_EXPORT const OtlpPartialSuccessSignal &OtlpLogPartialSuccessSignal() noexcept;

/**
 * Parses the partial-success field out of an OTLP/JSON export response
 * body. An empty body, or one carrying no partialSuccess object, leaves
 * `partial_success` at its defaults and succeeds: OTLP defines a body-less
 * 2xx as full success, and a partialSuccess whose members are absent
 * carries the proto3 defaults, which is what the protobuf path reports for
 * the same body.
 *
 * Returns false only when the body is non-empty and cannot be read as a
 * JSON object. Anything else malformed inside it — a partialSuccess of the
 * wrong type, a member of the wrong type, a field this version does not
 * know — is read past rather than rejected. That is a deliberate
 * divergence from the protobuf path, which fails the whole body on any of
 * the three: on a device whose only diagnostic channel is this log line, a
 * collector that adds a field must not cost the exporter its ability to
 * report a rejection.
 */
OPENTELEMETRY_EXPORT bool ParseOtlpJsonPartialSuccess(JsonReader &reader,
                                                      nostd::string_view body,
                                                      const OtlpPartialSuccessSignal &signal,
                                                      OtlpPartialSuccess &partial_success) noexcept;

/**
 * Parses a response body and reports it through the SDK internal log
 * handler, reproducing what the protobuf response path logs: an error
 * naming the rejected count and message when either is set, and a debug
 * line naming `exported_count` otherwise. An unreadable body is logged as
 * an error and reported by a false return, which a caller wiring this into
 * an export path must map onto a failed ExportResult the way the protobuf
 * response path does, so that an unreadable body is not read as a success.
 */
OPENTELEMETRY_EXPORT bool LogOtlpJsonPartialSuccess(JsonReader &reader,
                                                    nostd::string_view body,
                                                    const OtlpPartialSuccessSignal &signal,
                                                    std::size_t exported_count) noexcept;

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
