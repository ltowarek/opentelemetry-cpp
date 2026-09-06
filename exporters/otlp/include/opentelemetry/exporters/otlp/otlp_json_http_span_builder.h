// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/sdk/configuration/otlp_http_span_exporter_builder.h"
#include "opentelemetry/sdk/configuration/otlp_http_span_exporter_configuration.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace sdk
{
namespace configuration
{
class Registry;
}  // namespace configuration
}  // namespace sdk

namespace exporter
{
namespace otlp
{

/**
 * Builds the protobuf-free OTLP/JSON exporter from an `otlp_http`
 * configuration node.
 *
 * An application registers this **instead of** OtlpHttpSpanBuilder, not alongside it:
 * both fill the one registry slot for this transport and signal, and which
 * implementation fills it is an application decision made at registration
 * time, the same way HTTP, gRPC and file are chosen today.
 *
 * `encoding: protobuf` is warned about and then exported as JSON, because that
 * is the only thing this exporter can do. See MakeOtlpJsonHttpExporterOptions.
 */
class OPENTELEMETRY_EXPORT OtlpJsonHttpSpanBuilder
    : public opentelemetry::sdk::configuration::OtlpHttpSpanExporterBuilder
{
public:
  static void Register(opentelemetry::sdk::configuration::Registry *registry);

  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> Build(
      const opentelemetry::sdk::configuration::OtlpHttpSpanExporterConfiguration *model)
      const override;
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
