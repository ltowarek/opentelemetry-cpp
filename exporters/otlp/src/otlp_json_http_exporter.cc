// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_http_exporter.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/detail/default_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/detail/otlp_http_transport.h"
#include "opentelemetry/exporters/otlp/detail/otlp_json_http_client_options.h"
#include "opentelemetry/exporters/otlp/detail/otlp_json_http_send.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_runtime_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_partial_success.h"
#include "opentelemetry/exporters/otlp/otlp_json_span_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_json_trace_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/ext/http/client/detail/default_factory.h"
#include "opentelemetry/ext/http/client/http_client.h"
#include "opentelemetry/ext/http/client/http_client_factory.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/common/exporter_utils.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/span_limits.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

OtlpJsonHttpExporter::OtlpJsonHttpExporter() : OtlpJsonHttpExporter(OtlpHttpExporterOptions()) {}

OtlpJsonHttpExporter::OtlpJsonHttpExporter(const OtlpHttpExporterOptions &options)
    : OtlpJsonHttpExporter(options, OtlpHttpExporterRuntimeOptions())
{}

OtlpJsonHttpExporter::OtlpJsonHttpExporter(const OtlpHttpExporterOptions &options,
                                           const OtlpHttpExporterRuntimeOptions &runtime_options)
    : OtlpJsonHttpExporter(options,
                           runtime_options,
                           ext::http::client::detail::GetDefaultHttpClientFactory()->Create(
                               runtime_options.thread_instrumentation))
{}

OtlpJsonHttpExporter::OtlpJsonHttpExporter(
    const OtlpHttpExporterOptions &options,
    const OtlpHttpExporterRuntimeOptions &runtime_options,
    std::shared_ptr<ext::http::client::HttpClient> http_client)
    : OtlpJsonHttpExporter(options,
                           runtime_options,
                           std::move(http_client),
                           detail::GetDefaultJsonWriterFactory(),
                           detail::GetDefaultJsonReaderFactory())
{}

OtlpJsonHttpExporter::OtlpJsonHttpExporter(
    const OtlpHttpExporterOptions &options,
    const OtlpHttpExporterRuntimeOptions &runtime_options,
    std::shared_ptr<ext::http::client::HttpClient> http_client,
    const std::shared_ptr<JsonWriterFactory> &json_writer_factory,
    const std::shared_ptr<JsonReaderFactory> &json_reader_factory)
    : options_(options),
      transport_(std::make_unique<detail::OtlpHttpTransport>(
          detail::MakeOtlpJsonHttpClientOptions(options, runtime_options),
          std::move(http_client))),
      json_writer_factory_(json_writer_factory),
      json_reader_factory_(json_reader_factory)
{}

OtlpJsonHttpExporter::~OtlpJsonHttpExporter() = default;

// ----------------------------- Exporter methods ------------------------------

std::unique_ptr<opentelemetry::sdk::trace::Recordable>
OtlpJsonHttpExporter::MakeRecordable() noexcept
{
  auto recordable = std::make_unique<OtlpJsonSpanRecordable>();

  opentelemetry::sdk::trace::SpanLimits limits;
  limits.attribute_count_limit       = options_.max_attributes;
  limits.event_count_limit           = options_.max_events;
  limits.link_count_limit            = options_.max_links;
  limits.event_attribute_count_limit = options_.max_attributes_per_event;
  limits.link_attribute_count_limit  = options_.max_attributes_per_link;
  recordable->SetSpanLimits(limits);

  return recordable;
}

opentelemetry::sdk::common::ExportResult OtlpJsonHttpExporter::Export(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
        &spans) noexcept
{
  if (transport_->IsShutdown())
  {
    const std::size_t span_count = spans.size();
    OTEL_INTERNAL_LOG_ERROR("[OTLP TRACE HTTP Exporter] ERROR: Export "
                            << span_count << " trace span(s) failed, exporter is shutdown");
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  if (spans.empty())
  {
    return opentelemetry::sdk::common::ExportResult::kSuccess;
  }

  auto json_writer = json_writer_factory_->Create();
  ConvertSpansToJson(*json_writer, spans);
  if (!json_writer->ok())
  {
    OTEL_INTERNAL_LOG_ERROR("[OTLP TRACE HTTP Exporter] ERROR: Failed to serialize the request");
    return opentelemetry::sdk::common::ExportResult::kFailure;
  }

  return detail::SendOtlpJsonRequest(*transport_, json_reader_factory_,
                                     OtlpTracePartialSuccessSignal(), json_writer->ToString(),
                                     spans.size(), detail::MaxRunningRequests(options_));
}

bool OtlpJsonHttpExporter::ForceFlush(std::chrono::microseconds timeout) noexcept
{
  return transport_->ForceFlush(timeout);
}

bool OtlpJsonHttpExporter::Shutdown(std::chrono::microseconds timeout) noexcept
{
  return transport_->Shutdown(timeout);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
