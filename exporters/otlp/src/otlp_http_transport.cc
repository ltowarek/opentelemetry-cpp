// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/detail/otlp_http_transport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/otlp_http_client.h"
#include "opentelemetry/ext/http/client/detail/default_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/ext/http/common/url_parser.h"
#include "opentelemetry/nostd/function_ref.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/nostd/variant.h"
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

namespace
{

class ResponseHandler : public http_client::EventHandler
{
public:
  /**
   * Creates a response handler, that by default doesn't display to console.
   * The response body is handed to the callback as it arrived; reading it is
   * the caller's business.
   */
  ResponseHandler(OtlpHttpTransport::CompletionCallback &&callback, bool console_debug = false)
      : result_callback_{std::move(callback)}, console_debug_{console_debug}
  {}

  std::string BuildResponseLogMessage(http_client::Response &response,
                                      const http_client::Body &body) noexcept
  {
    std::stringstream ss;
    ss << "Status:" << response.GetStatusCode() << ", Header:";
    response.ForEachHeader([&ss](opentelemetry::nostd::string_view header_name,
                                 opentelemetry::nostd::string_view header_value) {
      ss << "\t" << header_name << ": " << header_value << ",";
      return true;
    });
    ss << "Body:";
    ss.write(reinterpret_cast<const char *>(body.data()),
             static_cast<std::streamsize>(body.size()));

    return ss.str();
  }

  /**
   * Automatically called when the response is received, store the body into a string and notify any
   * threads blocked on this result
   */
  void OnResponse(http_client::Response &response) noexcept override
  {
    sdk::common::ExportResult result = sdk::common::ExportResult::kSuccess;
    std::string log_message;
    // Lock the private members so they can't be read while being modified
    {
      std::unique_lock<std::mutex> lk(mutex_);

      // Store the body of the request
      body_ = response.GetBody();

      if (!(response.GetStatusCode() >= 200 && response.GetStatusCode() <= 299))
      {
        log_message = BuildResponseLogMessage(response, body_);

        OTEL_INTERNAL_LOG_ERROR("[OTLP HTTP Client] Export failed, " << log_message);
        result = sdk::common::ExportResult::kFailure;
      }
      else if (console_debug_)
      {
        if (log_message.empty())
        {
          log_message = BuildResponseLogMessage(response, body_);
        }
      }
    }

    if (console_debug_ && result == sdk::common::ExportResult::kSuccess)
    {
      OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Export success, " << log_message);
    }

    {
      bool expected = false;
      if (stopping_.compare_exchange_strong(expected, true, std::memory_order_release))
      {
        Unbind(result);
      }
    }
  }

  /**
   * Returns the body of the response
   */
  std::string GetResponseBody()
  {
    // Lock so that body_ can't be written to while returning it
    std::unique_lock<std::mutex> lk(mutex_);
    return std::string(body_.begin(), body_.end());
  }

  // Callback method when an http event occurs
  void OnEvent(http_client::SessionState state,
               opentelemetry::nostd::string_view reason) noexcept override
  {
    // need to modify stopping_ under lock before calling callback
    //
    // Every state the client can end a request on. A state missing from here leaves the caller
    // waiting for a callback that never comes. The exchange below decides ordering, so a state
    // arriving after a response does not report a second time.
    bool need_stop = false;
    switch (state)
    {
      case http_client::SessionState::CreateFailed:
      case http_client::SessionState::ConnectFailed:
      case http_client::SessionState::SendFailed:
      case http_client::SessionState::SSLHandshakeFailed:
      case http_client::SessionState::TimedOut:
      case http_client::SessionState::NetworkError:
      case http_client::SessionState::Destroyed:
      case http_client::SessionState::ReadError:
      case http_client::SessionState::WriteError:
      case http_client::SessionState::Cancelled: {
        need_stop = true;
      }
      break;

      default:
        break;
    }

    // If any failure event occurs, release the condition variable to unblock main thread
    switch (state)
    {
      case http_client::SessionState::CreateFailed: {
        std::stringstream error_message;
        error_message << "[OTLP HTTP Client] Session state: session create failed.";
        if (!reason.empty())
        {
          error_message.write(reason.data(), static_cast<std::streamsize>(reason.size()));
        }
        OTEL_INTERNAL_LOG_ERROR(error_message.str());
      }
      break;

      case http_client::SessionState::Created:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: session created");
        }
        break;

      case http_client::SessionState::Destroyed:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: session destroyed");
        }
        break;

