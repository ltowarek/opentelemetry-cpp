// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_metric_mapping.h"

#include <cstdint>
#include <string>
#include <vector>

#include "opentelemetry/exporters/otlp/otlp_json_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/metrics/data/circular_buffer.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{

namespace
{

namespace metric_sdk = opentelemetry::sdk::metrics;

// The OTLP AggregationTemporality values. The SDK's enumeration is a separate
// one, so the two are mapped rather than cast.
std::int32_t MapAggregationTemporality(
    metric_sdk::AggregationTemporality aggregation_temporality) noexcept
{
  switch (aggregation_temporality)
  {
    case metric_sdk::AggregationTemporality::kDelta:
      return 1;
    case metric_sdk::AggregationTemporality::kCumulative:
      return 2;
    case metric_sdk::AggregationTemporality::kUnspecified:
    default:
      return 0;
  }
}

/** A ValueType read as a double, which is what the wire type of sum/min/max is. */
double AsDouble(const metric_sdk::ValueType &value) noexcept
{
  if (nostd::holds_alternative<std::int64_t>(value))
  {
    return static_cast<double>(nostd::get<std::int64_t>(value));
  }
  return nostd::get<double>(value);
}

void WriteUInt64Array(JsonWriter &writer,
                      nostd::string_view key,
                      const std::vector<std::uint64_t> &values) noexcept
{
  if (values.empty())
  {
    return;
  }
  writer.Key(key);
  writer.BeginArray();
  for (const std::uint64_t value : values)
  {
    json_mapping::WriteUInt64String(writer, value);
  }
  writer.EndArray();
}

/**
 * Writes the two timestamps every data point starts with. Both come from the
 * MetricData rather than the point, because the SDK stamps a collection once
 * and every point in it shares that window.
 */
void WriteDataPointTimestamps(JsonWriter &writer,
                              std::uint64_t start_time_unix_nano,
                              std::uint64_t time_unix_nano) noexcept
{
  if (start_time_unix_nano != 0)
  {
    writer.Key("startTimeUnixNano");
    json_mapping::WriteUInt64String(writer, start_time_unix_nano);
  }
  if (time_unix_nano != 0)
  {
    writer.Key("timeUnixNano");
    json_mapping::WriteUInt64String(writer, time_unix_nano);
  }
}

/** The NumberDataPoint that both sums and gauges carry. */
void WriteNumberDataPoint(JsonWriter &writer,
                          const metric_sdk::PointAttributes &attributes,
                          const metric_sdk::ValueType &value,
                          std::uint64_t start_time_unix_nano,
                          std::uint64_t time_unix_nano) noexcept
{
  writer.BeginObject();
  WriteDataPointTimestamps(writer, start_time_unix_nano, time_unix_nano);

  // The value is a oneof, so it is on the wire even when it holds zero.
  if (nostd::holds_alternative<std::int64_t>(value))
  {
    writer.Key("asInt");
    writer.WriteString(std::to_string(nostd::get<std::int64_t>(value)));
  }
  else
  {
    writer.Key("asDouble");
    writer.WriteDouble(nostd::get<double>(value));
  }

  json_mapping::WriteAttributes(writer, attributes);
  writer.EndObject();
}

void WriteHistogramDataPoint(JsonWriter &writer,
                             const metric_sdk::PointAttributes &attributes,
                             const metric_sdk::HistogramPointData &point,
                             std::uint64_t start_time_unix_nano,
                             std::uint64_t time_unix_nano) noexcept
{
  writer.BeginObject();
  WriteDataPointTimestamps(writer, start_time_unix_nano, time_unix_nano);

  if (point.count_ != 0)
  {
    writer.Key("count");
    json_mapping::WriteUInt64String(writer, point.count_);
  }

  // sum, min and max have explicit presence, so a recorded zero is still on
  // the wire; what decides min and max is whether they were recorded at all.
  writer.Key("sum");
  writer.WriteDouble(AsDouble(point.sum_));

  WriteUInt64Array(writer, "bucketCounts", point.counts_);

  if (!point.boundaries_.empty())
  {
    writer.Key("explicitBounds");
    writer.BeginArray();
    for (const double boundary : point.boundaries_)
    {
      writer.WriteDouble(boundary);
    }
    writer.EndArray();
  }

  json_mapping::WriteAttributes(writer, attributes);

  if (point.record_min_max_)
  {
    writer.Key("min");
    writer.WriteDouble(AsDouble(point.min_));
    writer.Key("max");
    writer.WriteDouble(AsDouble(point.max_));
  }

  writer.EndObject();
}

void WriteBuckets(JsonWriter &writer,
                  const metric_sdk::AdaptingCircularBufferCounter &buckets) noexcept
{
  writer.BeginObject();
  if (buckets.StartIndex() != 0)
  {
    writer.Key("offset");
    writer.WriteInt32(buckets.StartIndex());
  }
  writer.Key("bucketCounts");
  writer.BeginArray();
  for (std::int32_t index = buckets.StartIndex(); index <= buckets.EndIndex(); ++index)
  {
    json_mapping::WriteUInt64String(writer, buckets.Get(index));
  }
  writer.EndArray();
  writer.EndObject();
}

void WriteExponentialHistogramDataPoint(JsonWriter &writer,
                                        const metric_sdk::PointAttributes &attributes,
                                        const metric_sdk::Base2ExponentialHistogramPointData &point,
                                        std::uint64_t start_time_unix_nano,
                                        std::uint64_t time_unix_nano) noexcept
{
  // A point that carries neither bucket run is emitted as the timestamps
  // alone: it is still a point in the series, but there is nothing in it to
  // describe.
  const bool has_buckets = point.positive_buckets_ != nullptr || point.negative_buckets_ != nullptr;
  if (!has_buckets)
  {
    if (start_time_unix_nano == 0 && time_unix_nano == 0)
    {
      writer.WriteNull();
      return;
    }
    writer.BeginObject();
    WriteDataPointTimestamps(writer, start_time_unix_nano, time_unix_nano);
    writer.EndObject();
    return;
  }

  writer.BeginObject();
  json_mapping::WriteAttributes(writer, attributes);
  WriteDataPointTimestamps(writer, start_time_unix_nano, time_unix_nano);

  if (point.count_ != 0)
  {
    writer.Key("count");
    json_mapping::WriteUInt64String(writer, point.count_);
  }

  writer.Key("sum");
  writer.WriteDouble(point.sum_);

  if (point.scale_ != 0)
  {
    writer.Key("scale");
    writer.WriteInt32(point.scale_);
  }
  if (point.zero_count_ != 0)
  {
    writer.Key("zeroCount");
    json_mapping::WriteUInt64String(writer, point.zero_count_);
  }

  if (point.positive_buckets_ != nullptr && !point.positive_buckets_->Empty())
  {
    writer.Key("positive");
    WriteBuckets(writer, *point.positive_buckets_);
  }
  if (point.negative_buckets_ != nullptr && !point.negative_buckets_->Empty())
  {
    writer.Key("negative");
    WriteBuckets(writer, *point.negative_buckets_);
  }

  if (point.record_min_max_)
  {
    writer.Key("min");
    writer.WriteDouble(point.min_);
    writer.Key("max");
    writer.WriteDouble(point.max_);
  }

  writer.EndObject();
}

void WriteAggregationTemporality(JsonWriter &writer,
                                 const metric_sdk::MetricData &metric_data) noexcept
{
  const std::int32_t temporality = MapAggregationTemporality(metric_data.aggregation_temporality);
  if (temporality != 0)
  {
    writer.Key("aggregationTemporality");
    writer.WriteInt32(temporality);
  }
}

void WriteSum(JsonWriter &writer, const metric_sdk::MetricData &metric_data) noexcept
{
  const bool is_monotonic =
      metric_data.instrument_descriptor.type_ == metric_sdk::InstrumentType::kCounter ||
      metric_data.instrument_descriptor.type_ == metric_sdk::InstrumentType::kObservableCounter;
  const std::int32_t temporality = MapAggregationTemporality(metric_data.aggregation_temporality);

  if (metric_data.point_data_attr_.empty() && temporality == 0 && !is_monotonic)
  {
    writer.WriteNull();
    return;
  }

  const std::uint64_t start_ts =
      static_cast<std::uint64_t>(metric_data.start_ts.time_since_epoch().count());
  const std::uint64_t ts =
      static_cast<std::uint64_t>(metric_data.end_ts.time_since_epoch().count());

  writer.BeginObject();
  if (!metric_data.point_data_attr_.empty())
  {
    writer.Key("dataPoints");
    writer.BeginArray();
    for (const auto &point_data_with_attributes : metric_data.point_data_attr_)
    {
      const auto &sum_data =
          nostd::get<metric_sdk::SumPointData>(point_data_with_attributes.point_data);
      WriteNumberDataPoint(writer, point_data_with_attributes.attributes, sum_data.value_, start_ts,
                           ts);
    }
    writer.EndArray();
  }
  WriteAggregationTemporality(writer, metric_data);
  if (is_monotonic)
  {
    writer.Key("isMonotonic");
    writer.WriteBool(true);
  }
  writer.EndObject();
}

void WriteGauge(JsonWriter &writer, const metric_sdk::MetricData &metric_data) noexcept
{
  if (metric_data.point_data_attr_.empty())
  {
    writer.WriteNull();
    return;
  }

  const std::uint64_t start_ts =
      static_cast<std::uint64_t>(metric_data.start_ts.time_since_epoch().count());
  const std::uint64_t ts =
      static_cast<std::uint64_t>(metric_data.end_ts.time_since_epoch().count());

  writer.BeginObject();
  writer.Key("dataPoints");
  writer.BeginArray();
  for (const auto &point_data_with_attributes : metric_data.point_data_attr_)
  {
    const auto &gauge_data =
        nostd::get<metric_sdk::LastValuePointData>(point_data_with_attributes.point_data);
    WriteNumberDataPoint(writer, point_data_with_attributes.attributes, gauge_data.value_, start_ts,
                         ts);
  }
  writer.EndArray();
  writer.EndObject();
}

void WriteHistogram(JsonWriter &writer, const metric_sdk::MetricData &metric_data) noexcept
{
  const std::int32_t temporality = MapAggregationTemporality(metric_data.aggregation_temporality);
  if (metric_data.point_data_attr_.empty() && temporality == 0)
  {
    writer.WriteNull();
    return;
  }

  const std::uint64_t start_ts =
      static_cast<std::uint64_t>(metric_data.start_ts.time_since_epoch().count());
  const std::uint64_t ts =
      static_cast<std::uint64_t>(metric_data.end_ts.time_since_epoch().count());

  writer.BeginObject();
  if (!metric_data.point_data_attr_.empty())
  {
    writer.Key("dataPoints");
    writer.BeginArray();
    for (const auto &point_data_with_attributes : metric_data.point_data_attr_)
    {
      const auto &histogram_data =
          nostd::get<metric_sdk::HistogramPointData>(point_data_with_attributes.point_data);
      WriteHistogramDataPoint(writer, point_data_with_attributes.attributes, histogram_data,
                              start_ts, ts);
    }
    writer.EndArray();
  }
  WriteAggregationTemporality(writer, metric_data);
  writer.EndObject();
}

void WriteExponentialHistogram(JsonWriter &writer,
                               const metric_sdk::MetricData &metric_data) noexcept
{
  const std::int32_t temporality = MapAggregationTemporality(metric_data.aggregation_temporality);
  if (metric_data.point_data_attr_.empty() && temporality == 0)
  {
    writer.WriteNull();
    return;
  }

  const std::uint64_t start_ts =
      static_cast<std::uint64_t>(metric_data.start_ts.time_since_epoch().count());
  const std::uint64_t ts =
      static_cast<std::uint64_t>(metric_data.end_ts.time_since_epoch().count());

  writer.BeginObject();
  if (!metric_data.point_data_attr_.empty())
  {
    writer.Key("dataPoints");
    writer.BeginArray();
    for (const auto &point_data_with_attributes : metric_data.point_data_attr_)
    {
      const auto &histogram_data = nostd::get<metric_sdk::Base2ExponentialHistogramPointData>(
          point_data_with_attributes.point_data);
      WriteExponentialHistogramDataPoint(writer, point_data_with_attributes.attributes,
                                         histogram_data, start_ts, ts);
    }
    writer.EndArray();
  }
  WriteAggregationTemporality(writer, metric_data);
  writer.EndObject();
}

/**
 * Which of Metric's `data` alternatives a collection holds, decided by the
 * first point as the protobuf path decides it: one MetricData is one
 * instrument's aggregation, so its points cannot disagree.
 */
metric_sdk::AggregationType GetAggregationType(const metric_sdk::MetricData &metric_data) noexcept
{
  if (metric_data.point_data_attr_.empty())
  {
    return metric_sdk::AggregationType::kDrop;
  }
  const auto &point_data = metric_data.point_data_attr_[0].point_data;
  if (nostd::holds_alternative<metric_sdk::SumPointData>(point_data))
  {
    return metric_sdk::AggregationType::kSum;
  }
  if (nostd::holds_alternative<metric_sdk::HistogramPointData>(point_data))
  {
    return metric_sdk::AggregationType::kHistogram;
  }
  if (nostd::holds_alternative<metric_sdk::Base2ExponentialHistogramPointData>(point_data))
  {
    return metric_sdk::AggregationType::kBase2ExponentialHistogram;
  }
  if (nostd::holds_alternative<metric_sdk::LastValuePointData>(point_data))
  {
    return metric_sdk::AggregationType::kLastValue;
  }
  return metric_sdk::AggregationType::kDrop;
}

void WriteMetric(JsonWriter &writer, const metric_sdk::MetricData &metric_data) noexcept
{
  writer.BeginObject();
  if (!metric_data.instrument_descriptor.name_.empty())
  {
    writer.Key("name");
    writer.WriteString(metric_data.instrument_descriptor.name_);
  }
  if (!metric_data.instrument_descriptor.description_.empty())
  {
    writer.Key("description");
    writer.WriteString(metric_data.instrument_descriptor.description_);
  }
  if (!metric_data.instrument_descriptor.unit_.empty())
  {
    writer.Key("unit");
    writer.WriteString(metric_data.instrument_descriptor.unit_);
  }

  switch (GetAggregationType(metric_data))
  {
    case metric_sdk::AggregationType::kLastValue:
      writer.Key("gauge");
      WriteGauge(writer, metric_data);
      break;
    case metric_sdk::AggregationType::kSum:
      writer.Key("sum");
      WriteSum(writer, metric_data);
      break;
    case metric_sdk::AggregationType::kHistogram:
      writer.Key("histogram");
      WriteHistogram(writer, metric_data);
      break;
    case metric_sdk::AggregationType::kBase2ExponentialHistogram:
      writer.Key("exponentialHistogram");
      WriteExponentialHistogram(writer, metric_data);
      break;
    default:
      // A dropped aggregation leaves the oneof unset rather than emitting an
      // empty alternative.
      break;
  }
  writer.EndObject();
}

}  // namespace

