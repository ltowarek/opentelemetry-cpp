// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "opentelemetry/exporters/otlp/otlp_environment.h"
#include "opentelemetry/exporters/otlp/otlp_http.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/thread_instrumentation.h"
#include "opentelemetry/version.h"

// forward declare google::protobuf::Message and google::protobuf::Arena
namespace google
{
namespace protobuf
{
class Arena;
class Message;
}  // namespace protobuf
}  // namespace google

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{
class OtlpHttpTransport;
}  // namespace detail

// The default URL path to post metric data.
constexpr char kDefaultMetricsPath[] = "/v1/metrics";
// The HTTP header "Content-Type"
constexpr char kHttpJsonContentType[]   = "application/json";
constexpr char kHttpBinaryContentType[] = "application/x-protobuf";

/**
 * Struct to hold OTLP HTTP client options.
 */
struct OtlpHttpClientOptions
{
  std::string url;

  /** SSL options. */
  ext::http::client::HttpSslOptions ssl_options;

  // By default, post binary data
  HttpRequestContentType content_type = HttpRequestContentType::kBinary;

  // If convert bytes into hex. By default, we will convert all bytes but id into base64
  // This option is ignored if content_type is not kJson
  JsonBytesMappingKind json_bytes_mapping = JsonBytesMappingKind::kHexId;

  // By default, do not compress data
  std::string compression = "none";

  // If using the json name of protobuf field to set the key of json. By default, we will use the
  // field name just like proto files.
  bool use_json_name = false;

  // Whether to print the status of the HTTP client in the console
  bool console_debug = false;

  std::chrono::system_clock::duration timeout{};

  // Additional HTTP headers
  OtlpHeaders http_headers;

  // Retry policy for select failure codes
  ext::http::client::RetryPolicy retry_policy;

  // Concurrent requests
  std::size_t max_concurrent_requests = 64;

  // Requests per connections
  std::size_t max_requests_per_connection = 8;

  // User agent
  std::string user_agent;

  std::shared_ptr<sdk::common::ThreadInstrumentation> thread_instrumentation =
      std::shared_ptr<sdk::common::ThreadInstrumentation>(nullptr);

  std::shared_ptr<JsonWriterFactory> json_writer_factory;

  inline OtlpHttpClientOptions(
      nostd::string_view input_url,
      bool input_ssl_insecure_skip_verify,
      nostd::string_view input_ssl_ca_cert_path,
      nostd::string_view input_ssl_ca_cert_string,
      nostd::string_view input_ssl_client_key_path,
      nostd::string_view input_ssl_client_key_string,
      nostd::string_view input_ssl_client_cert_path,
      nostd::string_view input_ssl_client_cert_string,
      nostd::string_view input_ssl_min_tls,
      nostd::string_view input_ssl_max_tls,
      nostd::string_view input_ssl_cipher,
      nostd::string_view input_ssl_cipher_suite,
      HttpRequestContentType input_content_type,
      JsonBytesMappingKind input_json_bytes_mapping,
      nostd::string_view input_compression,
      bool input_use_json_name,
      bool input_console_debug,
      std::chrono::system_clock::duration input_timeout,
      const OtlpHeaders &input_http_headers,
      std::uint32_t input_retry_policy_max_attempts,
      std::chrono::duration<float> input_retry_policy_initial_backoff,
      std::chrono::duration<float> input_retry_policy_max_backoff,
      float input_retry_policy_backoff_multiplier,
      const std::shared_ptr<sdk::common::ThreadInstrumentation> &input_thread_instrumentation,
      std::size_t input_concurrent_sessions         = 64,
      std::size_t input_max_requests_per_connection = 8,
      nostd::string_view input_user_agent           = GetOtlpDefaultUserAgent())
      : url(input_url),
        ssl_options(input_url,
                    input_ssl_insecure_skip_verify,
                    input_ssl_ca_cert_path,
                    input_ssl_ca_cert_string,
                    input_ssl_client_key_path,
                    input_ssl_client_key_string,
                    input_ssl_client_cert_path,
                    input_ssl_client_cert_string,
                    input_ssl_min_tls,
                    input_ssl_max_tls,
                    input_ssl_cipher,
                    input_ssl_cipher_suite),
        content_type(input_content_type),
        json_bytes_mapping(input_json_bytes_mapping),
        compression(input_compression),
        use_json_name(input_use_json_name),
        console_debug(input_console_debug),
        timeout(input_timeout),
        http_headers(input_http_headers),
        retry_policy{input_retry_policy_max_attempts, input_retry_policy_initial_backoff,
                     input_retry_policy_max_backoff, input_retry_policy_backoff_multiplier},
        max_concurrent_requests(input_concurrent_sessions),
        max_requests_per_connection(input_max_requests_per_connection),
        user_agent(input_user_agent),
        thread_instrumentation(input_thread_instrumentation)
  {}
};

