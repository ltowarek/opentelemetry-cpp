// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/exporters/otlp/detail/otlp_http_builder_options.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/configuration/otlp_http_encoding.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

/**
 * The OTLP/JSON half of the mapping: `encoding` is the one field these
 * exporters cannot honour, since they emit OTLP/JSON whatever it says, so a
 * node asking for protobuf is warned about and exported as JSON.
 * `content_type` is not set because the exporters hardcode JSON when they
 * build their transport options.
 */
template <typename ExporterOptions, typename ConfigurationModel>
ExporterOptions MakeOtlpJsonHttpExporterOptions(const ConfigurationModel &model)
{
  if (model.encoding == opentelemetry::sdk::configuration::OtlpHttpEncoding::protobuf)
  {
    OTEL_INTERNAL_LOG_WARN(
        "[Otlp Json Http Exporter] encoding protobuf is not supported by "
        "this exporter, exporting json instead");
  }

  return MakeOtlpHttpExporterOptions<ExporterOptions>(model);
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
