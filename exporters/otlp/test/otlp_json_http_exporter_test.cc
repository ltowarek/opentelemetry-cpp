// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Covers the OTLP/JSON trace exporter at the seam the protobuf exporter tests
// use: the request body that reached the HTTP client, and what the exporter
// reported back.
//
// This target links no protobuf, which is the point -- the equivalence test
// already proves the mapping matches the protobuf path byte for byte, so what
// is left to show is that an exporter reaches the network with that mapping
// and without the message runtime. It fails to link, loudly, if the build
// target split ever puts protobuf back underneath.

#include "opentelemetry/exporters/otlp/otlp_json_http_exporter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_client.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_trace_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/test_common/ext/http/client/nosend/http_client_factory_nosend.h"
#include "opentelemetry/test_common/ext/http/client/nosend/http_client_nosend.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace
{

namespace http_client = opentelemetry::ext::http::client;
namespace sdk_trace   = opentelemetry::sdk::trace;

// What the exporter did with one batch, seen from the HTTP client it was given.
struct ExportOutcome
{
  std::string request_body;
  std::string request_content_type;
  sdk::common::ExportResult result = sdk::common::ExportResult::kFailure;
};

OtlpHttpExporterOptions MakeOptions()
{
  OtlpHttpExporterOptions options;
  options.url     = "http://localhost:4318/v1/traces";
  options.timeout = std::chrono::system_clock::duration::zero();
  return options;
}

// Records one named span into a batch the exporter will accept.
std::vector<std::unique_ptr<sdk_trace::Recordable>> MakeBatch(OtlpJsonHttpExporter &exporter)
{
  std::vector<std::unique_ptr<sdk_trace::Recordable>> batch;
  batch.emplace_back(exporter.MakeRecordable());
  batch.back()->SetName("Test span");
  batch.back()->SetAttribute("key1", "value1");
  return batch;
}

std::string ExpectedBody(const nostd::span<std::unique_ptr<sdk_trace::Recordable>> &batch)
{
  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  ConvertSpansToJson(*writer, batch);
  EXPECT_TRUE(writer->ok());
  return writer->ToString();
}

ExportOutcome ExportOneBatch(const std::string &response_text,
                             http_client::StatusCode status_code = http_client::nosend::Http_Ok,
                             std::string *expected_body          = nullptr)
{
  ExportOutcome outcome;

  auto client         = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  auto no_send_client = std::static_pointer_cast<http_client::nosend::HttpClient>(client);
  OtlpJsonHttpExporter exporter(MakeOptions(), OtlpHttpExporterRuntimeOptions(), client);

  auto mock_session =
      std::static_pointer_cast<http_client::nosend::Session>(no_send_client->session_);
  EXPECT_CALL(*mock_session, SendRequest)
      .WillOnce([&](const std::shared_ptr<http_client::EventHandler> &handler) {
        const auto &request = *mock_session->GetRequest();
        outcome.request_body.assign(request.body_.begin(), request.body_.end());
        auto header = request.headers_.find("Content-Type");
        if (header != request.headers_.end())
        {
          outcome.request_content_type = header->second;
        }

        http_client::nosend::Response response;
        response.status_code_ = status_code;
        response.body_.assign(response_text.begin(), response_text.end());
        response.Finish(*handler);
      });

  auto batch = MakeBatch(exporter);
  nostd::span<std::unique_ptr<sdk_trace::Recordable>> spans(batch.data(), batch.size());
  if (expected_body != nullptr)
  {
    *expected_body = ExpectedBody(spans);
  }
  outcome.result = exporter.Export(spans);

  return outcome;
}

TEST(OtlpJsonHttpExporterTest, PostsTheMappedSpansAsJson)
{
  std::string expected_body;
  const auto outcome = ExportOneBatch("", http_client::nosend::Http_Ok, &expected_body);

  EXPECT_EQ(expected_body, outcome.request_body);
  EXPECT_EQ(kHttpJsonContentType, outcome.request_content_type);
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, outcome.result);
}

TEST(OtlpJsonHttpExporterTest, ReadsAPartialSuccessResponse)
{
  const auto outcome = ExportOneBatch(
      R"({"partialSuccess":{"rejectedSpans":"21","errorMessage":"too many spans!!"}})");

  // A rejection is reported, not failed: the request itself landed.
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, outcome.result);
}

// An asynchronous export reports success as soon as the request is under way and carries the
// final result to the callback instead, so only a synchronous one has a result to assert on.
#ifndef ENABLE_ASYNC_EXPORT
TEST(OtlpJsonHttpExporterTest, ReportsFailureOnAnUnreadableResponseBody)
{
  const auto outcome = ExportOneBatch("{some bad JSON");

  EXPECT_EQ(sdk::common::ExportResult::kFailure, outcome.result);
}

TEST(OtlpJsonHttpExporterTest, ReportsFailureOnAnErrorStatus)
{
  const auto outcome = ExportOneBatch("", 503);

  EXPECT_EQ(sdk::common::ExportResult::kFailure, outcome.result);
}
#endif

TEST(OtlpJsonHttpExporterTest, SucceedsWithoutSendingAnEmptyBatch)
{
  auto client = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  OtlpJsonHttpExporter exporter(MakeOptions(), OtlpHttpExporterRuntimeOptions(), client);

  nostd::span<std::unique_ptr<sdk_trace::Recordable>> empty;
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, exporter.Export(empty));
}

TEST(OtlpJsonHttpExporterTest, ReportsFailureAfterShutdown)
{
  auto client = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  OtlpJsonHttpExporter exporter(MakeOptions(), OtlpHttpExporterRuntimeOptions(), client);
  EXPECT_TRUE(exporter.Shutdown());

  auto batch = MakeBatch(exporter);
  nostd::span<std::unique_ptr<sdk_trace::Recordable>> spans(batch.data(), batch.size());
  EXPECT_EQ(sdk::common::ExportResult::kFailure, exporter.Export(spans));
}

}  // namespace
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