      case http_client::SessionState::Connecting:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: connecting to peer");
        }
        break;

      case http_client::SessionState::ConnectFailed: {
        std::stringstream error_message;
        error_message << "[OTLP HTTP Client] Session state: connection failed.";
        if (!reason.empty())
        {
          error_message.write(reason.data(), static_cast<std::streamsize>(reason.size()));
        }
        OTEL_INTERNAL_LOG_ERROR(error_message.str());
      }
      break;

      case http_client::SessionState::Connected:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: connected");
        }
        break;

      case http_client::SessionState::Sending:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: sending request");
        }
        break;

      case http_client::SessionState::SendFailed: {
        std::stringstream error_message;
        error_message << "[OTLP HTTP Client] Session state: request send failed.";
        if (!reason.empty())
        {
          error_message.write(reason.data(), static_cast<std::streamsize>(reason.size()));
        }
        OTEL_INTERNAL_LOG_ERROR(error_message.str());
      }
      break;

      case http_client::SessionState::Response:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: response received");
        }
        break;

      case http_client::SessionState::SSLHandshakeFailed: {
        std::stringstream error_message;
        error_message << "[OTLP HTTP Client] Session state: SSL handshake failed.";
        if (!reason.empty())
        {
          error_message.write(reason.data(), static_cast<std::streamsize>(reason.size()));
        }
        OTEL_INTERNAL_LOG_ERROR(error_message.str());
      }
      break;

      case http_client::SessionState::TimedOut: {
        std::stringstream error_message;
        error_message << "[OTLP HTTP Client] Session state: request time out.";
        if (!reason.empty())
        {
          error_message.write(reason.data(), static_cast<std::streamsize>(reason.size()));
        }
        OTEL_INTERNAL_LOG_ERROR(error_message.str());
      }
      break;

      case http_client::SessionState::NetworkError: {
        std::stringstream error_message;
        error_message << "[OTLP HTTP Client] Session state: network error.";
        if (!reason.empty())
        {
          error_message.write(reason.data(), static_cast<std::streamsize>(reason.size()));
        }
        OTEL_INTERNAL_LOG_ERROR(error_message.str());
      }
      break;

      case http_client::SessionState::ReadError:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: error reading response");
        }
        break;

      case http_client::SessionState::WriteError:
        if (console_debug_)
        {
          OTEL_INTERNAL_LOG_DEBUG("[OTLP HTTP Client] Session state: error writing request");
        }
        break;

      case http_client::SessionState::Cancelled: {
        std::stringstream error_message;
        error_message << "[OTLP HTTP Client] Session state: (manually) cancelled.";
        if (!reason.empty())
        {
          error_message.write(reason.data(), static_cast<std::streamsize>(reason.size()));
        }
        OTEL_INTERNAL_LOG_ERROR(error_message.str());
      }
      break;

      default:
        break;
    }

    if (need_stop)
    {
      bool expected = false;
      if (stopping_.compare_exchange_strong(expected, true, std::memory_order_release))
      {
        Unbind(sdk::common::ExportResult::kFailure);
      }
    }
  }

  void Unbind(sdk::common::ExportResult result)
  {
    // ReleaseSession may destroy this object, so we need to move owner and session into stack
    // first.
    OtlpHttpTransport *owner                                 = owner_;
    const opentelemetry::ext::http::client::Session *session = session_;
    auto callback                                            = std::move(result_callback_);

    owner_   = nullptr;
    session_ = nullptr;

    // Run the callback before releasing the session, so whatever its closure owns outlives
    // the response the callback is reading.
    if (callback)
    {
      callback(result, body_);
    }

    if (nullptr != owner && nullptr != session)
    {
      // Release the session at last
      owner->ReleaseSession(*session);
    }
  }

  void Bind(OtlpHttpTransport *owner,
            const opentelemetry::ext::http::client::Session &session) noexcept
  {
    session_ = &session;
    owner_   = owner;
  }

