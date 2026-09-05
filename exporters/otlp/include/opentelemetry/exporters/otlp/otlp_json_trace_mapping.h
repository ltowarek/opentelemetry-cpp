// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * Writes the OTLP/JSON ExportTraceServiceRequest for `spans`, reading the
 * recorded span data directly and never referencing protobuf.
 *
 * This does in one step what the protobuf path does in two -- grouping spans
 * into resource and scope batches, then converting the resulting message
 * through reflection -- and produces the same bytes for the same spans. The
 * grouping is by resource identity and scope equality, in first-seen order, so
 * a batch always encodes the same way.
 *
 * Every recordable must be an OtlpJsonSpanRecordable, which is what
 * MakeRecordable on the exporters using this mapping hands out.
 */
void ConvertSpansToJson(
    JsonWriter &writer,
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
        &spans) noexcept;

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
