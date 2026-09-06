// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "opentelemetry/exporters/otlp/otlp_preferred_temporality.h"
#include "opentelemetry/sdk/metrics/instruments.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace detail
{

/**
 * How a metric exporter's configured preference turns into the temporality it
 * reports per instrument.
 *
 * This is a decision about the SDK's metric data model, with no bearing on how
 * the data is then encoded, so it lives apart from the encoding: the OTLP/JSON
 * metric exporter has to answer GetAggregationTemporality without linking
 * protobuf, and answering it differently from OtlpMetricUtils -- which
 * delegates here -- would make two exporters of the same signal disagree about
 * what the collector receives.
 *
 * Inline rather than a library so that neither side takes a link dependency on
 * the other to ask the question, and under detail/ because it is that sharing
 * mechanism rather than API: unpublishing a header is harder than promoting
 * one.
 */

inline sdk::metrics::AggregationTemporality SelectDeltaTemporality(
    sdk::metrics::InstrumentType instrument_type) noexcept
{
  switch (instrument_type)
  {
    case sdk::metrics::InstrumentType::kCounter:
    case sdk::metrics::InstrumentType::kObservableCounter:
    case sdk::metrics::InstrumentType::kHistogram:
    case sdk::metrics::InstrumentType::kObservableGauge:
    case sdk::metrics::InstrumentType::kGauge:
      return sdk::metrics::AggregationTemporality::kDelta;
    case sdk::metrics::InstrumentType::kUpDownCounter:
    case sdk::metrics::InstrumentType::kObservableUpDownCounter:
      return sdk::metrics::AggregationTemporality::kCumulative;
  }
  return sdk::metrics::AggregationTemporality::kUnspecified;
}

inline sdk::metrics::AggregationTemporality SelectCumulativeTemporality(
    sdk::metrics::InstrumentType /* instrument_type */) noexcept
{
  return sdk::metrics::AggregationTemporality::kCumulative;
}

inline sdk::metrics::AggregationTemporality SelectLowMemoryTemporality(
    sdk::metrics::InstrumentType instrument_type) noexcept
{
  switch (instrument_type)
  {
    case sdk::metrics::InstrumentType::kCounter:
    case sdk::metrics::InstrumentType::kHistogram:
      return sdk::metrics::AggregationTemporality::kDelta;
    case sdk::metrics::InstrumentType::kObservableCounter:
    case sdk::metrics::InstrumentType::kGauge:
    case sdk::metrics::InstrumentType::kObservableGauge:
    case sdk::metrics::InstrumentType::kUpDownCounter:
    case sdk::metrics::InstrumentType::kObservableUpDownCounter:
      return sdk::metrics::AggregationTemporality::kCumulative;
  }
  return sdk::metrics::AggregationTemporality::kUnspecified;
}

inline sdk::metrics::AggregationTemporalitySelector ChooseAggregationTemporalitySelector(
    PreferredAggregationTemporality preferred_aggregation_temporality) noexcept
{
  if (preferred_aggregation_temporality == PreferredAggregationTemporality::kDelta)
  {
    return SelectDeltaTemporality;
  }
  else if (preferred_aggregation_temporality == PreferredAggregationTemporality::kCumulative)
  {
    return SelectCumulativeTemporality;
  }
  return SelectLowMemoryTemporality;
}

}  // namespace detail
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
