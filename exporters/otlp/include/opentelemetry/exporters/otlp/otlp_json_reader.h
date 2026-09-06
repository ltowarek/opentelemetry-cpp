// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * A minimal JSON accessor, expressed as a document with a cursor rather
 * than as a token stream. The asymmetry with JsonWriter is deliberate:
 * the writer emits unbounded telemetry batches, where a streaming token
 * vocabulary is what keeps peak memory bounded by output size, while the
 * reader consumes an export response whose entire payload is a rejected
 * count and an error message. A pull parser for two fields would be cost
 * with no corresponding benefit, and every backend worth plugging in here
 * already offers a document API.
 *
 * The cursor starts at the root object after a successful Parse().
 * EnterObject() descends into a member object and LeaveObject() returns to
 * the parent, so a reader instance never hands out node handles — node
 * identity and ownership are exactly what an abstraction across JSON
 * libraries cannot agree on.
 *
 * The interface carries no OTLP awareness. That OTLP/JSON may encode a
 * 64-bit integer either as a JSON number or as a decimal string is a
 * property of JSON's own number handling rather than of OTLP, so GetInt64
 * accepts both; which key holds a rejected count is the caller's business.
 *
 * Every method is noexcept. A malformed document, an absent key or a key
 * of the wrong type is reported by a false return, never by an exception,
 * which is required for builds with exceptions disabled. There is no
 * sticky failure flag: unlike a write, where a failure mid-document
 * invalidates everything after it, each read stands alone.
 */
class JsonReader
{
public:
  JsonReader()                              = default;
  JsonReader(const JsonReader &)            = delete;
  JsonReader(JsonReader &&)                 = delete;
  JsonReader &operator=(const JsonReader &) = delete;
  JsonReader &operator=(JsonReader &&)      = delete;
  virtual ~JsonReader()                     = default;

  // Parses a document and positions the cursor at its root. Returns false
  // if the text is not valid JSON or its root is not an object, leaving no
  // document loaded. Parsing again discards whatever was loaded before.
  virtual bool Parse(nostd::string_view document) noexcept = 0;

  // Descends into the object held by `key` in the current object. Returns
  // false, leaving the cursor where it was, if the key is absent or does
  // not hold an object.
  virtual bool EnterObject(nostd::string_view key) noexcept = 0;

  // Returns to the parent of the object entered last. No effect at the
  // root.
  virtual void LeaveObject() noexcept = 0;

  // Reads `key` from the current object as a 64-bit integer, accepting
  // both a JSON number and a decimal string. Returns false, leaving `value`
  // untouched, if the key is absent, holds another type, or holds a number
  // that is not an integer representable in 64 bits.
  virtual bool GetInt64(nostd::string_view key, std::int64_t &value) noexcept = 0;

  // Reads `key` from the current object as a string. Returns false,
  // leaving `value` untouched, if the key is absent or holds another type.
  virtual bool GetString(nostd::string_view key, std::string &value) noexcept = 0;
};

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
