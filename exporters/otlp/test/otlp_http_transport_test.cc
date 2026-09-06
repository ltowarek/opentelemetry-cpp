// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Proves the OTLP/HTTP send path is usable without protobuf.
//
// The exporter tests already cover what the transport does with a request --
// they assert exact serialized bodies through the same no-send client -- but
// every caller reaching that seam is protobuf-typed, so none of them can show
// that a protobuf-free payload gets through. This one links the transport and
// the no-send client and nothing else, so it fails to link, loudly, if the
// build target split ever puts protobuf back underneath.
//
// The assertions are at the same altitude as the exporter tests: the request
// body that reached the client, the content type it was sent with, and the
// response bytes handed back. Sessions, handlers and callback ordering are
// implementation detail.

#include "opentelemetry/exporters/otlp/detail/otlp_http_transport.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/otlp_http_client.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/thread_instrumentation.h"
#include "opentelemetry/test_common/ext/http/client/nosend/http_client_factory_nosend.h"
#include "opentelemetry/test_common/ext/http/client/nosend/http_client_nosend.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{
namespace
{

namespace http_client = opentelemetry::ext::http::client;

OtlpHttpClientOptions MakeOptions()
{
  const std::shared_ptr<sdk::common::ThreadInstrumentation> not_instrumented;
  OtlpHttpClientOptions options(
      "http://localhost:4318/v1/traces", false, /* ssl_insecure_skip_verify */
      "",                                       /* ssl_ca_cert_path */
      "",                                       /* ssl_ca_cert_string */
      "",                                       /* ssl_client_key_path */
      "",                                       /* ssl_client_key_string */
      "",                                       /* ssl_client_cert_path */
      "",                                       /* ssl_client_cert_string */
      "",                                       /* ssl_min_tls */
      "",                                       /* ssl_max_tls */
      "",                                       /* ssl_cipher */
      "",                                       /* ssl_cipher_suite */
      HttpRequestContentType::kJson, JsonBytesMappingKind::kHexId, "none", /* compression */
      false,                                                               /* use_json_name */
      false,                                                               /* console_debug */
      std::chrono::system_clock::duration::zero(), OtlpHeaders{}, 0U,      /* retry max attempts */
      std::chrono::duration<float>::zero(), std::chrono::duration<float>::zero(), 0.0f,
      not_instrumented);
  // A synchronous export, which is what a platform whose transport cannot spawn threads gets.
  options.max_concurrent_requests = 0;
  return options;
}

http_client::Body BodyOf(const std::string &text)
{
  return http_client::Body(text.begin(), text.end());
}

std::string TextOf(const http_client::Body &body)
{
  return std::string(body.begin(), body.end());
}

// Drives one export through a no-send client and reports what the request
// carried and what the completion callback saw.
struct ExportOutcome
{
  std::string request_body;
  std::string request_content_type;
  std::string response_body;
  sdk::common::ExportResult callback_result = sdk::common::ExportResult::kFailure;
  bool callback_ran                         = false;
  sdk::common::ExportResult export_result   = sdk::common::ExportResult::kFailure;
};

ExportOutcome RunExport(const std::string &request_text,
                        const std::string &content_type,
                        const std::string &response_text,
                        http_client::StatusCode status_code = http_client::nosend::Http_Ok)
{
  ExportOutcome outcome;

  auto client         = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  auto no_send_client = std::static_pointer_cast<http_client::nosend::HttpClient>(client);
  OtlpHttpTransport transport(MakeOptions(), client);

  auto mock_session =
      std::static_pointer_cast<http_client::nosend::Session>(no_send_client->session_);
  EXPECT_CALL(*mock_session, SendRequest)
      .WillOnce([&](const std::shared_ptr<http_client::EventHandler> &handler) {
        const auto &request  = *mock_session->GetRequest();
        outcome.request_body = TextOf(request.body_);
        auto header          = request.headers_.find("Content-Type");
        if (header != request.headers_.end())
        {
          outcome.request_content_type = header->second;
        }

        http_client::nosend::Response response;
        response.status_code_ = status_code;
        response.body_        = BodyOf(response_text);
        response.Finish(*handler);
      });

  outcome.export_result = transport.Export(
      BodyOf(request_text), content_type,
      [&outcome](sdk::common::ExportResult result, const http_client::Body &body) {
        outcome.callback_ran    = true;
        outcome.callback_result = result;
        outcome.response_body   = TextOf(body);
        return true;
      },
      0 /* max_running_requests, i.e. synchronous */);

  return outcome;
}

TEST(OtlpHttpTransportTest, PostsTheBodyItWasGiven)
{
  const std::string request = R"({"resourceSpans":[]})";

  const auto outcome = RunExport(request, kHttpJsonContentType, "");

  EXPECT_EQ(request, outcome.request_body);
  EXPECT_EQ(kHttpJsonContentType, outcome.request_content_type);
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, outcome.export_result);
}

TEST(OtlpHttpTransportTest, SendsTheContentTypeItWasGiven)
{
  const auto outcome = RunExport("\x01\x02", kHttpBinaryContentType, "");

  EXPECT_EQ(kHttpBinaryContentType, outcome.request_content_type);
}

TEST(OtlpHttpTransportTest, HandsTheResponseBodyBackRaw)
{
  const std::string response = R"({"partialSuccess":{"rejectedSpans":"21"}})";

  const auto outcome = RunExport("{}", kHttpJsonContentType, response);

  EXPECT_TRUE(outcome.callback_ran);
  EXPECT_EQ(response, outcome.response_body);
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, outcome.callback_result);
}

TEST(OtlpHttpTransportTest, ReportsFailureOnAnErrorStatus)
{
  const auto outcome = RunExport("{}", kHttpJsonContentType, "nope", 503);

  EXPECT_TRUE(outcome.callback_ran);
  EXPECT_EQ(sdk::common::ExportResult::kFailure, outcome.callback_result);
  EXPECT_EQ(sdk::common::ExportResult::kFailure, outcome.export_result);
  // The body reaches the caller whatever the status, so a collector that
  // explains itself in an error body can be read.
  EXPECT_EQ("nope", outcome.response_body);
}

TEST(OtlpHttpTransportTest, ReportsFailureWithoutSendingAfterShutdown)
{
  auto client = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  OtlpHttpTransport transport(MakeOptions(), client);
  EXPECT_TRUE(transport.Shutdown());
  EXPECT_TRUE(transport.IsShutdown());

  bool callback_ran = false;
  const auto result = transport.Export(
      BodyOf("{}"), kHttpJsonContentType,
      [&callback_ran](sdk::common::ExportResult, const http_client::Body &) {
        callback_ran = true;
        return true;
      },
      0);

  EXPECT_EQ(sdk::common::ExportResult::kFailure, result);
  // Every failure path reports exactly once, so a caller waiting on the
  // callback is never left waiting.
  EXPECT_TRUE(callback_ran);
}

TEST(OtlpHttpTransportTest, ReportsFailureOnAnUnparseableUrl)
{
  auto options = MakeOptions();
  // Only an out-of-range port makes the URL parser give up.
  options.url = "http://localhost:99999/v1/traces";
  auto client = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  OtlpHttpTransport transport(std::move(options), client);

  bool callback_ran = false;
  const auto result = transport.Export(
      BodyOf("{}"), kHttpJsonContentType,
      [&callback_ran](sdk::common::ExportResult, const http_client::Body &) {
        callback_ran = true;
        return true;
      },
      0);

  EXPECT_EQ(sdk::common::ExportResult::kFailure, result);
  EXPECT_TRUE(callback_ran);
}

}  // namespace
}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
