// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#ifdef ENABLE_JSON_READER_NLOHMANN
#  include "opentelemetry/exporters/otlp/otlp_json_reader_factory_nlohmann.h"
#else
#  include "opentelemetry/sdk/common/global_log_handler.h"
#endif

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

// Internal helper — not public API. Returns the built-in nlohmann factory
// when ENABLE_JSON_READER_NLOHMANN is defined; terminates with an error
// otherwise.
inline std::shared_ptr<JsonReaderFactory> GetDefaultJsonReaderFactory()
{
#ifdef ENABLE_JSON_READER_NLOHMANN
  static auto instance = std::make_shared<JsonReaderFactoryNlohmann>();
  return instance;
#else
  OTEL_INTERNAL_LOG_ERROR(
      "No default JSON reader backend is compiled in. "
      "Use the JsonReaderFactory constructor or factory overloads, "
      "or enable the nlohmann backend (OTELCPP_WITH_JSON_WRITER_NLOHMANN=ON).");
  std::terminate();
#endif
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
