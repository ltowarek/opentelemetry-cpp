// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "opentelemetry/exporters/otlp/detail/otlp_http_transport.h"
#include "opentelemetry/exporters/otlp/otlp_json_partial_success.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

/**
 * Posts a finished OTLP/JSON request body and reports what came back.
 *
 * What the three signals share once their data is encoded: the failure log
 * line, reading the response for a partial success, and -- because the
 * transport reports the HTTP exchange alone -- capturing the result the
 * response reading settled on, so that a synchronous export reports a body it
 * could not read as a failure rather than a success. Only the wording and the
 * rejected-count key differ, and those arrive in `signal`.
 *
 * @param transport the send path to post through
 * @param reader_factory used to read the response body, once per export; held for the
 *        lifetime of an in-flight asynchronous export, which can outlive this call
 * @param signal names the signal in the log lines and in the response
 * @param body_json the encoded request
 * @param exported_count how many spans, points or records `body_json` carries
 * @param max_running_requests 0 to export synchronously, as OtlpHttpTransport takes it
 */
sdk::common::ExportResult SendOtlpJsonRequest(
    OtlpHttpTransport &transport,
    const std::shared_ptr<JsonReaderFactory> &reader_factory,
    const OtlpPartialSuccessSignal &signal,
    const std::string &body_json,
    std::size_t exported_count,
    std::size_t max_running_requests) noexcept;

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
