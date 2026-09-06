// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Proves the protobuf-free OTLP/JSON metric mapping and the reflection-based
// protobuf one encode the same collection to the same bytes.
//
// One ResourceMetrics fixture feeds both paths, so anything the two disagree
// on is a difference in mapping rather than in input. The assertion is on the
// full body, because a mapping that drifts in one field is exactly what this
// is here to catch.

#include "opentelemetry/exporters/otlp/otlp_json_metric_mapping.h"

#include <gtest/gtest.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "opentelemetry/common/timestamp.h"
#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_converter.h"
#include "opentelemetry/exporters/otlp/otlp_json_writer.h"
#include "opentelemetry/exporters/otlp/otlp_metric_utils.h"
#include "opentelemetry/nostd/variant.h"
#include "opentelemetry/sdk/instrumentationscope/instrumentation_scope.h"
#include "opentelemetry/sdk/metrics/data/circular_buffer.h"
#include "opentelemetry/sdk/metrics/data/metric_data.h"
#include "opentelemetry/sdk/metrics/data/point_data.h"
#include "opentelemetry/sdk/metrics/export/metric_producer.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/version.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h" // IWYU pragma: keep
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h" // IWYU pragma: keep
// clang-format on

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace
{

namespace metric_sdk = opentelemetry::sdk::metrics;

std::string ProtobufPathJson(const metric_sdk::ResourceMetrics &data)
{
  proto::collector::metrics::v1::ExportMetricsServiceRequest request;
  OtlpMetricUtils::PopulateRequest(data, &request);

  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  // The defaults the OTLP HTTP and file exporters use for JSON bodies.
  ConvertGenericMessageToJson(*writer, request,
                              JsonConverterOptions{false, JsonBytesMappingKind::kHexId});
  EXPECT_TRUE(writer->ok());
  return writer->ToString();
}

std::string ProtobufFreePathJson(const metric_sdk::ResourceMetrics &data)
{
  auto writer = detail::GetDefaultJsonWriterFactory()->Create();
  ConvertMetricsToJson(*writer, data);
  EXPECT_TRUE(writer->ok());
  return writer->ToString();
}

void ExpectSameEncoding(const metric_sdk::ResourceMetrics &data)
{
  EXPECT_EQ(ProtobufPathJson(data), ProtobufFreePathJson(data));
}

const opentelemetry::sdk::resource::Resource &TestResource()
{
  static const auto resource = opentelemetry::sdk::resource::Resource::Create(
      {{"service.name", "equivalence"}, {"host.id", static_cast<std::int64_t>(7)}},
      "https://example.com/resource-schema");
  return resource;
}

const opentelemetry::sdk::instrumentationscope::InstrumentationScope &TestScope()
{
  static const auto scope = opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create(
      "equivalence_scope", "1.2.3", "https://example.com/scope-schema",
      {{"scope.attr", "scope value"}});
  return *scope;
}

metric_sdk::InstrumentDescriptor MakeDescriptor(
    metric_sdk::InstrumentType type,
    metric_sdk::InstrumentValueType value_type = metric_sdk::InstrumentValueType::kLong)
{
  return metric_sdk::InstrumentDescriptor{"metric_name", "metric description", "ms", type,
                                          value_type};
}

// A collection window with timestamps set, which is what the SDK hands the
// exporter in practice.
metric_sdk::MetricData MakeMetricData(metric_sdk::InstrumentDescriptor descriptor,
                                      metric_sdk::AggregationTemporality temporality)
{
  metric_sdk::MetricData metric_data;
  metric_data.instrument_descriptor   = std::move(descriptor);
  metric_data.aggregation_temporality = temporality;
  metric_data.start_ts =
      opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1700000000000000000LL));
  metric_data.end_ts =
      opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(1700000001000000000LL));
  return metric_data;
}

metric_sdk::PointAttributes MakePointAttributes()
{
  metric_sdk::PointAttributes attributes;
  attributes.SetAttribute("string_attr", "a string value");
  attributes.SetAttribute("int64_attr", static_cast<std::int64_t>(-42));
  attributes.SetAttribute("double_attr", 3.5);
  attributes.SetAttribute("bool_attr", true);
  return attributes;
}

// Wraps one metric into the ResourceMetrics both paths read.
metric_sdk::ResourceMetrics MakeResourceMetrics(std::vector<metric_sdk::MetricData> metrics)
{
  metric_sdk::ResourceMetrics data;
  data.resource_ = &TestResource();
  data.scope_metric_data_.push_back(metric_sdk::ScopeMetrics{&TestScope(), std::move(metrics)});
  return data;
}

TEST(OtlpJsonMetricEquivalence, NoResource)
{
  metric_sdk::ResourceMetrics data;
  ExpectSameEncoding(data);
}

