// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>

#include <opentelemetry/exporters/otlp/otlp_http_builder_utils.h>

#include <opentelemetry/exporters/otlp/otlp_http_client.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>

#include <opentelemetry/exporters/otlp/otlp_json_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_json_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_json_http_metric_exporter_factory.h>

#ifdef ENABLE_JSON_WRITER_NLOHMANN
#  include <opentelemetry/exporters/otlp/otlp_json_http_log_record_builder.h>
#  include <opentelemetry/exporters/otlp/otlp_json_http_push_metric_builder.h>
#  include <opentelemetry/exporters/otlp/otlp_json_http_span_builder.h>
#endif

#include <opentelemetry/exporters/otlp/otlp_http_log_record_builder.h>
#include <opentelemetry/exporters/otlp/otlp_http_push_metric_builder.h>
#include <opentelemetry/exporters/otlp/otlp_http_span_builder.h>

TEST(ExportersOtlpHttpInstall, OtlpHttpExporter)
{
  auto options  = opentelemetry::exporter::otlp::OtlpHttpExporterOptions();
  auto exporter = opentelemetry::exporter::otlp::OtlpHttpExporterFactory::Create(options);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpHttpInstall, OtlpHttpLogRecordExporter)
{
  auto options  = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions();
  auto exporter = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(options);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpHttpInstall, OtlpHttpMetricExporter)
{
  auto options  = opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions();
  auto exporter = opentelemetry::exporter::otlp::OtlpHttpMetricExporterFactory::Create(options);
  ASSERT_TRUE(exporter != nullptr);
}

// These ask for encoding: json, and a configuration file cannot supply a
// JsonWriterFactory, so they need the default backend to be compiled in.
#ifdef ENABLE_JSON_WRITER_NLOHMANN
TEST(ExportersOtlpHttpBuilderInstall, OtlpHttpSpanBuilder)
{
  auto builder = std::make_unique<opentelemetry::exporter::otlp::OtlpHttpSpanBuilder>();
  ASSERT_TRUE(builder != nullptr);

  opentelemetry::sdk::configuration::OtlpHttpSpanExporterConfiguration model;
  model.endpoint    = "http://localhost:4318";
  model.encoding    = opentelemetry::sdk::configuration::OtlpHttpEncoding::json;
  model.timeout     = 12;
  model.compression = "none";

  auto exporter = builder->Build(&model);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpHttpBuilderInstall, OtlpHttpPushMetricBuilder)
{
  auto builder = std::make_unique<opentelemetry::exporter::otlp::OtlpHttpPushMetricBuilder>();
  ASSERT_TRUE(builder != nullptr);

  opentelemetry::sdk::configuration::OtlpHttpPushMetricExporterConfiguration model;
  model.endpoint    = "http://localhost:4318";
  model.encoding    = opentelemetry::sdk::configuration::OtlpHttpEncoding::json;
  model.timeout     = 12;
  model.compression = "none";
  model.temporality_preference =
      opentelemetry::sdk::configuration::TemporalityPreference::cumulative;

  auto exporter = builder->Build(&model);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpHttpBuilderInstall, OtlpHttpLogRecordBuilder)
{
  auto builder = std::make_unique<opentelemetry::exporter::otlp::OtlpHttpLogRecordBuilder>();
  ASSERT_TRUE(builder != nullptr);

  opentelemetry::sdk::configuration::OtlpHttpLogRecordExporterConfiguration model;
  model.endpoint    = "http://localhost:4318";
  model.encoding    = opentelemetry::sdk::configuration::OtlpHttpEncoding::json;
  model.timeout     = 12;
  model.compression = "none";

  auto exporter = builder->Build(&model);
  ASSERT_TRUE(exporter != nullptr);
}
#endif

