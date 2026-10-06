// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include "otlp_json_reader_contract_test.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace test
{

std::unique_ptr<JsonReader> JsonReaderContract::MakeReader() const
{
  auto reader = GetParam()->Create();
  EXPECT_NE(nullptr, reader) << "JsonReaderFactory::Create() returned nullptr";
  return reader;
}

TEST_P(JsonReaderContract, FactoryReturnsAWorkingReader)
{
  auto reader = MakeReader();
  ASSERT_NE(reader, nullptr);
  EXPECT_TRUE(reader->Parse("{}"));
}

TEST_P(JsonReaderContract, RejectsMalformedDocuments)
{
  auto reader = MakeReader();
  EXPECT_FALSE(reader->Parse("{some bad JSON"));
  EXPECT_FALSE(reader->Parse("{\"a\": 1"));
  EXPECT_FALSE(reader->Parse(""));
}

TEST_P(JsonReaderContract, RejectsRootsThatAreNotObjects)
{
  auto reader = MakeReader();
  EXPECT_FALSE(reader->Parse("[1,2]"));
  EXPECT_FALSE(reader->Parse("\"str\""));
  EXPECT_FALSE(reader->Parse("null"));
  EXPECT_FALSE(reader->Parse("7"));
}

TEST_P(JsonReaderContract, ReadsNothingFromAReaderThatFailedToParse)
{
  auto reader = MakeReader();
  ASSERT_FALSE(reader->Parse("[1,2]"));

  std::int64_t number = 7;
  std::string text    = "untouched";
  EXPECT_FALSE(reader->EnterObject("a"));
  EXPECT_FALSE(reader->GetInt64("a", number));
  EXPECT_FALSE(reader->GetString("a", text));
  EXPECT_EQ(number, 7);
  EXPECT_EQ(text, "untouched");
}

TEST_P(JsonReaderContract, ParsingAgainDiscardsThePreviousDocument)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(R"({"a":"first"})"));
  ASSERT_TRUE(reader->Parse(R"({"b":"second"})"));

  std::string text;
  EXPECT_FALSE(reader->GetString("a", text));
  ASSERT_TRUE(reader->GetString("b", text));
  EXPECT_EQ(text, "second");
}

TEST_P(JsonReaderContract, ReadsAnInt64FromANumberOrADecimalString)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(R"({"number":21,"string":"21","negative":-21})"));

  std::int64_t value = 0;
  ASSERT_TRUE(reader->GetInt64("number", value));
  EXPECT_EQ(value, 21);
  value = 0;
  ASSERT_TRUE(reader->GetInt64("string", value));
  EXPECT_EQ(value, 21);
  value = 0;
  ASSERT_TRUE(reader->GetInt64("negative", value));
  EXPECT_EQ(value, -21);
}

TEST_P(JsonReaderContract, ReadsTheFullInt64Range)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(
      R"({"max":"9223372036854775807","min":"-9223372036854775808","overflow":"9223372036854775808"})"));

  std::int64_t value = 0;
  ASSERT_TRUE(reader->GetInt64("max", value));
  EXPECT_EQ(value, (std::numeric_limits<std::int64_t>::max)());
  ASSERT_TRUE(reader->GetInt64("min", value));
  EXPECT_EQ(value, (std::numeric_limits<std::int64_t>::min)());
  EXPECT_FALSE(reader->GetInt64("overflow", value));
}

TEST_P(JsonReaderContract, RejectsValuesThatAreNotIntegers)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(
      R"({"object":{},"array":[],"bool":true,"null":null,"real":1.5,"text":"nope","hex":"0x15","spaced":" 21","trailing":"21x","empty":""})"));

  std::int64_t value = 7;
  for (const char *key : {"object", "array", "bool", "null", "real", "text", "hex", "spaced",
                          "trailing", "empty", "absent"})
  {
    EXPECT_FALSE(reader->GetInt64(key, value)) << "key: " << key;
  }
  EXPECT_EQ(value, 7);
}

TEST_P(JsonReaderContract, ReadsAStringAndRejectsOtherTypes)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(R"({"text":"hello","number":1,"object":{},"null":null})"));

  std::string value = "untouched";
  ASSERT_TRUE(reader->GetString("text", value));
  EXPECT_EQ(value, "hello");

  for (const char *key : {"number", "object", "null", "absent"})
  {
    EXPECT_FALSE(reader->GetString(key, value)) << "key: " << key;
  }
  EXPECT_EQ(value, "hello");
}

TEST_P(JsonReaderContract, EntersAndLeavesNestedObjects)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(R"({"outer":{"inner":{"leaf":"deep"}},"leaf":"shallow"})"));

  std::string value;
  ASSERT_TRUE(reader->EnterObject("outer"));
  ASSERT_TRUE(reader->EnterObject("inner"));
  ASSERT_TRUE(reader->GetString("leaf", value));
  EXPECT_EQ(value, "deep");

  reader->LeaveObject();
  EXPECT_FALSE(reader->GetString("leaf", value));
  reader->LeaveObject();
  ASSERT_TRUE(reader->GetString("leaf", value));
  EXPECT_EQ(value, "shallow");
}

TEST_P(JsonReaderContract, RefusesToEnterAnythingButAnObject)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(R"({"array":[],"text":"a","null":null,"here":"root"})"));

  for (const char *key : {"array", "text", "null", "absent"})
  {
    EXPECT_FALSE(reader->EnterObject(key)) << "key: " << key;
  }

  // A refused descent must leave the cursor where it was.
  std::string value;
  ASSERT_TRUE(reader->GetString("here", value));
  EXPECT_EQ(value, "root");
}

TEST_P(JsonReaderContract, LeavingTheRootIsANoOp)
{
  auto reader = MakeReader();
  ASSERT_TRUE(reader->Parse(R"({"leaf":"root"})"));

  reader->LeaveObject();
  reader->LeaveObject();

  std::string value;
  ASSERT_TRUE(reader->GetString("leaf", value));
  EXPECT_EQ(value, "root");
}

}  // namespace test
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
