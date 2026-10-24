// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/detail/otlp_http_builder_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_builder_utils.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_span_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_span_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

void OtlpHttpSpanBuilder::Register(opentelemetry::sdk::configuration::Registry *registry)
{
  auto builder = std::make_unique<OtlpHttpSpanBuilder>();
  registry->SetOtlpHttpSpanBuilder(std::move(builder));
}

std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> OtlpHttpSpanBuilder::Build(
    const opentelemetry::sdk::configuration::OtlpHttpSpanExporterConfiguration *model) const
{
  auto options = detail::MakeOtlpHttpExporterOptions<OtlpHttpExporterOptions>(*model);

  options.content_type = OtlpHttpBuilderUtils::ConvertOtlpHttpEncoding(model->encoding);

  return OtlpHttpExporterFactory::Create(options);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
