// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

#include "opentelemetry/exporters/otlp/otlp_builder_utils.h"
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
 * Maps an `otlp_http` configuration node onto the exporter options the
 * protobuf-free exporters take.
 *
 * A template because the three configuration models carry the same connection
 * settings under the same names, as the three exporter option structs do;
 * there is one mapping between them, not three. The signal-specific part --
 * the metric temporality preference -- stays with its own builder.
 *
 * `encoding` is the one field these builders cannot honour: they emit
 * OTLP/JSON whatever it says. A node asking for protobuf gets a warning and a
 * working JSON exporter, which is how a builder reports a field it must ignore
 * elsewhere in the tree, and what the exporters' own documentation already
 * promises for `content_type`. `content_type` is deliberately not set: the
 * exporters hardcode JSON when they build their transport options, so
 * assigning it here would read as a choice that isn't one.
 *
 * Separate from the builders so that what the mapping produces can be asserted
 * directly. Nothing else can observe it -- an exporter exposes no options --
 * and "no configured field is silently dropped" is the property that makes
 * these builders a safe substitute for the protobuf ones.
 */
template <typename ExporterOptions, typename ConfigurationModel>
ExporterOptions MakeOtlpJsonHttpExporterOptions(const ConfigurationModel &model,
                                                nostd::string_view log_prefix)
{
  if (model.encoding == opentelemetry::sdk::configuration::OtlpHttpEncoding::protobuf)
  {
    OTEL_INTERNAL_LOG_WARN(log_prefix << " encoding protobuf is not supported by this exporter, "
                                         "exporting json instead");
  }

  // The nullptr overload skips the OTEL_EXPORTER_OTLP_* environment defaults, so the
  // configuration file is the only thing that decides what the exporter does.
  ExporterOptions options(nullptr);

  const auto *tls = model.tls.get();

  options.url                = model.endpoint;
  options.json_bytes_mapping = JsonBytesMappingKind::kHexId;
  options.use_json_name      = false;
  options.console_debug      = false;
  options.timeout            = std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::seconds{model.timeout});
  options.http_headers =
      OtlpBuilderUtils::ConvertHeadersConfigurationModel(model.headers.get(), model.headers_list);
  options.ssl_insecure_skip_verify = false;

  if (tls != nullptr)
  {
    options.ssl_ca_cert_path     = tls->ca_file;
    options.ssl_client_key_path  = tls->key_file;
    options.ssl_client_cert_path = tls->cert_file;
  }

  options.compression = model.compression;

  return options;
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
