// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <memory>

#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_metric_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/metrics/push_metric_exporter.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{
class OtlpHttpTransport;
}  // namespace detail

/**
 * Exports metrics as OTLP/JSON over HTTP without referencing protobuf.
 *
 * Same signal and same wire bytes as OtlpHttpMetricExporter configured for
 * JSON -- an equivalence test asserts the two encode identical metrics
 * identically -- but it maps from the SDK metric data model straight to JSON
 * tokens rather than through a protobuf message, and links neither the message
 * runtime nor the generated code. On a device where that runtime does not fit,
 * this is the metric exporter.
 *
 * The exporter emits JSON whatever `content_type` the options carry; binary
 * OTLP is what OtlpHttpMetricExporter is for.
 */
class OPENTELEMETRY_EXPORT OtlpJsonHttpMetricExporter final
    : public opentelemetry::sdk::metrics::PushMetricExporter
{
public:
  /**
   * Create an OtlpJsonHttpMetricExporter using all default options.
   */
  OtlpJsonHttpMetricExporter();

  /**
   * Create an OtlpJsonHttpMetricExporter using the given options.
   */
  explicit OtlpJsonHttpMetricExporter(const OtlpHttpMetricExporterOptions &options);

  /**
   * Create an OtlpJsonHttpMetricExporter using the given options and runtime options.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   */
  OtlpJsonHttpMetricExporter(const OtlpHttpMetricExporterOptions &options,
                             const OtlpHttpMetricExporterRuntimeOptions &runtime_options);

  /**
   * Create an OtlpJsonHttpMetricExporter using the given options, runtime options and HTTP
   * client.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   * @param http_client the HTTP client to be used for exporting
   */
  OtlpJsonHttpMetricExporter(const OtlpHttpMetricExporterOptions &options,
                             const OtlpHttpMetricExporterRuntimeOptions &runtime_options,
                             std::shared_ptr<ext::http::client::HttpClient> http_client);

  /**
   * Create an OtlpJsonHttpMetricExporter using the given options, runtime options, HTTP client
   * and JSON backends.
   *
   * The one constructor that takes the JSON backends, so that a build with the bundled
   * nlohmann backend excluded has a way in: every other constructor asks for the default,
   * which such a build does not have.
   *
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   * @param http_client the HTTP client to be used for exporting
   * @param json_writer_factory the JsonWriter factory used to serialize the request
   * @param json_reader_factory the JsonReader factory used to read the response
   */
  OtlpJsonHttpMetricExporter(const OtlpHttpMetricExporterOptions &options,
                             const OtlpHttpMetricExporterRuntimeOptions &runtime_options,
                             std::shared_ptr<ext::http::client::HttpClient> http_client,
                             const std::shared_ptr<JsonWriterFactory> &json_writer_factory,
                             const std::shared_ptr<JsonReaderFactory> &json_reader_factory);

  ~OtlpJsonHttpMetricExporter() override;

  /**
   * Get the AggregationTemporality for the given instrument type.
   */
  opentelemetry::sdk::metrics::AggregationTemporality GetAggregationTemporality(
      opentelemetry::sdk::metrics::InstrumentType instrument_type) const noexcept override;

  /**
   * Export
   * @param data metrics data
   */
  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::sdk::metrics::ResourceMetrics &data) noexcept override;

  /**
   * Force flush the exporter.
   * @param timeout an option timeout, default to max.
   * @return return true when all data are exported, and false when timeout
   */
  bool ForceFlush(
      std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

  /**
   * Shut down the exporter.
   * @param timeout an optional timeout, the default timeout of 0 means that no
   * timeout is applied.
   * @return return the status of this operation
   */
  bool Shutdown(
      std::chrono::microseconds timeout = (std::chrono::microseconds::max)()) noexcept override;

private:
  // The configuration options associated with this exporter.
  OtlpHttpMetricExporterOptions options_;

  std::unique_ptr<detail::OtlpHttpTransport> transport_;
  std::shared_ptr<JsonWriterFactory> json_writer_factory_;
  std::shared_ptr<JsonReaderFactory> json_reader_factory_;

  const opentelemetry::sdk::metrics::AggregationTemporalitySelector
      aggregation_temporality_selector_;
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
