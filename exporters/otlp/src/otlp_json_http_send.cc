// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/detail/otlp_json_http_send.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/detail/otlp_http_transport.h"
#include "opentelemetry/exporters/otlp/otlp_http_client.h"
#include "opentelemetry/exporters/otlp/otlp_json_partial_success.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/version.h"

namespace http_client = opentelemetry::ext::http::client;

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

sdk::common::ExportResult SendOtlpJsonRequest(
    OtlpHttpTransport &transport,
    const std::shared_ptr<JsonReaderFactory> &reader_factory,
    const OtlpPartialSuccessSignal &signal,
    const std::string &body_json,
    std::size_t exported_count,
    std::size_t max_running_requests) noexcept
{
  http_client::Body body(body_json.begin(), body_json.end());

  // The transport reports the HTTP exchange alone and cannot see what the callback made of
  // the response, so a synchronous export has to capture its own result.
  std::shared_ptr<sdk::common::ExportResult> session_result;
  if (max_running_requests == 0)
  {
    session_result =
        std::make_shared<sdk::common::ExportResult>(sdk::common::ExportResult::kSuccess);
  }

  auto handle_result = [reader_factory, &signal, exported_count, session_result](
                           sdk::common::ExportResult result,
                           const http_client::Body &response_body) {
    if (result != sdk::common::ExportResult::kSuccess)
    {
      OTEL_INTERNAL_LOG_ERROR(signal.log_prefix << " ERROR: Export " << exported_count << " "
                                                << signal.exported_noun
                                                << " error: " << static_cast<int>(result));
    }
    else
    {
      auto reader = reader_factory->Create();
      const nostd::string_view body_view(reinterpret_cast<const char *>(response_body.data()),
                                         response_body.size());
      // An unreadable body means the export cannot be reported as having landed, which is
      // what the protobuf response path does with a body it fails to parse.
      if (!LogOtlpJsonPartialSuccess(*reader, body_view, signal, exported_count))
      {
        result = sdk::common::ExportResult::kFailure;
      }
    }

    if (session_result)
    {
      *session_result = result;
    }
    return true;
  };

  const auto transport_result = transport.Export(std::move(body), kHttpJsonContentType,
                                                 std::move(handle_result), max_running_requests);

  if (max_running_requests == 0 && transport_result == sdk::common::ExportResult::kSuccess)
  {
    return *session_result;
  }

  return transport_result;
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