TEST(OtlpJsonMetricEquivalence, ResourceWithoutAttributesAndNoScopes)
{
  // The empty resource, not Create({}) -- Create always injects the SDK's own
  // telemetry.* attributes, so only this reaches a resource with no fields set.
  metric_sdk::ResourceMetrics data;
  data.resource_ = &opentelemetry::sdk::resource::Resource::GetEmpty();
  ExpectSameEncoding(data);
}

TEST(OtlpJsonMetricEquivalence, ScopeWithoutNameVersionOrAttributes)
{
  static const auto bare_scope =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("", "", "", {});
  metric_sdk::ResourceMetrics data;
  data.resource_ = &TestResource();
  data.scope_metric_data_.push_back(
      metric_sdk::ScopeMetrics{bare_scope.get(), std::vector<metric_sdk::MetricData>{}});
  ExpectSameEncoding(data);
}

TEST(OtlpJsonMetricEquivalence, NullScopeIsSkipped)
{
  metric_sdk::ResourceMetrics data;
  data.resource_ = &TestResource();
  data.scope_metric_data_.push_back(metric_sdk::ScopeMetrics{
      static_cast<const opentelemetry::sdk::instrumentationscope::InstrumentationScope *>(nullptr),
      std::vector<metric_sdk::MetricData>{}});
  ExpectSameEncoding(data);
}

