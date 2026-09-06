// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_reader_factory_nlohmann.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/nostd/string_view.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

// Parses a decimal integer the way OTLP/JSON writes a 64-bit field, which
// is as a string. The digits are validated before std::strtoll sees them,
// because strtoll would otherwise silently accept leading whitespace, a
// "0x" prefix and trailing characters; strtoll is then left to do the one
// thing that check cannot, which is reject a value outside the 64-bit
// range.
bool ParseDecimalInt64(const std::string &text, std::int64_t &value) noexcept
{
  if (text.empty())
  {
    return false;
  }
  const std::size_t first_digit = (text[0] == '-' || text[0] == '+') ? 1 : 0;
  if (first_digit >= text.size())
  {
    return false;
  }
  for (std::size_t i = first_digit; i < text.size(); ++i)
  {
    if (text[i] < '0' || text[i] > '9')
    {
      return false;
    }
  }

  errno                  = 0;
  const long long parsed = std::strtoll(text.c_str(), nullptr, 10);
  if (errno == ERANGE)
  {
    return false;
  }
  value = static_cast<std::int64_t>(parsed);
  return true;
}

// A JsonReader over an nlohmann::json document. Every lookup goes through
// find() and an is_*() check rather than at() or operator[], and parsing
// uses the non-throwing overload, so nlohmann is never asked to do
// anything it would answer with an exception. This is what lets every
// method stay noexcept in a build with exceptions disabled.
class NlohmannJsonReader final : public JsonReader
{
public:
  bool Parse(nostd::string_view document) noexcept override
  {
    stack_.clear();
    root_ = nlohmann::json::parse(document.begin(), document.end(), nullptr, false);
    if (root_.is_discarded() || !root_.is_object())
    {
      root_ = nlohmann::json::value_t::discarded;
      return false;
    }
    stack_.push_back(&root_);
    return true;
  }

  bool EnterObject(nostd::string_view key) noexcept override
  {
    const nlohmann::json *member = Find(key);
    if (member == nullptr || !member->is_object())
    {
      return false;
    }
    stack_.push_back(member);
    return true;
  }

  void LeaveObject() noexcept override
  {
    if (stack_.size() > 1)
    {
      stack_.pop_back();
    }
  }

  bool GetInt64(nostd::string_view key, std::int64_t &value) noexcept override
  {
    const nlohmann::json *member = Find(key);
    if (member == nullptr)
    {
      return false;
    }
    if (member->is_string())
    {
      return ParseDecimalInt64(member->get_ref<const std::string &>(), value);
    }
    if (member->is_number_integer())
    {
      // An unsigned value past the signed range parses as a number but has
      // no int64 to be read into.
      if (member->is_number_unsigned() &&
          member->get<std::uint64_t>() >
              static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
      {
        return false;
      }
      value = member->get<std::int64_t>();
      return true;
    }
    return false;
  }

  bool GetString(nostd::string_view key, std::string &value) noexcept override
  {
    const nlohmann::json *member = Find(key);
    if (member == nullptr || !member->is_string())
    {
      return false;
    }
    value = member->get_ref<const std::string &>();
    return true;
  }

private:
  const nlohmann::json *Find(nostd::string_view key) noexcept
  {
    if (stack_.empty())
    {
      return nullptr;
    }
    const auto it = stack_.back()->find(std::string(key.data(), key.size()));
    return it == stack_.back()->end() ? nullptr : &(*it);
  }

  nlohmann::json root_{nlohmann::json::value_t::discarded};
  // Ancestors of the current object, innermost last. Empty until Parse()
  // succeeds; the root is never popped.
  std::vector<const nlohmann::json *> stack_;
};

}  // namespace

std::unique_ptr<JsonReader> JsonReaderFactoryNlohmann::Create()
{
  return std::unique_ptr<JsonReader>(new NlohmannJsonReader());
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
