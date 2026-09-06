// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_log_mapping.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

#include "opentelemetry/exporters/otlp/otlp_json_log_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_json_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

using opentelemetry::sdk::instrumentationscope::InstrumentationScope;

// Whether the record has any field set at all. A record nothing was ever put
// into -- which the SDK does emit, since a logger with no data still hands the
// processor a recordable -- is a message with no fields set, and so encodes as
// null rather than as an empty object.
bool HasAnyField(const OtlpJsonLogRecordable &record) noexcept
{
  return record.GetTimestamp() != 0 || record.GetSeverityNumber() != 0 ||
         !record.GetSeverityText().empty() || record.HasBody() ||
         !record.GetAttributes().empty() || record.GetDroppedAttributesCount() != 0 ||
         record.GetFlags() != 0 || record.GetTraceId().IsValid() || record.GetSpanId().IsValid() ||
         record.GetObservedTimestamp() != 0 || !record.GetEventName().empty();
}

// Fields go out in LogRecord's proto field-number order, which is not the
// order the message reads in: observedTimeUnixNano is field 11 and so follows
// spanId. The nlohmann backend sorts object keys, so no test can catch a
// departure from this; an order-preserving backend would.
void WriteLogRecord(JsonWriter &writer, const OtlpJsonLogRecordable &record) noexcept
{
  if (!HasAnyField(record))
  {
    writer.WriteNull();
    return;
  }

  writer.BeginObject();

  if (record.GetTimestamp() != 0)
  {
    writer.Key("timeUnixNano");
    json_mapping::WriteUInt64String(writer, record.GetTimestamp());
  }
  if (record.GetSeverityNumber() != 0)
  {
    writer.Key("severityNumber");
    writer.WriteInt32(record.GetSeverityNumber());
  }
  if (!record.GetSeverityText().empty())
  {
    writer.Key("severityText");
    writer.WriteString(record.GetSeverityText());
  }
  if (record.HasBody())
  {
    writer.Key("body");
    json_mapping::WriteAnyValue(writer, record.GetBody());
  }

  json_mapping::WriteAttributes(writer, record.GetAttributes());
  if (record.GetDroppedAttributesCount() != 0)
  {
    writer.Key("droppedAttributesCount");
    writer.WriteUInt32(record.GetDroppedAttributesCount());
  }

  if (record.GetFlags() != 0)
  {
    writer.Key("flags");
    writer.WriteUInt32(record.GetFlags());
  }

  // An invalid id is all zero bytes, which the protobuf path never assigns and
  // clears if it is handed one.
  if (record.GetTraceId().IsValid())
  {
    writer.Key("traceId");
    json_mapping::WriteHexId(writer, record.GetTraceId().Id().data(),
                             opentelemetry::trace::TraceId::kSize);
  }
  if (record.GetSpanId().IsValid())
  {
    writer.Key("spanId");
    json_mapping::WriteHexId(writer, record.GetSpanId().Id().data(),
                             opentelemetry::trace::SpanId::kSize);
  }

  if (record.GetObservedTimestamp() != 0)
  {
    writer.Key("observedTimeUnixNano");
    json_mapping::WriteUInt64String(writer, record.GetObservedTimestamp());
  }
  if (!record.GetEventName().empty())
  {
    writer.Key("eventName");
    writer.WriteString(record.GetEventName());
  }

  writer.EndObject();
}

/**
 * The records of one batch, grouped the way OTLP nests them: by resource, then
 * by instrumentation scope within each resource. Both levels keep first-seen
 * order, which is the order the protobuf path appends them in.
 */
struct ScopeGroup
{
  const InstrumentationScope *scope = nullptr;
  std::vector<const OtlpJsonLogRecordable *> records;
};

struct ResourceGroup
{
  const opentelemetry::sdk::resource::Resource *resource = nullptr;
  std::vector<ScopeGroup> scopes;
};

struct ScopePointerHasher
{
  std::size_t operator()(const InstrumentationScope *scope) const noexcept
  {
    return scope == nullptr ? 0 : scope->HashCode();
  }
};

struct ScopePointerEqual
{
  bool operator()(const InstrumentationScope *left,
                  const InstrumentationScope *right) const noexcept
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

std::vector<ResourceGroup> GroupLogs(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>>
        &logs) noexcept
{
  std::vector<ResourceGroup> groups;
  // Indices into `groups` and into each group's scopes, so lookup is by hash
  // while the output order stays the order things were first seen.
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, std::size_t> resource_index;
  std::vector<std::unordered_map<const InstrumentationScope *, std::size_t, ScopePointerHasher,
                                 ScopePointerEqual>>
      scope_index;

  for (const auto &recordable : logs)
  {
    if (recordable == nullptr)
    {
      continue;
    }
    const auto *record = static_cast<const OtlpJsonLogRecordable *>(recordable.get());

    const auto *resource = &record->GetResource();
    auto resource_slot   = resource_index.find(resource);
    if (resource_slot == resource_index.end())
    {
      resource_slot = resource_index.emplace(resource, groups.size()).first;
      groups.push_back(ResourceGroup{resource, {}});
      scope_index.emplace_back();
    }
    ResourceGroup &resource_group = groups[resource_slot->second];

    const auto *scope = &record->GetInstrumentationScope();
    auto &scopes      = scope_index[resource_slot->second];
    auto scope_slot   = scopes.find(scope);
    if (scope_slot == scopes.end())
    {
      scope_slot = scopes.emplace(scope, resource_group.scopes.size()).first;
      resource_group.scopes.push_back(ScopeGroup{scope, {}});
    }
    resource_group.scopes[scope_slot->second].records.push_back(record);
  }

  return groups;
}

}  // namespace

void ConvertLogsToJson(
    JsonWriter &writer,
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>>
        &logs) noexcept
{
  const std::vector<ResourceGroup> groups = GroupLogs(logs);

  // An empty batch sets no field, and a message with no fields set is null.
  if (groups.empty())
  {
    writer.WriteNull();
    return;
  }

  writer.BeginObject();
  {
    writer.Key("resourceLogs");
    writer.BeginArray();
    for (const ResourceGroup &resource_group : groups)
    {
      writer.BeginObject();

      writer.Key("resource");
      json_mapping::WriteResource(writer, *resource_group.resource);

      if (!resource_group.scopes.empty())
      {
        writer.Key("scopeLogs");
        writer.BeginArray();
        for (const ScopeGroup &scope_group : resource_group.scopes)
        {
          writer.BeginObject();
          writer.Key("scope");
          json_mapping::WriteInstrumentationScope(writer, *scope_group.scope);
          if (!scope_group.records.empty())
          {
            writer.Key("logRecords");
            writer.BeginArray();
            for (const OtlpJsonLogRecordable *record : scope_group.records)
            {
              WriteLogRecord(writer, *record);
            }
            writer.EndArray();
          }
          if (!scope_group.scope->GetSchemaURL().empty())
          {
            writer.Key("schemaUrl");
            writer.WriteString(scope_group.scope->GetSchemaURL());
          }
          writer.EndObject();
        }
        writer.EndArray();
      }

      if (!resource_group.resource->GetSchemaURL().empty())
      {
        writer.Key("schemaUrl");
        writer.WriteString(resource_group.resource->GetSchemaURL());
      }

      writer.EndObject();
    }
    writer.EndArray();
  }
  writer.EndObject();
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
