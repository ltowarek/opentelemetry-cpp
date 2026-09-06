// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <memory>

#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/sdk/logs/recordable.h"
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
 * Exports log records as OTLP/JSON over HTTP without referencing protobuf.
 *
 * Same signal and same wire bytes as OtlpHttpLogRecordExporter configured for
 * JSON -- an equivalence test asserts the two encode identical records
 * identically -- but it maps from the recordable straight to JSON tokens
 * rather than through a protobuf message, and links neither the message
 * runtime nor the generated code. On a device where that runtime does not fit,
 * this is the log record exporter.
 *
 * The exporter emits JSON whatever `content_type` the options carry; binary
 * OTLP is what OtlpHttpExporter is for.
 */
class OPENTELEMETRY_EXPORT OtlpJsonHttpLogRecordExporter final
    : public opentelemetry::sdk::logs::LogRecordExporter
{
public:
  /**
   * Create an OtlpJsonHttpLogRecordExporter using all default options.
   */
  OtlpJsonHttpLogRecordExporter();

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options.
   */
  explicit OtlpJsonHttpLogRecordExporter(const OtlpHttpLogRecordExporterOptions &options);

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options and runtime options.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   */
  OtlpJsonHttpLogRecordExporter(const OtlpHttpLogRecordExporterOptions &options,
                                const OtlpHttpLogRecordExporterRuntimeOptions &runtime_options);

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options, runtime options and HTTP
   * client.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   * @param http_client the HTTP client to be used for exporting
   */
  OtlpJsonHttpLogRecordExporter(const OtlpHttpLogRecordExporterOptions &options,
                                const OtlpHttpLogRecordExporterRuntimeOptions &runtime_options,
                                std::shared_ptr<ext::http::client::HttpClient> http_client);

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options, runtime options, HTTP client
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
  OtlpJsonHttpLogRecordExporter(const OtlpHttpLogRecordExporterOptions &options,
                                const OtlpHttpLogRecordExporterRuntimeOptions &runtime_options,
                                std::shared_ptr<ext::http::client::HttpClient> http_client,
                                const std::shared_ptr<JsonWriterFactory> &json_writer_factory,
                                const std::shared_ptr<JsonReaderFactory> &json_reader_factory);

  ~OtlpJsonHttpLogRecordExporter() override;

  /**
   * Create a log record recordable.
   * @return a newly initialized Recordable object
   */
  std::unique_ptr<opentelemetry::sdk::logs::Recordable> MakeRecordable() noexcept override;

  /**
   * Export
   * @param records a span of unique pointers to log record recordables
   */
  opentelemetry::sdk::common::ExportResult Export(
      const nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>> &records) noexcept
      override;

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
  OtlpHttpLogRecordExporterOptions options_;

  std::unique_ptr<detail::OtlpHttpTransport> transport_;
  std::shared_ptr<JsonWriterFactory> json_writer_factory_;
  std::shared_ptr<JsonReaderFactory> json_reader_factory_;
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
