// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>
#include <memory>

#include "opentelemetry/exporters/otlp/otlp_json_reader.h"
#include "opentelemetry/exporters/otlp/otlp_json_reader_factory.h"
#include "opentelemetry/version.h"

OPENTELEMETRY_BEGIN_NAMESPACE
namespace exporter
{
namespace otlp
{
namespace test
{

// Tests a JsonReader backend against the contract documented in otlp_json_reader.h. The test of a
// backend compiles otlp_json_reader_contract_test.cc and instantiates the suite with its factory:
//
//   INSTANTIATE_TEST_SUITE_P(Name, JsonReaderContract,
//                            ::testing::Values(std::make_shared<NameJsonReaderFactory>()));
class JsonReaderContract : public ::testing::TestWithParam<std::shared_ptr<JsonReaderFactory>>
{
protected:
  // Expects the factory to have produced a reader, so a failure here does not crash the suite.
  std::unique_ptr<JsonReader> MakeReader() const;
};

}  // namespace test
}  // namespace otlp
}  // namespace exporter
OPENTELEMETRY_END_NAMESPACE
