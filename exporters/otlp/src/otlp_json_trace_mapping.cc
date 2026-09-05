// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_trace_mapping.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "opentelemetry/exporters/otlp/otlp_json_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_json_span_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
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

// The OTLP Span.SpanKind values. The SDK's SpanKind is a separate enumeration
// whose order does not match, so the two are mapped rather than cast.
enum class SpanKindValue : std::int32_t
{
  kUnspecified = 0,
  kInternal    = 1,
  kServer      = 2,
  kClient      = 3,
  kProducer    = 4,
  kConsumer    = 5,
};

SpanKindValue MapSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept
{
  switch (span_kind)
  {
    case opentelemetry::trace::SpanKind::kInternal:
      return SpanKindValue::kInternal;
    case opentelemetry::trace::SpanKind::kServer:
      return SpanKindValue::kServer;
    case opentelemetry::trace::SpanKind::kClient:
      return SpanKindValue::kClient;
    case opentelemetry::trace::SpanKind::kProducer:
      return SpanKindValue::kProducer;
    case opentelemetry::trace::SpanKind::kConsumer:
      return SpanKindValue::kConsumer;
    default:
      return SpanKindValue::kUnspecified;
  }
}

// Status.StatusCode, which happens to agree with the SDK's StatusCode, but is
// spelled out so a change on either side is a compile error rather than a
// silently wrong number on the wire.
std::int32_t MapStatusCode(opentelemetry::trace::StatusCode code) noexcept
{
  switch (code)
  {
    case opentelemetry::trace::StatusCode::kOk:
      return 1;
    case opentelemetry::trace::StatusCode::kError:
      return 2;
    case opentelemetry::trace::StatusCode::kUnset:
    default:
      return 0;
  }
}

/**
 * Writes an attributes array and its dropped count, the pair that every
 * message carrying attributes ends with. Both are omitted at their default,
 * an empty list and a zero count.
 */
void WriteAttributesAndDroppedCount(
    JsonWriter &writer,
    const OtlpJsonSpanRecordable::OrderedAttributes &attributes,
    std::uint32_t dropped_count) noexcept
{
  if (!attributes.empty())
  {
    writer.Key("attributes");
    writer.BeginArray();
    for (const auto &attribute : attributes)
    {
      json_mapping::WriteKeyValue(writer, attribute.first, attribute.second,
                                  json_mapping::AttributeMappingOptions{});
    }
    writer.EndArray();
  }
  if (dropped_count != 0)
  {
    writer.Key("droppedAttributesCount");
    writer.WriteUInt32(dropped_count);
  }
}

void WriteEvent(JsonWriter &writer, const OtlpJsonSpanRecordable::Event &event) noexcept
{
  writer.BeginObject();
  if (event.time_unix_nano != 0)
  {
    writer.Key("timeUnixNano");
    json_mapping::WriteUInt64String(writer, event.time_unix_nano);
  }
  if (!event.name.empty())
  {
    writer.Key("name");
    writer.WriteString(event.name);
  }
  WriteAttributesAndDroppedCount(writer, event.attributes, event.dropped_attributes_count);
  writer.EndObject();
}

void WriteLink(JsonWriter &writer, const OtlpJsonSpanRecordable::Link &link) noexcept
{
  writer.BeginObject();
  writer.Key("traceId");
  json_mapping::WriteHexId(writer, link.trace_id.Id().data(),
                           opentelemetry::trace::TraceId::kSize);
  writer.Key("spanId");
  json_mapping::WriteHexId(writer, link.span_id.Id().data(), opentelemetry::trace::SpanId::kSize);
  if (!link.trace_state.empty())
  {
    writer.Key("traceState");
    writer.WriteString(link.trace_state);
  }
  WriteAttributesAndDroppedCount(writer, link.attributes, link.dropped_attributes_count);
  writer.EndObject();
}

void WriteStatus(JsonWriter &writer, const OtlpJsonSpanRecordable::Status &status) noexcept
{
  const std::int32_t code = MapStatusCode(status.code);

  // A status that was set but says nothing -- the unset code, no message --
  // has no field set, and a message with no fields set is null, not {}.
  if (code == 0 && status.message.empty())
  {
    writer.WriteNull();
    return;
  }

  writer.BeginObject();
  if (!status.message.empty())
  {
    writer.Key("message");
    writer.WriteString(status.message);
  }
  if (code != 0)
  {
    writer.Key("code");
    writer.WriteInt32(code);
  }
  writer.EndObject();
}

