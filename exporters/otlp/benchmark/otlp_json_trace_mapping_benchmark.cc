// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

// Compares the two OTLP/JSON trace paths on serialization alone: the
// reflection-based converter fed by OtlpRecordable, and the protobuf-free
// mapping fed by OtlpJsonSpanRecordable. Neither case touches HTTP or a file,
// so a run measures recording plus encoding and nothing else.
//
// The equivalence test already proves the two produce identical bytes. What
// this answers is the other half of the same question: whether shedding
// protobuf costs anything at the sizes an exporter actually batches.

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/exporters/otlp/detail/default_json_writer_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_converter.h"
#include "opentelemetry/exporters/otlp/otlp_json_span_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_json_trace_mapping.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/exporters/otlp/otlp_recordable_utils.h"
#include "opentelemetry/nostd/span.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/trace/trace_state.h"

// clang-format off
#include "opentelemetry/exporters/otlp/protobuf_include_prefix.h" // IWYU pragma: keep
#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/exporters/otlp/protobuf_include_suffix.h" // IWYU pragma: keep
// clang-format on

namespace otlp       = opentelemetry::exporter::otlp;
namespace proto      = opentelemetry::proto;
namespace nostd      = opentelemetry::nostd;
namespace sdk_trace  = opentelemetry::sdk::trace;
namespace trace_api  = opentelemetry::trace;
namespace common_log = opentelemetry::sdk::common::internal_log;

namespace
{

constexpr std::uint8_t kTraceIdBytes[trace_api::TraceId::kSize] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
constexpr std::uint8_t kSpanIdBytes[trace_api::SpanId::kSize]       = {0x11, 0x12, 0x13, 0x14,
                                                                       0x15, 0x16, 0x17, 0x18};
constexpr std::uint8_t kParentSpanIdBytes[trace_api::SpanId::kSize] = {0x21, 0x22, 0x23, 0x24,
                                                                       0x25, 0x26, 0x27, 0x28};

// One span's worth of recording, the same script into either recordable type: a mix of
// attribute types, an event and a parent, so every token kind and the hex-ID mapping take part.
void RecordSpan(std::size_t i, sdk_trace::Recordable &recordable)
{
  recordable.SetIdentity(
      trace_api::SpanContext(trace_api::TraceId(kTraceIdBytes), trace_api::SpanId(kSpanIdBytes),
                             trace_api::TraceFlags(trace_api::TraceFlags::kIsSampled), false,
                             trace_api::TraceState::GetDefault()),
      trace_api::SpanId(kParentSpanIdBytes));
  recordable.SetName("GET /api/v1/resource");
  recordable.SetSpanKind(trace_api::SpanKind::kServer);
  recordable.SetStartTime(opentelemetry::common::SystemTimestamp(
      std::chrono::nanoseconds(1700000000000000000LL + static_cast<std::int64_t>(i))));
  recordable.SetDuration(std::chrono::nanoseconds(100000));
  recordable.SetAttribute("http.request.method", "GET");
  recordable.SetAttribute("url.path", "/api/v1/resource");
  recordable.SetAttribute("http.response.status_code", static_cast<std::int64_t>(200));
  recordable.SetAttribute("sample.ratio", 0.25);
  recordable.SetAttribute("retried", false);
  recordable.AddEvent("cache.miss", opentelemetry::common::SystemTimestamp(std::chrono::nanoseconds(
                                        1700000000000050000LL + static_cast<std::int64_t>(i))));
  recordable.SetStatus(trace_api::StatusCode::kOk, "");
}

template <typename Recordable>
std::vector<std::unique_ptr<sdk_trace::Recordable>> MakeBatch(std::size_t span_count)
{
  std::vector<std::unique_ptr<sdk_trace::Recordable>> batch;
  batch.reserve(span_count);
  for (std::size_t i = 0; i < span_count; ++i)
  {
    batch.emplace_back(new Recordable());
    RecordSpan(i, *batch.back());
  }
  return batch;
}

void BM_OtlpJsonTrace_ProtobufPath(benchmark::State &state)
{
  const auto span_count = static_cast<std::size_t>(state.range(0));
  auto batch            = MakeBatch<otlp::OtlpRecordable>(span_count);
  const nostd::span<std::unique_ptr<sdk_trace::Recordable>> spans(batch.data(), batch.size());

  for (auto _ : state)
  {
    proto::collector::trace::v1::ExportTraceServiceRequest request;
    otlp::OtlpRecordableUtils::PopulateRequest(spans, &request);

    auto writer = otlp::detail::GetDefaultJsonWriterFactory()->Create();
    otlp::ConvertGenericMessageToJson(
        *writer, request, otlp::JsonConverterOptions{false, otlp::JsonBytesMappingKind::kHexId});
    std::string body = writer->ToString();
    benchmark::DoNotOptimize(body.data());
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_OtlpJsonTrace_ProtobufFreePath(benchmark::State &state)
{
  const auto span_count = static_cast<std::size_t>(state.range(0));
  auto batch            = MakeBatch<otlp::OtlpJsonSpanRecordable>(span_count);
  const nostd::span<std::unique_ptr<sdk_trace::Recordable>> spans(batch.data(), batch.size());

  for (auto _ : state)
  {
    auto writer = otlp::detail::GetDefaultJsonWriterFactory()->Create();
    otlp::ConvertSpansToJson(*writer, spans);
    std::string body = writer->ToString();
    benchmark::DoNotOptimize(body.data());
  }

  state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_OtlpJsonTrace_ProtobufPath)
    ->RangeMultiplier(10)
    ->Range(1, 1000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_OtlpJsonTrace_ProtobufFreePath)
    ->RangeMultiplier(10)
    ->Range(1, 1000)
    ->Unit(benchmark::kMicrosecond);

}  // namespace

int main(int argc, char **argv)
{
  common_log::GlobalLogHandler::SetLogLevel(common_log::LogLevel::None);

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
