// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

/**
 * One batch of recordables, grouped the way OTLP nests them: by resource, then
 * by instrumentation scope within each resource. Both levels keep first-seen
 * order, which is the order the protobuf path appends them in.
 */
template <typename Item>
struct ScopeGroup
{
  const sdk::instrumentationscope::InstrumentationScope *scope = nullptr;
  std::vector<const Item *> items;
};

template <typename Item>
struct ResourceGroup
{
  const sdk::resource::Resource *resource = nullptr;
  std::vector<ScopeGroup<Item>> scopes;
};

struct ScopePointerHasher
{
  std::size_t operator()(
      const sdk::instrumentationscope::InstrumentationScope *scope) const noexcept
  {
    return scope == nullptr ? 0 : scope->HashCode();
  }
};

struct ScopePointerEqual
{
  bool operator()(const sdk::instrumentationscope::InstrumentationScope *left,
                  const sdk::instrumentationscope::InstrumentationScope *right) const noexcept
  {
    if (left == right)
    {
      return true;
    }
    if (left == nullptr || right == nullptr)
    {
      return false;
    }
    return *left == *right;
  }
};

// A span recordable hands out its resource and scope by pointer, a log
// recordable by reference, so the grouping takes either.
template <typename T>
const T *AsPointer(const T *value) noexcept
{
  return value;
}

template <typename T>
const T *AsPointer(const T &value) noexcept
{
  return &value;
}

template <typename Item, typename Recordable>
std::vector<ResourceGroup<Item>> GroupByResourceAndScope(
    const nostd::span<std::unique_ptr<Recordable>> &recordables) noexcept
{
  std::vector<ResourceGroup<Item>> groups;
  // Indices into `groups` and into each group's scopes, so lookup is by hash
  // while the output order stays the order things were first seen.
  std::unordered_map<const sdk::resource::Resource *, std::size_t> resource_index;
  std::vector<std::unordered_map<const sdk::instrumentationscope::InstrumentationScope *,
                                 std::size_t, ScopePointerHasher, ScopePointerEqual>>
      scope_index;

  for (const auto &recordable : recordables)
  {
    if (recordable == nullptr)
    {
      continue;
    }
    const auto *item = static_cast<const Item *>(recordable.get());

    const auto *resource = AsPointer(item->GetResource());
    auto resource_slot   = resource_index.find(resource);
    if (resource_slot == resource_index.end())
    {
      resource_slot = resource_index.emplace(resource, groups.size()).first;
      groups.push_back(ResourceGroup<Item>{resource, {}});
      scope_index.emplace_back();
    }
    ResourceGroup<Item> &resource_group = groups[resource_slot->second];

    const auto *scope = AsPointer(item->GetInstrumentationScope());
    auto &scopes      = scope_index[resource_slot->second];
    auto scope_slot   = scopes.find(scope);
    if (scope_slot == scopes.end())
    {
      scope_slot = scopes.emplace(scope, resource_group.scopes.size()).first;
      resource_group.scopes.push_back(ScopeGroup<Item>{scope, {}});
    }
    resource_group.scopes[scope_slot->second].items.push_back(item);
  }

  return groups;
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
