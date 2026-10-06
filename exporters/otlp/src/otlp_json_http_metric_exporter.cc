// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_http_metric_exporter.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/detail/default_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/detail/otlp_aggregation_temporality.h"
#include "opentelemetry/exporters/otlp/detail/otlp_http_transport.h"
#include "opentelemetry/exporters/otlp/detail/otlp_json_http_client_options.h"
#include "opentelemetry/exporters/otlp/detail/otlp_json_http_send.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_metric_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_json_partial_success.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/ext/http/client/detail/default_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

// A null factory means "use the compiled-in default" wherever one can be supplied: on the
// runtime options, which default to empty, and on the constructor overload that takes
// factories directly, which would otherwise store a null and dereference it on export.
std::shared_ptr<JsonWriterFactory> ResolveJsonWriterFactory(
    const std::shared_ptr<JsonWriterFactory> &json_writer_factory) noexcept
{
  return json_writer_factory ? json_writer_factory : detail::GetDefaultJsonWriterFactory();
}

std::shared_ptr<JsonReaderFactory> ResolveJsonReaderFactory(
    const std::shared_ptr<JsonReaderFactory> &json_reader_factory) noexcept
{
  return json_reader_factory ? json_reader_factory : detail::GetDefaultJsonReaderFactory();
}

}  // namespace

OtlpJsonHttpMetricExporter::OtlpJsonHttpMetricExporter()
    : OtlpJsonHttpMetricExporter(OtlpHttpMetricExporterOptions())
{}

OtlpJsonHttpMetricExporter::OtlpJsonHttpMetricExporter(const OtlpHttpMetricExporterOptions &options)
    : OtlpJsonHttpMetricExporter(options, OtlpHttpMetricExporterRuntimeOptions())
{}

OtlpJsonHttpMetricExporter::OtlpJsonHttpMetricExporter(
    const OtlpHttpMetricExporterOptions &options,
    const OtlpHttpMetricExporterRuntimeOptions &runtime_options)
    : OtlpJsonHttpMetricExporter(options,
                                 runtime_options,
                                 ext::http::client::detail::GetDefaultHttpClientFactory()->Create(
                                     runtime_options.thread_instrumentation))
{}

OtlpJsonHttpMetricExporter::OtlpJsonHttpMetricExporter(
    const OtlpHttpMetricExporterOptions &options,
    const OtlpHttpMetricExporterRuntimeOptions &runtime_options,
    std::shared_ptr<ext::http::client::HttpClient> http_client)
    : OtlpJsonHttpMetricExporter(options,
                                 runtime_options,
                                 std::move(http_client),
                                 ResolveJsonWriterFactory(runtime_options.json_writer_factory),
                                 ResolveJsonReaderFactory(runtime_options.json_reader_factory))
{}

OtlpJsonHttpMetricExporter::OtlpJsonHttpMetricExporter(
    const OtlpHttpMetricExporterOptions &options,
    const OtlpHttpMetricExporterRuntimeOptions &runtime_options,
    std::shared_ptr<ext::http::client::HttpClient> http_client,
    const std::shared_ptr<JsonWriterFactory> &json_writer_factory,
    const std::shared_ptr<JsonReaderFactory> &json_reader_factory)
    : options_(options),
      transport_(std::make_unique<detail::OtlpHttpTransport>(
          detail::MakeOtlpJsonHttpClientOptions(options, runtime_options),
          std::move(http_client))),
      json_writer_factory_(ResolveJsonWriterFactory(json_writer_factory)),
      json_reader_factory_(ResolveJsonReaderFactory(json_reader_factory)),
      aggregation_temporality_selector_(
          detail::ChooseAggregationTemporalitySelector(options_.aggregation_temporality))
{}

OtlpJsonHttpMetricExporter::~OtlpJsonHttpMetricExporter() = default;

// ----------------------------- Exporter methods ------------------------------

opentelemetry::sdk::metrics::AggregationTemporality
OtlpJsonHttpMetricExporter::GetAggregationTemporality(
    opentelemetry::sdk::metrics::InstrumentType instrument_type) const noexcept
{
  return aggregation_temporality_selector_(instrument_type);
}

opentelemetry::sdk::common::ExportResult OtlpJsonHttpMetricExporter::Export(
    const opentelemetry::sdk::metrics::ResourceMetrics &data) noexcept
{
  if (transport_->IsShutdown())
  {
    const std::size_t metric_count = data.scope_metric_data_.size();
    OTEL_INTERNAL_LOG_ERROR("[OTLP METRIC HTTP Exporter] ERROR: Export "
                            << metric_count << " metric(s) failed, exporter is shutdown");
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  if (data.scope_metric_data_.empty())
  {
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }

  auto json_writer = json_writer_factory_->Create();
  if (!json_writer)
  {
    OTEL_INTERNAL_LOG_ERROR(
        "[OTLP METRIC HTTP Exporter] ERROR: JsonWriterFactory::Create() returned nullptr");
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  ConvertMetricsToJson(*json_writer, data);

  // ToString() can fail on its own, so the body is only usable once ok() holds after it.
  std::string body_json = json_writer->ok() ? json_writer->ToString() : std::string();
  if (!json_writer->ok())
  {
    OTEL_INTERNAL_LOG_ERROR("[OTLP METRIC HTTP Exporter] ERROR: Failed to serialize the request");
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  return detail::SendOtlpJsonRequest(
      *transport_, json_reader_factory_, OtlpMetricPartialSuccessSignal(), body_json,
      data.scope_metric_data_.size(), detail::MaxRunningRequests(options_));
}

bool OtlpJsonHttpMetricExporter::ForceFlush(std::chrono::microseconds timeout) noexcept
{
  return transport_->ForceFlush(timeout);
}

bool OtlpJsonHttpMetricExporter::Shutdown(std::chrono::microseconds timeout) noexcept
{
  return transport_->Shutdown(timeout);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
