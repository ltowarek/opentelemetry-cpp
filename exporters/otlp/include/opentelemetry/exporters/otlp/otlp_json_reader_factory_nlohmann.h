// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * The default JsonReader implementation, backed by nlohmann-json. Compiled
 * only when OTELCPP_WITH_JSON_WRITER_NLOHMANN is enabled — the reader and
 * the writer share one option because they share the backend library, and
 * a separate option would let a consumer exclude the writer backend while
 * still pulling nlohmann in for the reader.
 */
class OPENTELEMETRY_EXPORT JsonReaderFactoryNlohmann : public JsonReaderFactory
{
public:
  std::unique_ptr<JsonReader> Create() override;
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
