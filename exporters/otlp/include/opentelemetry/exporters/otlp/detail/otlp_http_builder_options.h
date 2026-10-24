// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

#include "opentelemetry/exporters/otlp/otlp_builder_utils.h"
#include "opentelemetry/exporters/otlp/otlp_http.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

/**
 * Maps an `otlp_http` configuration node onto exporter options. A template
 * because the three configuration models carry the same connection settings
 * under the same names, as the three option structs do, and because both
 * exporter families read the same node.
 *
 * `encoding` is left to the caller: the protobuf exporters turn it into a
 * content type, and the OTLP/JSON ones cannot honour it at all.
 */
template <typename ExporterOptions, typename ConfigurationModel>
ExporterOptions MakeOtlpHttpExporterOptions(const ConfigurationModel &model)
{
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