TEST(ExportersOtlpHttpBuilderInstall, OtlpHttpBuilderUtilsConvertOtlpHttpEncoding)
{
  using opentelemetry::exporter::otlp::HttpRequestContentType;
  using opentelemetry::exporter::otlp::OtlpHttpBuilderUtils;
  using opentelemetry::sdk::configuration::OtlpHttpEncoding;

  EXPECT_EQ(OtlpHttpBuilderUtils::ConvertOtlpHttpEncoding(OtlpHttpEncoding::json),
            HttpRequestContentType::kJson);
  EXPECT_EQ(OtlpHttpBuilderUtils::ConvertOtlpHttpEncoding(OtlpHttpEncoding::protobuf),
            HttpRequestContentType::kBinary);
}

// The factory-less overloads resolve the default backends. An install without one is
// usable only through the constructors that take both factories, which the unit tests
// cover; what is checkable here is that the exported targets link.
#ifdef ENABLE_JSON_WRITER_NLOHMANN
TEST(ExportersOtlpJsonHttpInstall, OtlpJsonHttpExporter)
{
  auto options  = opentelemetry::exporter::otlp::OtlpHttpExporterOptions();
  auto exporter = opentelemetry::exporter::otlp::OtlpJsonHttpExporterFactory::Create(options);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpJsonHttpInstall, OtlpJsonHttpLogRecordExporter)
{
  auto options = opentelemetry::exporter::otlp::OtlpHttpLogRecordExporterOptions();
  auto exporter =
      opentelemetry::exporter::otlp::OtlpJsonHttpLogRecordExporterFactory::Create(options);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpJsonHttpInstall, OtlpJsonHttpMetricExporter)
{
  auto options  = opentelemetry::exporter::otlp::OtlpHttpMetricExporterOptions();
  auto exporter = opentelemetry::exporter::otlp::OtlpJsonHttpMetricExporterFactory::Create(options);
  ASSERT_TRUE(exporter != nullptr);
}
#endif

#ifdef ENABLE_JSON_WRITER_NLOHMANN
TEST(ExportersOtlpJsonHttpBuilderInstall, OtlpJsonHttpSpanBuilder)
{
  auto builder = std::make_unique<opentelemetry::exporter::otlp::OtlpJsonHttpSpanBuilder>();
  ASSERT_TRUE(builder != nullptr);

  opentelemetry::sdk::configuration::OtlpHttpSpanExporterConfiguration model;
  model.endpoint    = "http://localhost:4318";
  model.encoding    = opentelemetry::sdk::configuration::OtlpHttpEncoding::json;
  model.timeout     = 12;
  model.compression = "none";

  auto exporter = builder->Build(&model);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpJsonHttpBuilderInstall, OtlpJsonHttpPushMetricBuilder)
{
  auto builder = std::make_unique<opentelemetry::exporter::otlp::OtlpJsonHttpPushMetricBuilder>();
  ASSERT_TRUE(builder != nullptr);

  opentelemetry::sdk::configuration::OtlpHttpPushMetricExporterConfiguration model;
  model.endpoint    = "http://localhost:4318";
  model.encoding    = opentelemetry::sdk::configuration::OtlpHttpEncoding::json;
  model.timeout     = 12;
  model.compression = "none";
  model.temporality_preference =
      opentelemetry::sdk::configuration::TemporalityPreference::cumulative;

  auto exporter = builder->Build(&model);
  ASSERT_TRUE(exporter != nullptr);
}

TEST(ExportersOtlpJsonHttpBuilderInstall, OtlpJsonHttpLogRecordBuilder)
{
  auto builder = std::make_unique<opentelemetry::exporter::otlp::OtlpJsonHttpLogRecordBuilder>();
  ASSERT_TRUE(builder != nullptr);

  opentelemetry::sdk::configuration::OtlpHttpLogRecordExporterConfiguration model;
  model.endpoint    = "http://localhost:4318";
  model.encoding    = opentelemetry::sdk::configuration::OtlpHttpEncoding::json;
  model.timeout     = 12;
  model.compression = "none";

  auto exporter = builder->Build(&model);
  ASSERT_TRUE(exporter != nullptr);
}
#endif
