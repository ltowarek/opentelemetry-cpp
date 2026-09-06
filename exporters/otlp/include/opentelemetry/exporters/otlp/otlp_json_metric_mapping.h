// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

/**
 * Writes the OTLP/JSON ExportMetricsServiceRequest for `data`, reading the SDK
 * metric data model directly and never referencing protobuf.
 *
 * Metrics need no recordable and no grouping pass: a ResourceMetrics already
 * carries its resource and its scopes in the shape OTLP nests them in, so this
 * walks it as it stands and produces the same bytes the protobuf path does for
 * the same input.
 *
 * Resource, scope and attribute mapping is shared with traces through
 * json_mapping; only the point types are new here. Sums and gauges carry
 * NumberDataPoints, histograms carry bucket counts and bounds, and exponential
 * histograms carry their two bucket runs.
 *
 * The integer/double split is per field rather than per value: counts, bucket
 * counts and an integer point value are 64-bit and so decimal strings, while a
 * histogram's sum, min and max are `double` on the wire even when the SDK
 * recorded them as int64.
 */
void ConvertMetricsToJson(JsonWriter &writer,
                          const opentelemetry::sdk::metrics::ResourceMetrics &data) noexcept;

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
