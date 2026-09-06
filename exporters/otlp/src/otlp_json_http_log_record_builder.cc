// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_http_log_record_builder.h"

#include <memory>
#include <utility>

#include "opentelemetry/exporters/otlp/detail/otlp_json_http_builder_options.h"
#include "opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h"
#include "opentelemetry/exporters/otlp/otlp_json_http_log_record_exporter_factory.h"
#include "opentelemetry/sdk/configuration/otlp_http_log_record_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_log_record_exporter_configuration.h"
#include "opentelemetry/sdk/configuration/registry.h"
#include "opentelemetry/sdk/logs/exporter.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

void OtlpJsonHttpLogRecordBuilder::Register(opentelemetry::sdk::configuration::Registry *registry)
{
  auto builder = std::make_unique<OtlpJsonHttpLogRecordBuilder>();
  registry->SetOtlpHttpLogRecordBuilder(std::move(builder));
}

std::unique_ptr<opentelemetry::sdk::logs::LogRecordExporter> OtlpJsonHttpLogRecordBuilder::Build(
    const opentelemetry::sdk::configuration::OtlpHttpLogRecordExporterConfiguration *model) const
{
  auto options = detail::MakeOtlpJsonHttpExporterOptions<OtlpHttpLogRecordExporterOptions>(*model);

  return OtlpJsonHttpLogRecordExporterFactory::Create(options);
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
