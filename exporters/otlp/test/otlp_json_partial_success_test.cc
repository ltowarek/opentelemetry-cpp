// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "opentelemetry/exporters/otlp/otlp_json_partial_success.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "opentelemetry/exporters/otlp/detail/default_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/nostd/string_view.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/test_common/sdk/common/scoped_test_log_handler.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace
{

using test_common::ScopedTestLogHandler;

std::unique_ptr<JsonReader> MakeReader()
{
  return detail::GetDefaultJsonReaderFactory()->Create();
}

OtlpPartialSuccess Parse(nostd::string_view body, bool &ok)
{
  auto reader = MakeReader();
  OtlpPartialSuccess partial_success;
  ok = ParseOtlpJsonPartialSuccess(*reader, body, OtlpTracePartialSuccessSignal(), partial_success);
  return partial_success;
}

bool Contains(const std::vector<ScopedTestLogHandler::Entry> &entries, const std::string &needle)
{
  return std::any_of(entries.begin(), entries.end(), [&](const ScopedTestLogHandler::Entry &entry) {
    return entry.msg.find(needle) != std::string::npos;
  });
}

bool HasError(const std::vector<ScopedTestLogHandler::Entry> &entries)
{
  return std::any_of(entries.begin(), entries.end(), [](const ScopedTestLogHandler::Entry &entry) {
    return entry.level == sdk::common::internal_log::LogLevel::Error;
  });
}

}  // namespace

