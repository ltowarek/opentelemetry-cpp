// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * Factory class for OtlpJsonHttpLogRecordExporter.
 *
 * The header pulls in neither protobuf nor a JSON backend, so a consumer can
 * select the protobuf-free log record exporter without either appearing in its
 * own translation unit.
 */
class OPENTELEMETRY_EXPORT OtlpJsonHttpLogRecordExporterFactory
{
public:
  /**
   * Create an OtlpJsonHttpLogRecordExporter using all default options.
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create();

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options.
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create(
      const OtlpHttpLogRecordExporterOptions &options);

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options and runtime options.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create(
      const OtlpHttpLogRecordExporterOptions &options,
      const OtlpHttpLogRecordExporterRuntimeOptions &runtime_options);

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options, runtime options and HTTP
   * client.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   * @param http_client the HTTP client to be used for exporting
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create(
      const OtlpHttpLogRecordExporterOptions &options,
      const OtlpHttpLogRecordExporterRuntimeOptions &runtime_options,
      std::shared_ptr<opentelemetry::ext::http::client::HttpClient> http_client);

  /**
   * Create an OtlpJsonHttpLogRecordExporter using the given options, runtime options, HTTP client
   * and JSON backends.
   *
   * The one overload that takes the JSON backends, so that a build with the bundled
   * nlohmann backend excluded has a way in.
   *
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   * @param http_client the HTTP client to be used for exporting
   * @param json_writer_factory the JsonWriter factory used to serialize the request
   * @param json_reader_factory the JsonReader factory used to read the response
   */
  static std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> Create(
      const OtlpHttpLogRecordExporterOptions &options,
      const OtlpHttpLogRecordExporterRuntimeOptions &runtime_options,
      std::shared_ptr<opentelemetry::ext::http::client::HttpClient> http_client,
      const std::shared_ptr<JsonWriterFactory> &json_writer_factory,
      const std::shared_ptr<JsonReaderFactory> &json_reader_factory);
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