private:
  // Define a mutex to keep thread safety
  std::mutex mutex_;

  // Track the owner and the binded session
  OtlpHttpTransport *owner_                                 = nullptr;
  const opentelemetry::ext::http::client::Session *session_ = nullptr;

  // Whether notify has been called
  std::atomic<bool> stopping_{false};

  // The response body, as it arrived
  http_client::Body body_;

  // Result callback when in async mode
  OtlpHttpTransport::CompletionCallback result_callback_;

  // Whether to print the results from the callback
  bool console_debug_ = false;
};

}  // namespace

OtlpHttpTransport::OtlpHttpTransport(OtlpHttpClientOptions &&options)
    : OtlpHttpTransport(std::move(options),
                        ext::http::client::detail::GetDefaultHttpClientFactory())
{}

OtlpHttpTransport::OtlpHttpTransport(
    OtlpHttpClientOptions &&options,
    const std::shared_ptr<ext::http::client::HttpClientFactory> &factory)
    : OtlpHttpTransport(std::move(options), factory->Create(options.thread_instrumentation))
{}

OtlpHttpTransport::OtlpHttpTransport(OtlpHttpClientOptions &&options,
                                     std::shared_ptr<ext::http::client::HttpClient> http_client)
    : is_shutdown_(false),
      options_(std::move(options)),
      http_client_(std::move(http_client)),
      start_session_counter_(0),
      finished_session_counter_(0)
{
  http_client_->SetMaxSessionsPerConnection(options_.max_requests_per_connection);
}

// ----------------------------- HTTP Transport methods ------------------------------
sdk::common::ExportResult OtlpHttpTransport::Export(http_client::Body &&body,
                                                    nostd::string_view content_type,
                                                    CompletionCallback &&result_callback,
                                                    std::size_t max_running_requests) noexcept
{
  // Only a sync export (max_running_requests == 0) needs to capture the result of the callback.
  std::shared_ptr<sdk::common::ExportResult> session_result;
  auto callback = std::move(result_callback);
  if (max_running_requests == 0)
  {
    session_result =
        std::make_shared<sdk::common::ExportResult>(sdk::common::ExportResult::kSuccess);
    callback = [session_result, cb = std::move(callback)](sdk::common::ExportResult result,
                                                          const http_client::Body &response_body) {
      *session_result = result;
      return cb(result, response_body);
    };
  }

  auto session = createSession(std::move(body), content_type, std::move(callback));
  if (auto *result = opentelemetry::nostd::get_if<sdk::common::ExportResult>(&session))
  {
    return *result;
  }

  addSession(std::move(*opentelemetry::nostd::get_if<HttpSessionData>(&session)));

  // Wait for the response to be received
  if (options_.console_debug)
  {
    OTEL_INTERNAL_LOG_DEBUG(
        "[OTLP HTTP Client] Waiting for response from "
        << options_.url << " (timeout = "
        << std::chrono::duration_cast<std::chrono::milliseconds>(options_.timeout).count()
        << " milliseconds)");
  }

  // Wait for any session to finish if there are to many sessions
  std::unique_lock<std::mutex> lock(session_waker_lock_);
  bool wait_successful =
      session_waker_.wait_for(lock, options_.timeout, [this, max_running_requests] {
        std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};
        return running_sessions_.size() <= max_running_requests;
      });

  cleanupGCSessions();

  if (!wait_successful)
  {
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  // Only sync export (max_running_requests == 0) have final result to return here.
  if (max_running_requests == 0)
  {
    return *session_result;
  }

  return opentelemetry::sdk::common::ExportResult::kSuccess;
}

opentelemetry::nostd::variant<opentelemetry::sdk::common::ExportResult,
                              OtlpHttpTransport::HttpSessionData>