TEST(OtlpJsonPartialSuccess, TreatsAnEmptyBodyAsFullSuccess)
{
  bool ok    = false;
  auto empty = Parse("", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(empty.rejected_count, 0);
  EXPECT_TRUE(empty.error_message.empty());
}

TEST(OtlpJsonPartialSuccess, TreatsAnAbsentPartialSuccessAsFullSuccess)
{
  bool ok     = false;
  auto absent = Parse("{}", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(absent.rejected_count, 0);
  EXPECT_TRUE(absent.error_message.empty());
}

TEST(OtlpJsonPartialSuccess, TreatsAnEmptyPartialSuccessAsFullSuccess)
{
  bool ok     = false;
  auto parsed = Parse(R"({"partialSuccess":{}})", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parsed.rejected_count, 0);
  EXPECT_TRUE(parsed.error_message.empty());
}

TEST(OtlpJsonPartialSuccess, ReadsARejectedCountEncodedAsAString)
{
  bool ok     = false;
  auto parsed = Parse(R"({"partialSuccess":{"rejectedSpans":"21","errorMessage":"too many"}})", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parsed.rejected_count, 21);
  EXPECT_EQ(parsed.error_message, "too many");
}

TEST(OtlpJsonPartialSuccess, ReadsARejectedCountEncodedAsANumber)
{
  bool ok     = false;
  auto parsed = Parse(R"({"partialSuccess":{"rejectedSpans":21,"errorMessage":"too many"}})", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parsed.rejected_count, 21);
  EXPECT_EQ(parsed.error_message, "too many");
}

TEST(OtlpJsonPartialSuccess, ReadsAnErrorMessageWithNoRejectedCount)
{
  bool ok     = false;
  auto parsed = Parse(R"({"partialSuccess":{"errorMessage":"nothing rejected, but"}})", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parsed.rejected_count, 0);
  EXPECT_EQ(parsed.error_message, "nothing rejected, but");
}

TEST(OtlpJsonPartialSuccess, FallsBackToDefaultsForFieldsOfTheWrongType)
{
  bool ok = false;
  auto parsed =
      Parse(R"({"partialSuccess":{"rejectedSpans":{},"errorMessage":["not","a","string"]}})", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parsed.rejected_count, 0);
  EXPECT_TRUE(parsed.error_message.empty());
}

TEST(OtlpJsonPartialSuccess, FallsBackToDefaultsForAPartialSuccessOfTheWrongType)
{
  bool ok     = false;
  auto parsed = Parse(R"({"partialSuccess":"not an object"})", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parsed.rejected_count, 0);
  EXPECT_TRUE(parsed.error_message.empty());
}

TEST(OtlpJsonPartialSuccess, ReadsPastFieldsItDoesNotKnow)
{
  bool ok = false;
  auto parsed =
      Parse(R"({"unknownField":1,"partialSuccess":{"rejectedSpans":"21","futureField":true}})", ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(parsed.rejected_count, 21);
}

TEST(OtlpJsonPartialSuccess, ReportsBodiesItCannotRead)
{
  for (const char *body :
       {"{some bad JSON", R"({"partialSuccess":{"rejectedSpans":"21")", "[1,2]", "\"str\"", "null"})
  {
    bool ok     = true;
    auto parsed = Parse(body, ok);
    EXPECT_FALSE(ok) << "body: " << body;
    EXPECT_EQ(parsed.rejected_count, 0);
    EXPECT_TRUE(parsed.error_message.empty());
  }
}

TEST(OtlpJsonPartialSuccess, LogsRejectedCountAndMessageForEachSignal)
{
  struct Case
  {
    const OtlpPartialSuccessSignal &signal;
    const char *body;
    const char *expected;
  };

  const Case cases[] = {
      {OtlpTracePartialSuccessSignal(),
       R"({"partialSuccess":{"rejectedSpans":"21","errorMessage":"too many spans!!"}})",
       "[OTLP TRACE HTTP Exporter] Export partial success: 21 span(s) rejected: \"too many "
       "spans!!\""},
      {OtlpMetricPartialSuccessSignal(),
       R"({"partialSuccess":{"rejectedDataPoints":"7","errorMessage":"too many points!!"}})",
       "[OTLP METRIC HTTP Exporter] Export partial success: 7 data point(s) rejected: \"too many "
       "points!!\""},
      {OtlpLogPartialSuccessSignal(),
       R"({"partialSuccess":{"rejectedLogRecords":"3","errorMessage":"too many logs!!"}})",
       "[OTLP LOG HTTP Exporter] Export partial success: 3 log record(s) rejected: \"too many "
       "logs!!\""},
  };

  for (const Case &test_case : cases)
  {
    ScopedTestLogHandler log{sdk::common::internal_log::LogLevel::Error};
    auto reader = MakeReader();
    EXPECT_TRUE(LogOtlpJsonPartialSuccess(*reader, test_case.body, test_case.signal, 100));
    EXPECT_TRUE(Contains(log.Drain(), test_case.expected)) << "expected: " << test_case.expected;
  }
}

TEST(OtlpJsonPartialSuccess, LogsNoErrorWhenNothingWasRejected)
{
  for (const char *body : {"", "{}", R"({"partialSuccess":{}})",
                           R"({"partialSuccess":{"rejectedSpans":"0","errorMessage":""}})"})
  {
    ScopedTestLogHandler log{sdk::common::internal_log::LogLevel::Debug};
    auto reader = MakeReader();
    EXPECT_TRUE(LogOtlpJsonPartialSuccess(*reader, body, OtlpTracePartialSuccessSignal(), 5));

    auto entries = log.Drain();
    EXPECT_FALSE(HasError(entries)) << "body: " << body;
#if OTEL_INTERNAL_LOG_LEVEL >= OTEL_INTERNAL_LOG_LEVEL_DEBUG
    EXPECT_TRUE(Contains(entries, "[OTLP TRACE HTTP Exporter] Export 5 trace span(s) success"))
        << "body: " << body;
#endif
  }
}

TEST(OtlpJsonPartialSuccess, LogsAnErrorForABodyItCannotRead)
{
  ScopedTestLogHandler log{sdk::common::internal_log::LogLevel::Error};
  auto reader = MakeReader();
  EXPECT_FALSE(
      LogOtlpJsonPartialSuccess(*reader, "{some bad JSON", OtlpTracePartialSuccessSignal(), 5));
  EXPECT_TRUE(
      Contains(log.Drain(), "[OTLP TRACE HTTP Exporter] Failed to parse JSON response body"));
}

}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
