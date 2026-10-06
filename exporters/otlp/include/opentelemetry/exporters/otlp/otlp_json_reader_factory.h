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
 * one. Set it as json_reader_factory in the runtime options of an OTLP/JSON
 * HTTP exporter.
 *
 * Create() is called once per export response. One factory may be shared by
 * several exporters, and an exporter may export from several threads, so
 * Create() can be called concurrently and must be thread-safe.
 *
 * Create() must not throw: it is called from noexcept export paths, where an
 * escaping exception terminates the program. To signal a failure it returns
 * nullptr, and the export then fails, because a response left unread cannot
 * be reported as having landed.
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
