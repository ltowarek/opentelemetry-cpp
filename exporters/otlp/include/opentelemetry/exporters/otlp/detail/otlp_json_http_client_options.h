// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include "opentelemetry/exporters/otlp/otlp_http.h"
#include "opentelemetry/exporters/otlp/otlp_http_client.h"
#include "opentelemetry/sdk/common/thread_instrumentation.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

/**
 * Builds the transport's options from a signal's exporter options.
 *
 * A template because the three OTLP/HTTP exporter option structs are separate
 * types carrying the same connection settings under the same names; there is
 * one mapping between them, not three.
 *
 * The content type is not read: these exporters emit OTLP/JSON and nothing
 * else, so a caller that left the default binary setting in place still gets
 * JSON rather than an empty body.
 */
template <typename ExporterOptions, typename RuntimeOptions>
OtlpHttpClientOptions MakeOtlpJsonHttpClientOptions(const ExporterOptions &options,
                                                    const RuntimeOptions &runtime_options)
{
  OtlpHttpClientOptions client_options(
      options.url, options.ssl_insecure_skip_verify, options.ssl_ca_cert_path,
      options.ssl_ca_cert_string, options.ssl_client_key_path, options.ssl_client_key_string,
      options.ssl_client_cert_path, options.ssl_client_cert_string, options.ssl_min_tls,
      options.ssl_max_tls, options.ssl_cipher, options.ssl_cipher_suite,
      HttpRequestContentType::kJson, options.json_bytes_mapping, options.compression,
      options.use_json_name, options.console_debug, options.timeout, options.http_headers,
      options.retry_policy_max_attempts, options.retry_policy_initial_backoff,
      options.retry_policy_max_backoff, options.retry_policy_backoff_multiplier,
      runtime_options.thread_instrumentation
#ifdef ENABLE_ASYNC_EXPORT
      ,
      options.max_concurrent_requests, options.max_requests_per_connection
#endif
  );
  return client_options;
}

/**
 * How many requests the exporter lets run at once, which is 0 -- meaning
 * export synchronously -- unless the asynchronous preview is compiled in. The
 * option carrying it only exists under that flag, so reading it is guarded
 * once here rather than in each exporter.
 */
template <typename ExporterOptions>
std::size_t MaxRunningRequests(const ExporterOptions &options) noexcept
{
#ifdef ENABLE_ASYNC_EXPORT
  return options.max_concurrent_requests;
#else
  static_cast<void>(options);
  return 0;
#endif
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
