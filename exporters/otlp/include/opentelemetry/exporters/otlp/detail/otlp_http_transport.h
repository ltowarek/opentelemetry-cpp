// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "opentelemetry/exporters/otlp/otlp_http_client.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
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
 * Posts a finished request body to an OTLP endpoint and hands the response
 * bytes back, without referencing protobuf.
 *
 * This is everything OtlpHttpClient does apart from producing the request
 * body and parsing the response into a message: the session pool and its
 * garbage collection, the concurrency counter, the cached parsed URI, retry
 * policy, compression, ForceFlush and Shutdown. OtlpHttpClient is this type
 * plus a protobuf message on either end, and delegates rather than repeating
 * any of it, so the two paths cannot drift.
 *
 * It lives in a build target that does not link protobuf, which is what lets
 * the OTLP/JSON exporters shed the message runtime rather than merely avoiding
 * it in their own source.
 *
 * Not public API. The header is under detail/ because only in-repo exporters
 * are meant to use it, but unlike most detail/ headers it is consumed across
 * target boundaries.
 */
class OtlpHttpTransport
{
public:
  /**
   * Invoked once per export with the outcome and the raw response body, which
   * is empty unless the endpoint sent one.
   *
   * The result reflects the HTTP exchange alone. A caller that reads the body
   * and finds it unusable -- a partial-success payload it cannot parse, say --
   * owns downgrading the result it reports to its own caller, because Export
   * cannot see what the callback decided.
   */
  using CompletionCallback =
      std::function<bool(sdk::common::ExportResult, const ext::http::client::Body &)>;

  /**
   * Create an OtlpHttpTransport using the given options.
   * Uses the default HTTP client factory (curl when available).
   */
  explicit OtlpHttpTransport(OtlpHttpClientOptions &&options);

  /**
   * Create an OtlpHttpTransport using the given options and HTTP client factory.
   * @param options the Otlp http client options to be used for exporting
   * @param factory the HTTP client factory used to create the underlying HTTP client
   */
  OtlpHttpTransport(OtlpHttpClientOptions &&options,
                    const std::shared_ptr<ext::http::client::HttpClientFactory> &factory);

  /**
   * Create an OtlpHttpTransport using the specified http client.
   * @param options the Otlp http client options to be used for exporting
   * @param http_client the http client to be used for exporting
   */
  OtlpHttpTransport(OtlpHttpClientOptions &&options,
                    std::shared_ptr<ext::http::client::HttpClient> http_client);

  ~OtlpHttpTransport();
  OtlpHttpTransport(const OtlpHttpTransport &)            = delete;
  OtlpHttpTransport &operator=(const OtlpHttpTransport &) = delete;
  OtlpHttpTransport(OtlpHttpTransport &&)                 = delete;
  OtlpHttpTransport &operator=(OtlpHttpTransport &&)      = delete;

  /**
   * Post a request body. Synchronous when max_running_requests is 0,
   * asynchronous otherwise.
   *
   * @param body the finished request body to post
   * @param content_type the value to send as the Content-Type header
   * @param result_callback callback to call when the exporting is done
   * @param max_running_requests wait for at most max_running_requests running requests
   * @return the export result; kSuccess for asynchronous exports (final result via
   * result_callback). For a synchronous export this is the result the callback was invoked
   * with, before the callback ran -- a caller whose callback can downgrade the result must
   * capture its own.
   */
  sdk::common::ExportResult Export(ext::http::client::Body &&body,
                                   nostd::string_view content_type,
                                   CompletionCallback &&result_callback,
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
   * Get options of current OTLP http transport.
   * @return options of current OTLP http transport.
   */
  inline const OtlpHttpClientOptions &GetOptions() const noexcept { return options_; }

  /**
   * Get if this OTLP http transport is shutdown.
   * @return return true after Shutdown is called.
   */
  bool IsShutdown() const noexcept;

private:
  struct HttpSessionData
  {
    std::shared_ptr<opentelemetry::ext::http::client::Session> session;
    std::shared_ptr<opentelemetry::ext::http::client::EventHandler> event_handle;

    HttpSessionData() noexcept;
    HttpSessionData(
        std::shared_ptr<opentelemetry::ext::http::client::Session> &&input_session,
        std::shared_ptr<opentelemetry::ext::http::client::EventHandler> &&input_handle) noexcept;

    ~HttpSessionData();
    HttpSessionData(HttpSessionData &&) noexcept;
    HttpSessionData &operator=(HttpSessionData &&) noexcept;
    HttpSessionData(const HttpSessionData &)            = delete;
    HttpSessionData &operator=(const HttpSessionData &) = delete;
  };

  /**
   * @brief Create a Session object carrying the request, or return an error result.
   *
   * @param body the finished request body to post
   * @param content_type the value to send as the Content-Type header
   * @param result_callback callback for the export result; receives the raw response body
   */
  nostd::variant<sdk::common::ExportResult, HttpSessionData> createSession(
      ext::http::client::Body &&body,
      nostd::string_view content_type,
      CompletionCallback &&result_callback) noexcept;

  /**
   * Add http session and hold it's lifetime.
   * @param session_data the session to add
   */
  void addSession(HttpSessionData &&session_data) noexcept;

  /**
   * @brief Real delete all sessions and event handles.
   * @note This function is called in the same thread where we create sessions and handles
   *
   * @return return true if there are more sessions to delete
   */
  bool cleanupGCSessions() noexcept;

  // Stores if this HTTP transport had its Shutdown() method called
  std::atomic<bool> is_shutdown_;

  // The configuration options associated with this HTTP transport.
  const OtlpHttpClientOptions options_;

  // Object that stores the HTTP sessions that have been created
  std::shared_ptr<ext::http::client::HttpClient> http_client_;

  // Cached parsed URI
  std::string http_uri_;

  // Running sessions and event handles
  std::unordered_map<const opentelemetry::ext::http::client::Session *, HttpSessionData>
      running_sessions_;
  // Sessions and event handles that are waiting to be deleted
  std::list<HttpSessionData> gc_sessions_;
  // Lock for running_sessions_, gc_sessions_ and http_client_
  std::recursive_mutex session_manager_lock_;
  // Condition variable and mutex to control the concurrency count of running sessions
  std::mutex session_waker_lock_;
  std::condition_variable session_waker_;
  std::atomic<size_t> start_session_counter_;
  std::atomic<size_t> finished_session_counter_;
};

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