OtlpHttpTransport::createSession(http_client::Body &&body,
                                 nostd::string_view content_type,
                                 CompletionCallback &&result_callback) noexcept
{
  // An empty body stands in for the response the caller never received, on every path that
  // fails before one exists.
  static const http_client::Body kNoResponseBody;

  // Parse uri and store it to cache
  if (http_uri_.empty())
  {
    const auto parse_url = opentelemetry::ext::http::common::UrlParser(options_.url);
    if (!parse_url.success_)
    {
      std::string error_message = "[OTLP HTTP Client] Export failed, invalid url: " + options_.url;
      if (options_.console_debug)
      {
        std::cerr << error_message << '\n';
      }
      OTEL_INTERNAL_LOG_ERROR(error_message);

      const auto result = opentelemetry::sdk::common::ExportResult::kFailure;
      result_callback(result, kNoResponseBody);
      return result;
    }

    if (!parse_url.path_.empty() && parse_url.path_[0] == '/')
    {
      http_uri_ = parse_url.path_.substr(1);
    }
    else
    {
      http_uri_ = parse_url.path_;
    }
  }

  // Send the request
  std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};
  // Return failure if this exporter has been shutdown
  if (IsShutdown())
  {
    const char *error_message = "[OTLP HTTP Client] Export failed, exporter is shutdown";
    if (options_.console_debug)
    {
      std::cerr << error_message << '\n';
    }
    OTEL_INTERNAL_LOG_ERROR(error_message);

    const auto result = opentelemetry::sdk::common::ExportResult::kFailure;
    result_callback(result, kNoResponseBody);
    return result;
  }

  auto session = http_client_->CreateSession(options_.url);
  auto request = session->CreateRequest();

  for (auto &header : options_.http_headers)
  {
    request->AddHeader(header.first,
                       opentelemetry::ext::http::common::UrlDecoder::Decode(header.second));
  }
  request->SetUri(http_uri_);
  request->SetSslOptions(options_.ssl_options);
  request->SetTimeoutMs(std::chrono::duration_cast<std::chrono::milliseconds>(options_.timeout));
  request->SetMethod(http_client::Method::Post);
  request->SetBody(body);
  request->ReplaceHeader("Content-Type", content_type);
  request->ReplaceHeader("User-Agent", options_.user_agent);
  request->EnableLogging(options_.console_debug);
  request->SetRetryPolicy(options_.retry_policy);

  if (options_.compression == "gzip")
  {
    request->SetCompression(opentelemetry::ext::http::client::Compression::kGzip);
  }

  return HttpSessionData{
      std::move(session),
      std::shared_ptr<opentelemetry::ext::http::client::EventHandler>{
          std::make_shared<ResponseHandler>(std::move(result_callback), options_.console_debug)}};
}

OtlpHttpTransport::~OtlpHttpTransport()
{
  if (!IsShutdown())
  {
    Shutdown();
  }

  // Wait for all the sessions to finish
  std::unique_lock<std::mutex> lock(session_waker_lock_);
  while (true)
  {
    {
      std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};
      if (running_sessions_.empty())
      {
        break;
      }
    }
    // When changes of running_sessions_ and notify_one/notify_all happen between predicate
    // checking and waiting, we should not wait forever. We should cleanup gc sessions here as soon
    // as possible to call FinishSession() and cleanup resources.
    if (std::cv_status::timeout == session_waker_.wait_for(lock, options_.timeout))
    {
      cleanupGCSessions();
    }
  }

  // And then remove all session datas
  while (cleanupGCSessions())
    ;
}

bool OtlpHttpTransport::ForceFlush(std::chrono::microseconds timeout) noexcept
{
  // ASAN will report chrono: runtime error: signed integer overflow: A + B cannot be represented
  //   in type 'long int' here. So we reset timeout to meet signed long int limit here.
  timeout = opentelemetry::common::DurationUtil::AdjustWaitForTimeout(
      timeout, std::chrono::microseconds::zero());

  // Wait for all the sessions to finish
  std::unique_lock<std::mutex> lock(session_waker_lock_);

  std::chrono::steady_clock::duration timeout_steady =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(timeout);
  if (timeout_steady <= std::chrono::steady_clock::duration::zero())
  {
    timeout_steady = (std::chrono::steady_clock::duration::max)();
  }

  size_t wait_counter = start_session_counter_.load(std::memory_order_acquire);

  while (timeout_steady > std::chrono::steady_clock::duration::zero())
  {
    {
      std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};
      if (running_sessions_.empty())
      {
        break;
      }
    }
    // When changes of running_sessions_ and notify_one/notify_all happen between predicate
    // checking and waiting, we should not wait forever.We should cleanup gc sessions here as soon
    // as possible to call FinishSession() and cleanup resources.
    const std::chrono::steady_clock::duration wait_interval = (std::min)(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(options_.timeout),
        timeout_steady);

    std::chrono::steady_clock::time_point start_timepoint = std::chrono::steady_clock::now();
    if (std::cv_status::timeout == session_waker_.wait_for(lock, wait_interval))
    {
      cleanupGCSessions();
    }
    else if (finished_session_counter_.load(std::memory_order_acquire) >= wait_counter)
    {
      break;
    }

    timeout_steady -= std::chrono::steady_clock::now() - start_timepoint;
  }

  // What the wait was for, rather than what is left of the deadline. A session that finishes
  // between the last check and the last wait takes its notification with it, and the flush it was
  // holding up has happened.
  return finished_session_counter_.load(std::memory_order_acquire) >= wait_counter;
}

