// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_http_exporter_factory.h"

#include <memory>
#include <utility>

#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_http_exporter.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> OtlpJsonHttpExporterFactory::Create()
{
  OtlpHttpExporterOptions options;
  return Create(options);
}

std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> OtlpJsonHttpExporterFactory::Create(
    const OtlpHttpExporterOptions &options)
{
  OtlpHttpExporterRuntimeOptions runtime_options;
  return Create(options, runtime_options);
}

std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> OtlpJsonHttpExporterFactory::Create(
    const OtlpHttpExporterOptions &options,
    const OtlpHttpExporterRuntimeOptions &runtime_options)
{
  return std::make_unique<OtlpJsonHttpExporter>(options, runtime_options);
}

std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> OtlpJsonHttpExporterFactory::Create(
    const OtlpHttpExporterOptions &options,
    const OtlpHttpExporterRuntimeOptions &runtime_options,
    std::shared_ptr<opentelemetry::ext::http::client::HttpClient> http_client)
{
  return std::make_unique<OtlpJsonHttpExporter>(options, runtime_options, std::move(http_client));
}

std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> OtlpJsonHttpExporterFactory::Create(
    const OtlpHttpExporterOptions &options,
    const OtlpHttpExporterRuntimeOptions &runtime_options,
    std::shared_ptr<opentelemetry::ext::http::client::HttpClient> http_client,
    const std::shared_ptr<JsonWriterFactory> &json_writer_factory,
    const std::shared_ptr<JsonReaderFactory> &json_reader_factory)
{
  return std::make_unique<OtlpJsonHttpExporter>(options, runtime_options, std::move(http_client),
                                                json_writer_factory, json_reader_factory);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