/**
 * The OTLP HTTP client exports span data in OpenTelemetry Protocol (OTLP) format.
 */
class OtlpHttpClient
{
public:
  /**
   * Create an OtlpHttpClient using the given options.
   * Uses the default HTTP client factory (curl when available).
   */
  explicit OtlpHttpClient(OtlpHttpClientOptions &&options);

  /**
   * Create an OtlpHttpClient using the given options and HTTP client factory.
   * @param options the Otlp http client options to be used for exporting
   * @param factory the HTTP client factory used to create the underlying HTTP client
   */
  OtlpHttpClient(OtlpHttpClientOptions &&options,
                 const std::shared_ptr<ext::http::client::HttpClientFactory> &factory);

  /**
   * Create an OtlpHttpClient using the specified http client.
   * @param options the Otlp http client options to be used for exporting
   * @param http_client the http client to be used for exporting
   */
  OtlpHttpClient(OtlpHttpClientOptions &&options,
                 std::shared_ptr<ext::http::client::HttpClient> http_client);

  ~OtlpHttpClient();
  OtlpHttpClient(const OtlpHttpClient &)            = delete;
  OtlpHttpClient &operator=(const OtlpHttpClient &) = delete;
  OtlpHttpClient(OtlpHttpClient &&)                 = delete;
  OtlpHttpClient &operator=(OtlpHttpClient &&)      = delete;

  /**
   * Export message with typed response. Synchronous when max_running_requests is 0,
   * asynchronous otherwise.
   * @param message message to export, it should be ExportTraceServiceRequest,
   * ExportMetricsServiceRequest or ExportLogsServiceRequest
   * @param arena protobuf arena that owns response
   * @param response the parsed body is written here on 2xx
   * @param result_callback callback to call when the exporting is done
   * @param max_running_requests wait for at most max_running_requests running requests
   * @return the export result; kSuccess for asynchronous exports (final result via
   * result_callback)
   */
  sdk::common::ExportResult Export(
      const google::protobuf::Message &message,
      std::unique_ptr<google::protobuf::Arena> &&arena,
      google::protobuf::Message *response,
      std::function<bool(opentelemetry::sdk::common::ExportResult, google::protobuf::Message *)>
          &&result_callback,
      std::size_t max_running_requests) noexcept;

  /**
   * Force flush the HTTP client.
   */
  bool ForceFlush(std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept;

  /**
   * Shut down the HTTP client.
   * @param timeout an optional timeout, the default timeout of 0 means that no
   * timeout is applied.
   * @return return the status of this operation
   */
  bool Shutdown(std::chrono::microseconds timeout = std::chrono::microseconds(0)) noexcept;

  /**
   * @brief Release the lifetime of specify session.
   *
   * @param session the session to release
   */
  void ReleaseSession(const opentelemetry::ext::http::client::Session &session) noexcept;

  /**
   * Get options of current OTLP http client.
   * @return options of current OTLP http client.
   */
  const OtlpHttpClientOptions &GetOptions() const noexcept;

  /**
   * Get if this OTLP http client is shutdown.
   * @return return true after Shutdown is called.
   */
  bool IsShutdown() const noexcept;

private:
  // The transport that owns the sessions, the concurrency control and the shutdown
  // handling. Held by pointer so that the public header does not have to include the
  // detail one, which includes this header in turn for the options.
  // Resolved from options_.json_writer_factory, or the default backend.
  std::shared_ptr<JsonWriterFactory> json_writer_factory_;
  std::unique_ptr<detail::OtlpHttpTransport> transport_;

};
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
