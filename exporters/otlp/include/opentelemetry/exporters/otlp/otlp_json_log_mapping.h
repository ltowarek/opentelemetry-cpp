// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/sdk/logs/recordable.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * Writes the OTLP/JSON ExportLogsServiceRequest for `logs`, reading the
 * recorded log data directly and never referencing protobuf.
 *
 * This does in one step what the protobuf path does in two -- grouping records
 * into resource and scope batches, then converting the resulting message
 * through reflection -- and produces the same bytes for the same records. The
 * grouping is by resource identity and scope equality, in first-seen order, so
 * a batch always encodes the same way.
 *
 * Resource, scope and attribute mapping is shared with traces through
 * json_mapping. Unlike a span, a log record always has both a resource and a
 * scope -- a recordable that was given neither reports the SDK's defaults --
 * so both keys are always on the wire, encoding as null when the default
 * carries nothing.
 *
 * Every recordable must be an OtlpJsonLogRecordable, which is what
 * MakeRecordable on the exporters using this mapping hands out.
 */
void ConvertLogsToJson(
    JsonWriter &writer,
    const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::logs::Recordable>>
        &logs) noexcept;

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
