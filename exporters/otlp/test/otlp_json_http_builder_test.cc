// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Covers the file-configuration path for the protobuf-free exporters.
//
// Three things are worth asserting and nothing else is. That registering one
// of these builders fills the same registry slot the protobuf builder would --
// that substitution is the whole mechanism, and it is what makes a separate
// registry slot unnecessary. That a node asking for protobuf encoding is
// warned about rather than silently honoured or failed. And that every field
// the configuration node carries survives the mapping, because these builders
// are only a safe substitute if nothing a user configured is quietly dropped.
//
// The mapping is asserted through the seam the builders call rather than
// through Build(), which returns an exporter that exposes none of it.

#include "opentelemetry/exporters/otlp/detail/otlp_json_http_builder_options.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>

#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_http_log_record_builder.h"
#include "opentelemetry/exporters/otlp/otlp_json_http_push_metric_builder.h"
#include "opentelemetry/exporters/otlp/otlp_json_http_span_builder.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/configuration/default_histogram_aggregation.h"
#include "opentelemetry/sdk/configuration/headers_configuration.h"
#include "opentelemetry/sdk/configuration/http_tls_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_http_encoding.h"
#include "opentelemetry/sdk/configuration/otlp_http_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_http_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/otlp_http_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/configuration/temporality_preference.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/test_common/sdk/common/scoped_test_log_handler.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace
{

namespace configuration = opentelemetry::sdk::configuration;
using opentelemetry::test_common::ScopedTestLogHandler;

// Every connection setting an otlp_http node can carry, set to something
// distinguishable, so a dropped field shows up as a failed expectation rather
// than as a value that happens to match a default.
template <typename ConfigurationModel>
void FillModel(ConfigurationModel &model)
{
  model.endpoint     = "http://example.com:4318/v1/signal";
  model.encoding     = configuration::OtlpHttpEncoding::json;
  model.timeout      = 12;
  model.compression  = "gzip";
  model.headers_list = "k1=v1,k2=v2";

  auto tls       = std::make_unique<configuration::HttpTlsConfiguration>();
  tls->ca_file   = "/tmp/ca.pem";
  tls->key_file  = "/tmp/key.pem";
  tls->cert_file = "/tmp/cert.pem";
  model.tls      = std::move(tls);
}

template <typename ExporterOptions>
void ExpectModelFieldsSurvived(const ExporterOptions &options)
{
  EXPECT_EQ("http://example.com:4318/v1/signal", options.url);
  EXPECT_EQ(
      std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds{12}),
      options.timeout);
  EXPECT_EQ("gzip", options.compression);
  EXPECT_EQ("/tmp/ca.pem", options.ssl_ca_cert_path);
  EXPECT_EQ("/tmp/key.pem", options.ssl_client_key_path);
  EXPECT_EQ("/tmp/cert.pem", options.ssl_client_cert_path);

  const auto has_header = [&options](const std::string &key, const std::string &value) {
    const auto range = options.http_headers.equal_range(key);
    return std::any_of(range.first, range.second,
                       [&value](const auto &entry) { return entry.second == value; });
  };
  EXPECT_TRUE(has_header("k1", "v1"));
  EXPECT_TRUE(has_header("k2", "v2"));
}

TEST(OtlpJsonHttpBuilder, SpanNodeFieldsSurviveTheMapping)
{
  configuration::OtlpHttpSpanExporterConfiguration model;
  FillModel(model);

  ExpectModelFieldsSurvived(
      detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpExporterOptions>(model));
}

TEST(OtlpJsonHttpBuilder, LogRecordNodeFieldsSurviveTheMapping)
{
  configuration::OtlpHttpLogRecordExporterConfiguration model;
  FillModel(model);

  ExpectModelFieldsSurvived(
      detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpLogRecordExporterOptions>(model));
}

TEST(OtlpJsonHttpBuilder, MetricNodeFieldsSurviveTheMapping)
{
  configuration::OtlpHttpPushMetricExporterConfiguration model;
  FillModel(model);
  model.temporality_preference = configuration::TemporalityPreference::delta;

  ExpectModelFieldsSurvived(
      detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpMetricExporterOptions>(model));
}

// A node with no TLS section must not be read as one asking for empty paths.
TEST(OtlpJsonHttpBuilder, LeavesTlsPathsEmptyWhenTheNodeHasNoTlsSection)
{
  configuration::OtlpHttpSpanExporterConfiguration model;
  FillModel(model);
  model.tls.reset();

  const auto options = detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpExporterOptions>(model);

  EXPECT_TRUE(options.ssl_ca_cert_path.empty());
  EXPECT_TRUE(options.ssl_client_key_path.empty());
  EXPECT_TRUE(options.ssl_client_cert_path.empty());
}

