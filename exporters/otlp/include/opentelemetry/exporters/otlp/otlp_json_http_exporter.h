// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <memory>

#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/recordable.h"
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
 * Exports spans as OTLP/JSON over HTTP without referencing protobuf.
 *
 * Same signal and same wire bytes as OtlpHttpExporter configured for JSON --
 * an equivalence test asserts the two encode identical spans identically --
 * but it maps from the recordable straight to JSON tokens rather than through
 * a protobuf message, and links neither the message runtime nor the generated
 * code. On a device where that runtime does not fit, this is the trace
 * exporter.
 *
 * The exporter emits JSON whatever `content_type` the options carry; binary
 * OTLP is what OtlpHttpExporter is for.
 */
class OPENTELEMETRY_EXPORT OtlpJsonHttpExporter final
    : public opentelemetry::sdk::trace::SpanExporter
{
public:
  /**
   * Create an OtlpJsonHttpExporter using all default options.
   */
  OtlpJsonHttpExporter();

  /**
   * Create an OtlpJsonHttpExporter using the given options.
   */
  explicit OtlpJsonHttpExporter(const OtlpHttpExporterOptions &options);

  /**
   * Create an OtlpJsonHttpExporter using the given options and runtime options.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   */
  OtlpJsonHttpExporter(const OtlpHttpExporterOptions &options,
                       const OtlpHttpExporterRuntimeOptions &runtime_options);

  /**
   * Create an OtlpJsonHttpExporter using the given options, runtime options and HTTP client.
   * @param options the exporter options
   * @param runtime_options the runtime options (e.g. thread instrumentation)
   * @param http_client the HTTP client to be used for exporting
   */
  OtlpJsonHttpExporter(const OtlpHttpExporterOptions &options,
                       const OtlpHttpExporterRuntimeOptions &runtime_options,
                       std::shared_ptr<ext::http::client::HttpClient> http_client);

  /**
   * Create an OtlpJsonHttpExporter using the given options, runtime options, HTTP client and
   * JSON backends.
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
  OtlpJsonHttpExporter(const OtlpHttpExporterOptions &options,
                       const OtlpHttpExporterRuntimeOptions &runtime_options,
                       std::shared_ptr<ext::http::client::HttpClient> http_client,
                       const std::shared_ptr<JsonWriterFactory> &json_writer_factory,
                       const std::shared_ptr<JsonReaderFactory> &json_reader_factory);

  ~OtlpJsonHttpExporter() override;

  /**
   * Create a span recordable.
   * @return a newly initialized Recordable object
   */
  std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override;

  /**
   * Export
   * @param spans a span of unique pointers to span recordables
   */
  opentelemetry::sdk::common::ExportResult Export(
      const nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> &spans) noexcept
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
  OtlpHttpExporterOptions options_;

  std::unique_ptr<detail::OtlpHttpTransport> transport_;
  std::shared_ptr<JsonWriterFactory> json_writer_factory_;
  std::shared_ptr<JsonReaderFactory> json_reader_factory_;
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