void ConvertMetricsToJson(JsonWriter &writer,
                          const opentelemetry::sdk::metrics::ResourceMetrics &data) noexcept
{
  // Without a resource there is nothing to attach the metrics to, so the
  // request stays empty -- and a message with no fields set is null, not {}.
  if (data.resource_ == nullptr)
  {
    writer.WriteNull();
    return;
  }

  writer.BeginObject();
  writer.Key("resourceMetrics");
  writer.BeginArray();
  writer.BeginObject();

  // Unlike traces, the resource slot is always created, so it is present even
  // when the resource carries no attributes and so encodes as null.
  writer.Key("resource");
  json_mapping::WriteResource(writer, *data.resource_);

  // A scope is what a ScopeMetrics is keyed by; one without it is skipped
  // rather than emitted with the key absent.
  bool has_scope = false;
  for (const auto &scope_metrics : data.scope_metric_data_)
  {
    if (scope_metrics.scope_ == nullptr)
    {
      continue;
    }
    if (!has_scope)
    {
      has_scope = true;
      writer.Key("scopeMetrics");
      writer.BeginArray();
    }

    writer.BeginObject();
    writer.Key("scope");
    json_mapping::WriteInstrumentationScope(writer, *scope_metrics.scope_);
    if (!scope_metrics.metric_data_.empty())
    {
      writer.Key("metrics");
      writer.BeginArray();
      for (const auto &metric_data : scope_metrics.metric_data_)
      {
        WriteMetric(writer, metric_data);
      }
      writer.EndArray();
    }
    if (!scope_metrics.scope_->GetSchemaURL().empty())
    {
      writer.Key("schemaUrl");
      writer.WriteString(scope_metrics.scope_->GetSchemaURL());
    }
    writer.EndObject();
  }
  if (has_scope)
  {
    writer.EndArray();
  }

  if (!data.resource_->GetSchemaURL().empty())
  {
    writer.Key("schemaUrl");
    writer.WriteString(data.resource_->GetSchemaURL());
  }

  writer.EndObject();
  writer.EndArray();
  writer.EndObject();
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