TEST(OtlpJsonHttpBuilder, WarnsAndStillExportsJsonWhenTheNodeAsksForProtobuf)
{
  configuration::OtlpHttpSpanExporterConfiguration model;
  FillModel(model);
  model.encoding = configuration::OtlpHttpEncoding::protobuf;

  ScopedTestLogHandler log{sdk::common::internal_log::LogLevel::Warning};
  const auto options = detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpExporterOptions>(model);
  const auto entries = log.Drain();

  const bool warned = std::any_of(entries.begin(), entries.end(), [](const auto &entry) {
    return entry.level == sdk::common::internal_log::LogLevel::Warning &&
           entry.msg.find("protobuf") != std::string::npos;
  });
  EXPECT_TRUE(warned);

  // The rest of the node is still honoured; the exporter just emits JSON.
  ExpectModelFieldsSurvived(options);
}

TEST(OtlpJsonHttpBuilder, DoesNotWarnWhenTheNodeAsksForJson)
{
  configuration::OtlpHttpSpanExporterConfiguration model;
  FillModel(model);

  ScopedTestLogHandler log{sdk::common::internal_log::LogLevel::Warning};
  detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpExporterOptions>(model);
  const auto entries = log.Drain();

  EXPECT_TRUE(entries.empty());
}

// Registering a JSON builder fills the slot the protobuf builder would, which
// is what lets an application choose between them without a new registry slot.
TEST(OtlpJsonHttpBuilder, RegisterFillsTheSpanSlot)
{
  configuration::Registry registry;
  EXPECT_EQ(registry.GetOtlpHttpSpanBuilder(), nullptr);

  OtlpJsonHttpSpanBuilder::Register(&registry);

  EXPECT_NE(registry.GetOtlpHttpSpanBuilder(), nullptr);
}

TEST(OtlpJsonHttpBuilder, RegisterFillsTheMetricSlot)
{
  configuration::Registry registry;
  EXPECT_EQ(registry.GetOtlpHttpPushMetricExporterBuilder(), nullptr);

  OtlpJsonHttpPushMetricBuilder::Register(&registry);

  EXPECT_NE(registry.GetOtlpHttpPushMetricExporterBuilder(), nullptr);
}

TEST(OtlpJsonHttpBuilder, RegisterFillsTheLogRecordSlot)
{
  configuration::Registry registry;
  EXPECT_EQ(registry.GetOtlpHttpLogRecordBuilder(), nullptr);

  OtlpJsonHttpLogRecordBuilder::Register(&registry);

  EXPECT_NE(registry.GetOtlpHttpLogRecordBuilder(), nullptr);
}

// The temporality preference is the one field a builder maps itself rather
// than through the shared mapping, and the built exporter reports it, so this
// is asserted through Build() rather than at the mapping seam.
TEST(OtlpJsonHttpBuilder, MetricNodeTemporalityPreferenceReachesTheExporter)
{
  configuration::OtlpHttpPushMetricExporterConfiguration model;
  FillModel(model);
  model.default_histogram_aggregation =
      configuration::DefaultHistogramAggregation::explicit_bucket_histogram;

  model.temporality_preference = configuration::TemporalityPreference::delta;
  const auto delta_exporter    = OtlpJsonHttpPushMetricBuilder().Build(&model);
  ASSERT_NE(delta_exporter, nullptr);
  EXPECT_EQ(sdk::metrics::AggregationTemporality::kDelta,
            delta_exporter->GetAggregationTemporality(sdk::metrics::InstrumentType::kCounter));

  model.temporality_preference   = configuration::TemporalityPreference::cumulative;
  const auto cumulative_exporter = OtlpJsonHttpPushMetricBuilder().Build(&model);
  ASSERT_NE(cumulative_exporter, nullptr);
  EXPECT_EQ(sdk::metrics::AggregationTemporality::kCumulative,
            cumulative_exporter->GetAggregationTemporality(sdk::metrics::InstrumentType::kCounter));
}

TEST(OtlpJsonHttpBuilder, BuildsAWorkingExporterForEachSignal)
{
  configuration::OtlpHttpSpanExporterConfiguration span_model;
  FillModel(span_model);
  EXPECT_NE(OtlpJsonHttpSpanBuilder().Build(&span_model), nullptr);

  configuration::OtlpHttpLogRecordExporterConfiguration log_model;
  FillModel(log_model);
  EXPECT_NE(OtlpJsonHttpLogRecordBuilder().Build(&log_model), nullptr);

  configuration::OtlpHttpPushMetricExporterConfiguration metric_model;
  FillModel(metric_model);
  metric_model.temporality_preference = configuration::TemporalityPreference::cumulative;
  metric_model.default_histogram_aggregation =
      configuration::DefaultHistogramAggregation::explicit_bucket_histogram;
  EXPECT_NE(OtlpJsonHttpPushMetricBuilder().Build(&metric_model), nullptr);
}

}  // namespace
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
