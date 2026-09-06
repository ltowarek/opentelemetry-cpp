// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_http_client.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/detail/otlp_http_transport.h"
#include "opentelemetry/exporters/otlp/otlp_http.h"
#include "opentelemetry/exporters/otlp/otlp_json_converter.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer_factory.h"
#include "opentelemetry/ext/http/client/detail/default_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h" // IWYU pragma: keep
// clang-format on
#include <google/protobuf/arena.h>
#include <google/protobuf/message.h>
#include <google/protobuf/stubs/port.h>
#include <google/protobuf/util/json_util.h>
// IWYU pragma: no_include <google/protobuf/stubs/status.h>
// IWYU pragma: no_include <google/protobuf/stubs/stringpiece.h>
// IWYU pragma: no_include <google/protobuf/json/json.h>
// IWYU pragma: no_include <absl/status/status.h>
// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h" // IWYU pragma: keep
// clang-format on

#ifdef GetMessage
#  undef GetMessage
#endif

namespace http_client = opentelemetry::ext::http::client;

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

bool SerializeToHttpBody(http_client::Body &output, const google::protobuf::Message &message)
{
  auto body_size = message.ByteSizeLong();
  if (body_size > 0)
  {
    output.resize(body_size);
    return message.SerializeWithCachedSizesToArray(
        reinterpret_cast<google::protobuf::uint8 *>(&output[0]));
  }
  return true;
}

}  // namespace

OtlpHttpClient::OtlpHttpClient(OtlpHttpClientOptions &&options)
    : OtlpHttpClient(std::move(options), ext::http::client::detail::GetDefaultHttpClientFactory())
{}

OtlpHttpClient::OtlpHttpClient(OtlpHttpClientOptions &&options,
                               const std::shared_ptr<ext::http::client::HttpClientFactory> &factory)
    : OtlpHttpClient(std::move(options), factory->Create(options.thread_instrumentation))
{}

OtlpHttpClient::OtlpHttpClient(OtlpHttpClientOptions &&options,
                               std::shared_ptr<ext::http::client::HttpClient> http_client)
    : json_writer_factory_(options.json_writer_factory ? options.json_writer_factory
                                                        : detail::GetDefaultJsonWriterFactory()),
      transport_(
          std::make_unique<detail::OtlpHttpTransport>(std::move(options), std::move(http_client)))
{}

OtlpHttpClient::~OtlpHttpClient() = default;

// ----------------------------- HTTP Client methods ------------------------------
sdk::common::ExportResult OtlpHttpClient::Export(
    const google::protobuf::Message &message,
    std::unique_ptr<google::protobuf::Arena> &&arena,
    google::protobuf::Message *response,
    std::function<bool(opentelemetry::sdk::common::ExportResult, google::protobuf::Message *)>
        &&result_callback,
    std::size_t max_running_requests) noexcept
{
  const OtlpHttpClientOptions &options = transport_->GetOptions();

  http_client::Body body_vec;
  std::string content_type;
  if (options.content_type == HttpRequestContentType::kBinary)
  {
    if (SerializeToHttpBody(body_vec, message))
    {
      if (options.console_debug)
      {
        OTEL_INTERNAL_LOG_DEBUG(
            "[OTLP HTTP Client] Request body(Binary): " << message.Utf8DebugString());
      }
    }
    else
    {
      if (options.console_debug)
      {
        OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Serialize body failed(Binary):"
                                << message.InitializationErrorString());
      }

      const auto result = opentelemetry::sdk::common::ExportResult::kFailure;
      result_callback(result, response);
      return result;
    }
    content_type = kHttpBinaryContentType;
  }
  else
  {
    std::unique_ptr<JsonWriter> json_writer = json_writer_factory_->Create();
    ConvertGenericMessageToJson(
        *json_writer, message,
        JsonConverterOptions{options.use_json_name, options.json_bytes_mapping});

    if (!json_writer->ok())
    {
      const auto result = opentelemetry::sdk::common::ExportResult::kFailure;
      result_callback(result, response);
      return result;
    }

    std::string post_body_json = json_writer->ToString();
    if (options.console_debug)
    {
      OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Request body(Json)" << post_body_json);
    }
    body_vec.assign(post_body_json.begin(), post_body_json.end());
    content_type = kHttpJsonContentType;
  }

  // The transport reports the outcome of the HTTP exchange alone, but parsing the response
  // is part of whether this export succeeded, so a synchronous export has to observe the
  // result the parse settled on rather than the one the transport returns.
  std::shared_ptr<sdk::common::ExportResult> session_result;
  if (max_running_requests == 0)
  {
    session_result =
        std::make_shared<sdk::common::ExportResult>(sdk::common::ExportResult::kSuccess);
  }

  // The arena owns `response`, so it has to outlive this callback rather than the call that
  // started the export; a shared_ptr because std::function requires a copyable target.
  std::shared_ptr<google::protobuf::Arena> shared_arena{std::move(arena)};

  auto completion = [shared_arena, response, session_result,
                     content_type_kind = options.content_type,
                     callback = std::move(result_callback)](sdk::common::ExportResult result,
                                                            const http_client::Body &body) {
    // On 2xx with a non-empty body, parse it into the caller-provided typed response
    if (response != nullptr && result == sdk::common::ExportResult::kSuccess && !body.empty())
    {
      const std::string body_string(body.begin(), body.end());
      if (content_type_kind == HttpRequestContentType::kJson)
      {
        if (!google::protobuf::util::JsonStringToMessage(body_string, response).ok())
        {
          OTEL_INTERNAL_LOG_ERROR("[OTLP HTTP Client] Failed to parse JSON response body");
          result = sdk::common::ExportResult::kFailure;
        }
      }
      else if (!response->ParseFromString(body_string))
      {
        OTEL_INTERNAL_LOG_ERROR("[OTLP HTTP Client] Failed to parse response body");
        result = sdk::common::ExportResult::kFailure;
      }
    }

    if (session_result)
    {
      *session_result = result;
    }

    return callback(result, response);
  };

  const auto transport_result = transport_->Export(std::move(body_vec), content_type,
                                                   std::move(completion), max_running_requests);

  if (max_running_requests == 0 && transport_result == sdk::common::ExportResult::kSuccess)
  {
    return *session_result;
  }

  return transport_result;
}

bool OtlpHttpClient::ForceFlush(std::chrono::microseconds timeout) noexcept
{
  return transport_->ForceFlush(timeout);
}

bool OtlpHttpClient::Shutdown(std::chrono::microseconds timeout) noexcept
{
  return transport_->Shutdown(timeout);
}

void OtlpHttpClient::ReleaseSession(
    const opentelemetry::ext::http::client::Session &session) noexcept
{
  transport_->ReleaseSession(session);
}

const OtlpHttpClientOptions &OtlpHttpClient::GetOptions() const noexcept
{
  return transport_->GetOptions();
}

bool OtlpHttpClient::IsShutdown() const noexcept
{
  return transport_->IsShutdown();
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
