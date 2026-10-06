// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <memory>

#include "opentelemetry/exporters/otlp/detail/default_json_reader_factory.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory_nlohmann.h"
#include "opentelemetry/version.h"
#include "otlp_json_reader_contract_test.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace test
{

INSTANTIATE_TEST_SUITE_P(Nlohmann,
                         JsonReaderContract,
                         ::testing::Values(std::make_shared<JsonReaderFactoryNlohmann>()));

// The default resolver is what the exporters reach when no factory is supplied, so it has to be
// the backend compiled in rather than some other instance.
TEST(NlohmannJsonReader, IsTheDefaultBackend)
{
  auto reader = detail::GetDefaultJsonReaderFactory()->Create();
  ASSERT_NE(nullptr, reader);
  EXPECT_TRUE(reader->Parse(R"({"a":1})"));
}

}  // namespace test
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