void WriteSpan(JsonWriter &writer, const OtlpJsonSpanRecordable &span) noexcept
{
  writer.BeginObject();

  writer.Key("traceId");
  json_mapping::WriteHexId(writer, span.GetTraceId().Id().data(),
                           opentelemetry::trace::TraceId::kSize);
  writer.Key("spanId");
  json_mapping::WriteHexId(writer, span.GetSpanId().Id().data(),
                           opentelemetry::trace::SpanId::kSize);

  if (!span.GetTraceState().empty())
  {
    writer.Key("traceState");
    writer.WriteString(span.GetTraceState());
  }
  if (span.GetParentSpanId().IsValid())
  {
    writer.Key("parentSpanId");
    json_mapping::WriteHexId(writer, span.GetParentSpanId().Id().data(),
                             opentelemetry::trace::SpanId::kSize);
  }
  if (!span.GetName().empty())
  {
    writer.Key("name");
    writer.WriteString(span.GetName());
  }

  const SpanKindValue kind =
      span.HasSpanKind() ? MapSpanKind(span.GetSpanKind()) : SpanKindValue::kUnspecified;
  if (kind != SpanKindValue::kUnspecified)
  {
    writer.Key("kind");
    writer.WriteInt32(static_cast<std::int32_t>(kind));
  }

  if (span.GetStartTimeUnixNano() != 0)
  {
    writer.Key("startTimeUnixNano");
    json_mapping::WriteUInt64String(writer, span.GetStartTimeUnixNano());
  }
  if (span.GetEndTimeUnixNano() != 0)
  {
    writer.Key("endTimeUnixNano");
    json_mapping::WriteUInt64String(writer, span.GetEndTimeUnixNano());
  }

  WriteAttributesAndDroppedCount(writer, span.GetAttributes(), span.GetDroppedAttributesCount());

  if (!span.GetEvents().empty())
  {
    writer.Key("events");
    writer.BeginArray();
    for (const auto &event : span.GetEvents())
    {
      WriteEvent(writer, event);
    }
    writer.EndArray();
  }
  if (span.GetDroppedEventsCount() != 0)
  {
    writer.Key("droppedEventsCount");
    writer.WriteUInt32(span.GetDroppedEventsCount());
  }

  if (!span.GetLinks().empty())
  {
    writer.Key("links");
    writer.BeginArray();
    for (const auto &link : span.GetLinks())
    {
      WriteLink(writer, link);
    }
    writer.EndArray();
  }
  if (span.GetDroppedLinksCount() != 0)
  {
    writer.Key("droppedLinksCount");
    writer.WriteUInt32(span.GetDroppedLinksCount());
  }

  if (span.GetStatus().is_set)
  {
    writer.Key("status");
    WriteStatus(writer, span.GetStatus());
  }

  if (span.GetFlags() != 0)
  {
    writer.Key("flags");
    writer.WriteUInt32(span.GetFlags());
  }

  writer.EndObject();
}

/**
 * The spans of one batch, grouped the way OTLP nests them: by resource, then
 * by instrumentation scope within each resource. Both levels keep first-seen
 * order, which is the order the protobuf path appends them in.
 */
struct ScopeGroup
{
  const InstrumentationScope *scope = nullptr;
  std::vector<const OtlpJsonSpanRecordable *> spans;
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
  bool operator()(const InstrumentationScope *left, const InstrumentationScope *right) const noexcept
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

std::vector<ResourceGroup> GroupSpans(
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
        &spans) noexcept
{
  std::vector<ResourceGroup> groups;
  // Indices into `groups` and into each group's scopes, so lookup is by hash
  // while the output order stays the order things were first seen.
  std::unordered_map<const opentelemetry::sdk::resource::Resource *, std::size_t> resource_index;
  std::vector<std::unordered_map<const InstrumentationScope *, std::size_t, ScopePointerHasher,
                                 ScopePointerEqual>>
      scope_index;

  for (const auto &recordable : spans)
  {
    if (recordable == nullptr)
    {
      continue;
    }
    const auto *span = static_cast<const OtlpJsonSpanRecordable *>(recordable.get());

    const auto *resource = span->GetResource();
    auto resource_slot   = resource_index.find(resource);
    if (resource_slot == resource_index.end())
    {
      resource_slot = resource_index.emplace(resource, groups.size()).first;
      groups.push_back(ResourceGroup{resource, {}});
      scope_index.emplace_back();
    }
    ResourceGroup &resource_group = groups[resource_slot->second];

    const auto *scope = span->GetInstrumentationScope();
    auto &scopes      = scope_index[resource_slot->second];
    auto scope_slot   = scopes.find(scope);
    if (scope_slot == scopes.end())
    {
      scope_slot = scopes.emplace(scope, resource_group.scopes.size()).first;
      resource_group.scopes.push_back(ScopeGroup{scope, {}});
    }
    resource_group.scopes[scope_slot->second].spans.push_back(span);
  }

  return groups;
}

}  // namespace

void ConvertSpansToJson(
    JsonWriter &writer,
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
        &spans) noexcept
{
  const std::vector<ResourceGroup> groups = GroupSpans(spans);

  // An empty batch sets no field, and a message with no fields set is null.
  if (groups.empty())
  {
    writer.WriteNull();
    return;
  }

  writer.BeginObject();
  {
    writer.Key("resourceSpans");
    writer.BeginArray();
    for (const ResourceGroup &resource_group : groups)
    {
      writer.BeginObject();

      // A span recorded without a resource leaves the field absent rather
      // than emitting an empty one, the same as never calling mutable_resource.
      if (resource_group.resource != nullptr)
      {
        writer.Key("resource");
        json_mapping::WriteResource(writer, *resource_group.resource);
      }

      if (!resource_group.scopes.empty())
      {
        writer.Key("scopeSpans");
        writer.BeginArray();
        for (const ScopeGroup &scope_group : resource_group.scopes)
        {
          writer.BeginObject();
          if (scope_group.scope != nullptr)
          {
            writer.Key("scope");
            json_mapping::WriteInstrumentationScope(writer, *scope_group.scope);
          }
          if (!scope_group.spans.empty())
          {
            writer.Key("spans");
            writer.BeginArray();
            for (const OtlpJsonSpanRecordable *span : scope_group.spans)
            {
              WriteSpan(writer, *span);
            }
            writer.EndArray();
          }
          if (scope_group.scope != nullptr && !scope_group.scope->GetSchemaURL().empty())
          {
            writer.Key("schemaUrl");
            writer.WriteString(scope_group.scope->GetSchemaURL());
          }
          writer.EndObject();
        }
        writer.EndArray();
      }

      if (resource_group.resource != nullptr && !resource_group.resource->GetSchemaURL().empty())
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