bool OtlpHttpTransport::Shutdown(std::chrono::microseconds timeout) noexcept
{
  is_shutdown_.store(true, std::memory_order_release);

  bool force_flush_result = ForceFlush(timeout);

  {
    std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};

    // Shutdown the session manager
    http_client_->CancelAllSessions();
    http_client_->FinishAllSessions();
  }

  // Wait util all sessions are canceled.
  while (cleanupGCSessions())
  {
    ForceFlush(std::chrono::milliseconds{1});
  }
  return force_flush_result;
}

void OtlpHttpTransport::ReleaseSession(
    const opentelemetry::ext::http::client::Session &session) noexcept
{
  bool has_session = false;

  std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};

  auto session_iter = running_sessions_.find(&session);
  if (session_iter != running_sessions_.end())
  {
    // Move session and handle into gc list, and they will be destroyed later
    gc_sessions_.emplace_back(std::move(session_iter->second));
    running_sessions_.erase(session_iter);

    finished_session_counter_.fetch_add(1, std::memory_order_release);
    has_session = true;
  }

  // Call session_waker_.notify_all() with session_manager_lock_ locked to keep session_waker_
  // available when destroying OtlpHttpTransport
  if (has_session)
  {
    session_waker_.notify_all();
  }
}

OtlpHttpTransport::HttpSessionData::HttpSessionData() noexcept = default;

OtlpHttpTransport::HttpSessionData::HttpSessionData(
    std::shared_ptr<opentelemetry::ext::http::client::Session> &&input_session,
    std::shared_ptr<opentelemetry::ext::http::client::EventHandler> &&input_handle) noexcept
    : session(std::move(input_session)), event_handle(std::move(input_handle))
{}

OtlpHttpTransport::HttpSessionData::~HttpSessionData()                           = default;
OtlpHttpTransport::HttpSessionData::HttpSessionData(HttpSessionData &&) noexcept = default;
OtlpHttpTransport::HttpSessionData &OtlpHttpTransport::HttpSessionData::operator=(
    HttpSessionData &&) noexcept = default;

void OtlpHttpTransport::addSession(HttpSessionData &&session_data) noexcept
{
  if (!session_data.session || !session_data.event_handle)
  {
    return;
  }

  std::shared_ptr<opentelemetry::ext::http::client::Session> session = session_data.session;
  std::shared_ptr<opentelemetry::ext::http::client::EventHandler> handle =
      session_data.event_handle;
  {
    std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};
    static_cast<ResponseHandler *>(handle.get())->Bind(this, *session);

    HttpSessionData &store_session_data = running_sessions_[session.get()];
    store_session_data                  = std::move(session_data);
  }

  start_session_counter_.fetch_add(1, std::memory_order_release);
  // Send request after the session is added
  session->SendRequest(handle);
}

bool OtlpHttpTransport::cleanupGCSessions() noexcept
{
  std::lock_guard<std::recursive_mutex> guard{session_manager_lock_};
  std::list<HttpSessionData> gc_sessions;
  gc_sessions_.swap(gc_sessions);

  for (auto &session_data : gc_sessions)
  {
    // FinishSession must be called with same thread and before the session is destroyed
    if (session_data.session)
    {
      session_data.session->FinishSession();
    }
  }

  return !gc_sessions_.empty();
}

bool OtlpHttpTransport::IsShutdown() const noexcept
{
  return is_shutdown_.load(std::memory_order_acquire);
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
