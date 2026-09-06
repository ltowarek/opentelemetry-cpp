// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * Injection point for a consumer-supplied JsonReader backend, matching
 * JsonWriterFactory: a pure-virtual factory rather than a global setter,
 * with the nlohmann backend as the default a caller gets by not supplying
 * one.
 */
class OPENTELEMETRY_EXPORT JsonReaderFactory
{
public:
  JsonReaderFactory()                                     = default;
  JsonReaderFactory(const JsonReaderFactory &)            = delete;
  JsonReaderFactory(JsonReaderFactory &&)                 = delete;
  JsonReaderFactory &operator=(const JsonReaderFactory &) = delete;
  JsonReaderFactory &operator=(JsonReaderFactory &&)      = delete;
  virtual ~JsonReaderFactory()                            = default;

  virtual std::unique_ptr<JsonReader> Create() = 0;
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