TEST(OtlpJsonMetricEquivalence, DroppedAggregationLeavesNoData)
{
  // No points at all: the instrument's identity is on the wire, the oneof is
  // not.
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kCounter),
                                    metric_sdk::AggregationTemporality::kDelta);
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, MonotonicIntegerSum)
{
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kCounter),
                                    metric_sdk::AggregationTemporality::kDelta);
  metric_sdk::SumPointData point;
  point.value_ = static_cast<std::int64_t>(9007199254740993LL);  // Beyond double precision.
  metric_data.point_data_attr_.push_back(
      metric_sdk::PointDataAttributes{MakePointAttributes(), point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, NonMonotonicDoubleSumWithUnspecifiedTemporality)
{
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kUpDownCounter,
                                                   metric_sdk::InstrumentValueType::kDouble),
                                    metric_sdk::AggregationTemporality::kUnspecified);
  metric_sdk::SumPointData point;
  point.value_ = 0.0;
  metric_data.point_data_attr_.push_back(metric_sdk::PointDataAttributes{{}, point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, ZeroTimestampsAreOmitted)
{
  metric_sdk::MetricData metric_data;
  metric_data.instrument_descriptor   = MakeDescriptor(metric_sdk::InstrumentType::kCounter);
  metric_data.aggregation_temporality = metric_sdk::AggregationTemporality::kCumulative;
  metric_sdk::SumPointData point;
  point.value_ = static_cast<std::int64_t>(0);
  metric_data.point_data_attr_.push_back(metric_sdk::PointDataAttributes{{}, point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, IntegerGauge)
{
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kObservableGauge),
                                    metric_sdk::AggregationTemporality::kUnspecified);
  metric_sdk::LastValuePointData point;
  point.value_              = static_cast<std::int64_t>(-17);
  point.is_lastvalue_valid_ = true;
  metric_data.point_data_attr_.push_back(
      metric_sdk::PointDataAttributes{MakePointAttributes(), point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, DoubleGauge)
{
  auto metric_data = MakeMetricData(
      MakeDescriptor(metric_sdk::InstrumentType::kGauge, metric_sdk::InstrumentValueType::kDouble),
      metric_sdk::AggregationTemporality::kDelta);
  metric_sdk::LastValuePointData point;
  point.value_              = 2.25;
  point.is_lastvalue_valid_ = true;
  metric_data.point_data_attr_.push_back(metric_sdk::PointDataAttributes{{}, point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, HistogramWithMinMax)
{
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kHistogram,
                                                   metric_sdk::InstrumentValueType::kDouble),
                                    metric_sdk::AggregationTemporality::kDelta);
  metric_sdk::HistogramPointData point;
  point.boundaries_     = {1.0, 5.0, 10.0};
  point.counts_         = {1, 2, 3, 4};
  point.count_          = 10;
  point.sum_            = 42.5;
  point.min_            = 0.5;
  point.max_            = 11.5;
  point.record_min_max_ = true;
  metric_data.point_data_attr_.push_back(
      metric_sdk::PointDataAttributes{MakePointAttributes(), point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, HistogramWithoutMinMaxAndIntegerSum)
{
  // An integer histogram sum is still a double on the wire.
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kHistogram),
                                    metric_sdk::AggregationTemporality::kCumulative);
  metric_sdk::HistogramPointData point;
  point.boundaries_     = {2.0};
  point.counts_         = {0, 0};
  point.count_          = 0;
  point.sum_            = static_cast<std::int64_t>(0);
  point.min_            = static_cast<std::int64_t>(0);
  point.max_            = static_cast<std::int64_t>(0);
  point.record_min_max_ = false;
  metric_data.point_data_attr_.push_back(metric_sdk::PointDataAttributes{{}, point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, ExponentialHistogram)
{
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kHistogram,
                                                   metric_sdk::InstrumentValueType::kDouble),
                                    metric_sdk::AggregationTemporality::kDelta);
  metric_sdk::Base2ExponentialHistogramPointData point;
  point.sum_              = 12.5;
  point.min_              = 0.25;
  point.max_              = 8.0;
  point.count_            = 6;
  point.zero_count_       = 1;
  point.scale_            = -2;
  point.record_min_max_   = true;
  point.positive_buckets_ = std::make_unique<metric_sdk::AdaptingCircularBufferCounter>(10);
  point.positive_buckets_->Increment(3, 2);
  point.positive_buckets_->Increment(5, 1);
  point.negative_buckets_ = std::make_unique<metric_sdk::AdaptingCircularBufferCounter>(10);
  point.negative_buckets_->Increment(-1, 4);
  metric_data.point_data_attr_.push_back(
      metric_sdk::PointDataAttributes{MakePointAttributes(), point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, ExponentialHistogramWithEmptyBuckets)
{
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kHistogram,
                                                   metric_sdk::InstrumentValueType::kDouble),
                                    metric_sdk::AggregationTemporality::kCumulative);
  metric_sdk::Base2ExponentialHistogramPointData point;
  point.count_          = 0;
  point.record_min_max_ = false;
  metric_data.point_data_attr_.push_back(metric_sdk::PointDataAttributes{{}, point});
  ExpectSameEncoding(MakeResourceMetrics({std::move(metric_data)}));
}

TEST(OtlpJsonMetricEquivalence, ExponentialHistogramWithoutBucketCounters)
{
  // Both bucket runs absent: the point survives, but there is nothing in it
  // beyond the collection window.
  auto metric_data = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kHistogram,
                                                   metric_sdk::InstrumentValueType::kDouble),
                                    metric_sdk::AggregationTemporality::kDelta);
  metric_sdk::Base2ExponentialHistogramPointData point;
  point.sum_   = 99.0;
  point.count_ = 3;
  metric_data.point_data_attr_.push_back(
      metric_sdk::PointDataAttributes{MakePointAttributes(), std::move(point)});

  // The buckets are released only once the point sits where the mapping will
  // read it: copying a point restores default-constructed buckets, and every
  // copy in between -- an initializer list included -- would undo this.
  std::vector<metric_sdk::MetricData> metrics;
  metrics.push_back(std::move(metric_data));
  auto &stored = nostd::get<metric_sdk::Base2ExponentialHistogramPointData>(
      metrics.back().point_data_attr_.back().point_data);
  stored.positive_buckets_.reset();
  stored.negative_buckets_.reset();

  metric_sdk::ResourceMetrics data;
  data.resource_ = &TestResource();
  data.scope_metric_data_.push_back(metric_sdk::ScopeMetrics{&TestScope(), std::move(metrics)});
  ExpectSameEncoding(data);
}

TEST(OtlpJsonMetricEquivalence, SeveralMetricsInOneScope)
{
  auto counter = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kCounter),
                                metric_sdk::AggregationTemporality::kDelta);
  metric_sdk::SumPointData sum_point;
  sum_point.value_ = static_cast<std::int64_t>(3);
  counter.point_data_attr_.push_back(
      metric_sdk::PointDataAttributes{MakePointAttributes(), sum_point});

  auto gauge = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kObservableGauge,
                                             metric_sdk::InstrumentValueType::kDouble),
                              metric_sdk::AggregationTemporality::kUnspecified);
  metric_sdk::LastValuePointData gauge_point;
  gauge_point.value_              = 1.5;
  gauge_point.is_lastvalue_valid_ = true;
  gauge.point_data_attr_.push_back(metric_sdk::PointDataAttributes{{}, gauge_point});

  ExpectSameEncoding(MakeResourceMetrics({std::move(counter), std::move(gauge)}));
}

TEST(OtlpJsonMetricEquivalence, SeveralScopes)
{
  static const auto other_scope =
      opentelemetry::sdk::instrumentationscope::InstrumentationScope::Create("other_scope");

  auto counter = MakeMetricData(MakeDescriptor(metric_sdk::InstrumentType::kCounter),
                                metric_sdk::AggregationTemporality::kCumulative);
  metric_sdk::SumPointData sum_point;
  sum_point.value_ = static_cast<std::int64_t>(11);
  counter.point_data_attr_.push_back(metric_sdk::PointDataAttributes{{}, sum_point});

  metric_sdk::ResourceMetrics data = MakeResourceMetrics({std::move(counter)});
  data.scope_metric_data_.push_back(
      metric_sdk::ScopeMetrics{other_scope.get(), std::vector<metric_sdk::MetricData>{}});
  ExpectSameEncoding(data);
}

}  // namespace
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
