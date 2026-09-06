// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Covers the OTLP/JSON metric exporter at the seam the protobuf exporter tests
// use: the request body that reached the HTTP client, and what the exporter
// reported back.
//
// This target links no protobuf, which is the point -- the equivalence test
// already proves the mapping matches the protobuf path byte for byte, so what
// is left to show is that an exporter reaches the network with that mapping
// and without the message runtime. It fails to link, loudly, if the build
// target split ever puts protobuf back underneath.

#include "opentelemetry/exporters/otlp/otlp_json_http_metric_exporter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_client.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_metric_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/exporters/otlp/otlp_preferred_temporality.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/resource/resource.h"
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
namespace metric_sdk  = opentelemetry::sdk::metrics;

// What the exporter did with one collection, seen from the HTTP client it was given.
struct ExportOutcome
{
  std::string request_body;
  std::string request_content_type;
  sdk::common::ExportResult result = sdk::common::ExportResult::kFailure;
};

OtlpHttpMetricExporterOptions MakeOptions()
{
  OtlpHttpMetricExporterOptions options;
  options.url     = "http://localhost:4318/v1/metrics";
  options.timeout = std::chrono::system_clock::duration::zero();
  return options;
}

const opentelemetry::sdk::resource::Resource &TestResource()
{
  static const auto resource =
      opentelemetry::sdk::resource::Resource::Create({{"service.name", "json exporter"}});
  return resource;
}

const opentelemetry::sdk::instrumentationscope::InstrumentationScope &TestScope()
{
  static const auto scope =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("scope", "1.2.3");
  return *scope;
}

// One counter point in a closed collection window, which is what the SDK hands
// the exporter in practice.
metric_sdk::ResourceMetrics MakeResourceMetrics()
{
  metric_sdk::MetricData metric_data;
  metric_data.instrument_descriptor = metric_sdk::InstrumentDescriptor{
      "metric_name", "metric description", "ms", metric_sdk::InstrumentType::kCounter,
      metric_sdk::InstrumentValueType::kLong};
  metric_data.aggregation_temporality = metric_sdk::AggregationTemporality::kDelta;
  metric_data.start_ts =
      opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1700000000000000000LL));
  metric_data.end_ts =
      opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1700000001000000000LL));

  metric_sdk::SumPointData point;
  point.value_ = static_cast<std::int64_t>(11);
  metric_sdk::PointAttributes attributes;
  attributes.SetAttribute("key1", "value1");
  metric_data.point_data_attr_.push_back(
      metric_sdk::PointDataAttributes{std::move(attributes), point});

  metric_sdk::ResourceMetrics data;
  data.resource_ = &TestResource();
  data.scope_metric_data_.push_back(metric_sdk::ScopeMetrics{
      &TestScope(), std::vector<metric_sdk::MetricData>{std::move(metric_data)}});
  return data;
}

std::string ExpectedBody(const metric_sdk::ResourceMetrics &data)
{
  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  ConvertMetricsToJson(*writer, data);
  EXPECT_TRUE(writer->ok());
  return writer->ToString();
}

ExportOutcome ExportOneCollection(
    const std::string &response_text,
    http_client::StatusCode status_code = http_client::nosend::Http_Ok,
    std::string *expected_body          = nullptr)
{
  ExportOutcome outcome;

  auto client         = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  auto no_send_client = std::static_pointer_cast<http_client::nosend::HttpClient>(client);
  OtlpJsonHttpMetricExporter exporter(MakeOptions(), OtlpHttpMetricExporterRuntimeOptions(),
                                      client);

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

  const auto data = MakeResourceMetrics();
  if (expected_body != nullptr)
  {
    *expected_body = ExpectedBody(data);
  }
  outcome.result = exporter.Export(data);

  return outcome;
}

TEST(OtlpJsonHttpMetricExporterTest, PostsTheMappedMetricsAsJson)
{
  std::string expected_body;
  const auto outcome = ExportOneCollection("", http_client::nosend::Http_Ok, &expected_body);

  EXPECT_EQ(expected_body, outcome.request_body);
  EXPECT_EQ(kHttpJsonContentType, outcome.request_content_type);
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, outcome.result);
}

TEST(OtlpJsonHttpMetricExporterTest, ReadsAPartialSuccessResponse)
{
  const auto outcome = ExportOneCollection(
      R"({"partialSuccess":{"rejectedDataPoints":"21","errorMessage":"too many points!!"}})");

  // A rejection is reported, not failed: the request itself landed.
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, outcome.result);
}

// An asynchronous export reports success as soon as the request is under way and carries the
// final result to the callback instead, so only a synchronous one has a result to assert on.
#ifndef ENABLE_ASYNC_EXPORT
TEST(OtlpJsonHttpMetricExporterTest, ReportsFailureOnAnUnreadableResponseBody)
{
  const auto outcome = ExportOneCollection("{some bad JSON");

  EXPECT_EQ(sdk::common::ExportResult::kFailure, outcome.result);
}

TEST(OtlpJsonHttpMetricExporterTest, ReportsFailureOnAnErrorStatus)
{
  const auto outcome = ExportOneCollection("", 503);

  EXPECT_EQ(sdk::common::ExportResult::kFailure, outcome.result);
}
#endif

TEST(OtlpJsonHttpMetricExporterTest, SucceedsWithoutSendingAnEmptyCollection)
{
  auto client = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  OtlpJsonHttpMetricExporter exporter(MakeOptions(), OtlpHttpMetricExporterRuntimeOptions(),
                                      client);

  metric_sdk::ResourceMetrics empty;
  empty.resource_ = &TestResource();
  EXPECT_EQ(sdk::common::ExportResult::kSuccess, exporter.Export(empty));
}

TEST(OtlpJsonHttpMetricExporterTest, ReportsFailureAfterShutdown)
{
  auto client = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  OtlpJsonHttpMetricExporter exporter(MakeOptions(), OtlpHttpMetricExporterRuntimeOptions(),
                                      client);
  EXPECT_TRUE(exporter.Shutdown());

  EXPECT_EQ(sdk::common::ExportResult::kFailure, exporter.Export(MakeResourceMetrics()));
}

// The temporality the exporter reports has to match the protobuf exporter's,
// which is what a collector configuration is written against.
TEST(OtlpJsonHttpMetricExporterTest, ReportsTheConfiguredAggregationTemporality)
{
  auto options                    = MakeOptions();
  options.aggregation_temporality = PreferredAggregationTemporality::kDelta;
  auto client = test_common::ext::http::client::nosend::HttpClientFactoryNosend().Create();
  OtlpJsonHttpMetricExporter exporter(options, OtlpHttpMetricExporterRuntimeOptions(), client);

  EXPECT_EQ(metric_sdk::AggregationTemporality::kDelta,
            exporter.GetAggregationTemporality(metric_sdk::InstrumentType::kCounter));
  EXPECT_EQ(metric_sdk::AggregationTemporality::kCumulative,
            exporter.GetAggregationTemporality(metric_sdk::InstrumentType::kUpDownCounter));
}

}  // namespace
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
