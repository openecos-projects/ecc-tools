// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file SDCGeneratedClockResolverTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-10-28
 * @brief Generated-waveform data processing after shared SDC import.
 */

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "SDCGeneratedClockResolver.hh"
#include "TimingConstraints.hh"

namespace icts_test {
namespace {

auto primaryClock(const std::string& name = "master", const std::string& port = "clk_i") -> icts::SdcClockDecl
{
  icts::SdcClockDecl clock;
  clock.kind = icts::SdcClockDecl::Kind::kPrimary;
  clock.clock_name = name;
  clock.targets = {{icts::SdcObjectKind::kPort, port, true}};
  clock.period_ns = 10.0;
  clock.period_resolved = true;
  clock.waveform_ns = {0.0, 4.0};
  clock.waveform_explicit = true;
  clock.waveform_resolved = true;
  return clock;
}

auto generatedClock(const std::string& name = "generated", const std::string& master = "master") -> icts::SdcClockDecl
{
  icts::SdcClockDecl clock;
  clock.kind = icts::SdcClockDecl::Kind::kGenerated;
  clock.clock_name = name;
  clock.targets = {{icts::SdcObjectKind::kPin, "divider/Q", true}};
  clock.generated_sources = {{icts::SdcObjectKind::kClock, master, true}};
  clock.master_clock_name = master;
  return clock;
}

auto clockData(std::vector<icts::SdcClockDecl> clocks) -> icts::SdcClockData
{
  icts::SdcClockData data;
  data.clocks = std::move(clocks);
  return data;
}
TEST(SDCGeneratedClockResolverTest, ResolvesPowerOfTwoDivider)
{
  auto generated = generatedClock();
  generated.divide_by = 2;
  generated.divide_by_explicit = true;
  auto data = clockData({primaryClock(), generated});
  auto expected = data;
  expected.clocks[1].period_ns = 20.0;
  expected.clocks[1].period_resolved = true;
  expected.clocks[1].waveform_ns = {0.0, 10.0};
  expected.clocks[1].waveform_resolved = true;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, PreservesSourceDutyWhenMultiplying)
{
  auto generated = generatedClock();
  generated.multiply_by = 2;
  generated.multiply_by_explicit = true;
  auto data = clockData({primaryClock(), generated});
  auto expected = data;
  expected.clocks[1].period_ns = 5.0;
  expected.clocks[1].period_resolved = true;
  expected.clocks[1].waveform_ns = {0.0, 2.0};
  expected.clocks[1].waveform_resolved = true;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, AppliesExplicitDutyCycle)
{
  auto generated = generatedClock();
  generated.multiply_by = 2;
  generated.duty_cycle_percent = 25.0;
  auto data = clockData({primaryClock(), generated});
  auto expected = data;
  expected.clocks[1].period_ns = 5.0;
  expected.clocks[1].period_resolved = true;
  expected.clocks[1].waveform_ns = {0.0, 1.25};
  expected.clocks[1].waveform_resolved = true;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, AppliesInversionAfterDivision)
{
  auto generated = generatedClock();
  generated.divide_by = 2;
  generated.divide_by_explicit = true;
  generated.invert = true;
  auto data = clockData({primaryClock(), generated});
  auto expected = data;
  expected.clocks[1].period_ns = 20.0;
  expected.clocks[1].period_resolved = true;
  expected.clocks[1].waveform_ns = {10.0, 20.0};
  expected.clocks[1].waveform_resolved = true;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, DerivesShiftedEdgesInNanoseconds)
{
  auto generated = generatedClock();
  generated.generated_edges = {1, 3, 5};
  generated.generated_edge_shifts_ns = {1.0, 2.0, 3.0};
  auto data = clockData({primaryClock(), generated});
  auto expected = data;
  expected.clocks[1].period_ns = 22.0;
  expected.clocks[1].period_resolved = true;
  expected.clocks[1].waveform_ns = {1.0, 12.0};
  expected.clocks[1].waveform_resolved = true;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, ResolvesChainedDeclarationsIndependentOfInputOrder)
{
  auto parent = generatedClock("parent");
  parent.divide_by = 2;
  parent.divide_by_explicit = true;
  auto child = generatedClock("child", "parent");
  child.divide_by = 4;
  child.divide_by_explicit = true;
  auto data = clockData({child, parent, primaryClock()});
  auto expected = data;
  expected.clocks[0].period_ns = 80.0;
  expected.clocks[0].period_resolved = true;
  expected.clocks[0].waveform_ns = {0.0, 40.0};
  expected.clocks[0].waveform_resolved = true;
  expected.clocks[1].period_ns = 20.0;
  expected.clocks[1].period_resolved = true;
  expected.clocks[1].waveform_ns = {0.0, 10.0};
  expected.clocks[1].waveform_resolved = true;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, LeavesAmbiguousPhysicalSourcesForGraphResolution)
{
  auto generated = generatedClock();
  generated.master_clock_name.clear();
  generated.generated_sources = {{icts::SdcObjectKind::kPort, "clk_*", true}};
  auto data = clockData({primaryClock("a", "clk_a"), primaryClock("b", "clk_b"), generated});
  const auto expected = data;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, ReportsInvalidGeneratedEdgesThroughOwnedStatus)
{
  auto generated = generatedClock();
  generated.generated_edges = {1, 1, 3};
  auto data = clockData({primaryClock(), generated});
  auto expected = data;
  expected.status = icts::SdcConstraintStatusCode::kMalformed;
  expected.issues = {{icts::SdcConstraintStatusCode::kMalformed, "create_generated_clock", "invalid_generated_waveform:generated"}};
  expected.diagnostics = {"create_generated_clock:invalid_generated_waveform:generated"};
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, PreservesAnExistingInputFailure)
{
  auto data = clockData({primaryClock(), generatedClock()});
  data.diagnostics = {"set_case_analysis:unsupported"};
  data.status = icts::SdcConstraintStatusCode::kUnsupported;
  data.issues = {{icts::SdcConstraintStatusCode::kUnsupported, "set_case_analysis", "unsupported"}};
  const auto expected = data;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

TEST(SDCGeneratedClockResolverTest, DoesNotReinterpretAResolvedSharedFrontendWaveform)
{
  auto generated = generatedClock();
  generated.period_ns = 7.0;
  generated.period_resolved = true;
  generated.waveform_ns = {1.0, 3.0};
  generated.waveform_resolved = true;
  auto data = clockData({primaryClock(), generated});
  const auto expected = data;
  icts::SDCGeneratedClockResolver::resolve(data);
  EXPECT_EQ(data, expected);
}

}  // namespace
}  // namespace icts_test
