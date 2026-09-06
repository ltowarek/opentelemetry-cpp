// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_http_push_metric_builder.h"

#include <memory>
#include <utility>

#include "opentelemetry/exporters/otlp/detail/otlp_json_http_builder_options.h"
#include "opentelemetry/exporters/otlp/otlp_builder_utils.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_http_metric_exporter_factory.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/configuration/default_histogram_aggregation.h"
#include "opentelemetry/sdk/configuration/otlp_http_push_metric_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_push_metric_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

void OtlpJsonHttpPushMetricBuilder::Register(opentelemetry::sdk::configuration::Registry *registry)
{
  auto builder = std::make_unique<OtlpJsonHttpPushMetricBuilder>();
  registry->SetOtlpHttpPushMetricExporterBuilder(std::move(builder));
}

std::unique_ptr<opentelemetry::sdk::metrics::PushMetricExporter>
OtlpJsonHttpPushMetricBuilder::Build(
    const opentelemetry::sdk::configuration::OtlpHttpPushMetricExporterConfiguration *model) const
{
  // FIXME-SDK: default_histogram_aggregation is parsed but not implemented by the SDK.
  if (model->default_histogram_aggregation !=
      opentelemetry::sdk::configuration::DefaultHistogramAggregation::explicit_bucket_histogram)
  {
    OTEL_INTERNAL_LOG_WARN(
        "[Otlp Json Http Exporter] default_histogram_aggregation is not supported and will be "
        "ignored");
  }

  auto options = detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpMetricExporterOptions>(*model);

  options.aggregation_temporality =
      OtlpBuilderUtils::ConvertTemporalityPreference(model->temporality_preference);

  return OtlpJsonHttpMetricExporterFactory::Create(options);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
