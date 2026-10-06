// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace test
{

// The read-side counterpart of StubJsonWriter: each method calls the matching on_ member, so a
// test replaces only the behavior it needs. By default the document parses, holds no keys and so
// reports no partial success, which is what an empty 2xx body means.
class StubJsonReader : public JsonReader
{
public:
  bool Parse(nostd::string_view document) noexcept override { return on_parse(document); }
  bool EnterObject(nostd::string_view key) noexcept override { return on_enter_object(key); }
  void LeaveObject() noexcept override { on_leave_object(); }
  bool GetInt64(nostd::string_view key, std::int64_t &value) noexcept override
  {
    return on_get_int64(key, value);
  }
  bool GetString(nostd::string_view key, std::string &value) noexcept override
  {
    return on_get_string(key, value);
  }

  std::function<bool(nostd::string_view)> on_parse        = [](nostd::string_view) { return true; };
  std::function<bool(nostd::string_view)> on_enter_object = [](nostd::string_view) {
    return false;
  };
  std::function<void()> on_leave_object = [] {};
  std::function<bool(nostd::string_view, std::int64_t &)> on_get_int64 =
      [](nostd::string_view, std::int64_t &) { return false; };
  std::function<bool(nostd::string_view, std::string &)> on_get_string =
      [](nostd::string_view, std::string &) { return false; };
};

// Creates readers with the given function, so a test can inject any reader, or nullptr.
class StubJsonReaderFactory : public JsonReaderFactory
{
public:
  explicit StubJsonReaderFactory(std::function<std::unique_ptr<JsonReader>()> create =
                                     [] { return std::make_unique<StubJsonReader>(); })
      : create_(std::move(create))
  {}

  std::unique_ptr<JsonReader> Create() override { return create_(); }

private:
  std::function<std::unique_ptr<JsonReader>()> create_;
};

}  // namespace test

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
