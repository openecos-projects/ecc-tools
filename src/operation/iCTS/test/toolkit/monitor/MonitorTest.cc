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
 * @file MonitorTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-29
 * @brief Tests for the iCTS runtime Monitor contract.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <regex>
#include <string>
#include <thread>

#include "Monitor.hh"

namespace icts {
namespace {

// The reported string carries whole seconds, so proving "this call rebased the baseline"
// needs only a gap far below one second when the double-precision accessors are read
// instead. Keeping the gap at a few milliseconds is what makes these three tests cheap:
// what they assert is WHICH call rebases the baseline, not how long real time took.
constexpr auto kBaselineGap = std::chrono::milliseconds(5);
constexpr double kBaselineGapSeconds = 0.005;
// A measured gap is always at least the sleep and never much more, so compare against a
// margin rather than the exact sleep: the assertion is "time passed", not a stopwatch.
constexpr double kOneAndAHalfGaps = 1.5 * kBaselineGapSeconds;

TEST(MonitorTest, StatsInfoUsesIRTRuntimeFormat)
{
  Monitor monitor;
  const auto stats = monitor.getStatsInfo();
  const std::regex pattern(R"(^ \(elapsed = [0-9]{2}:[0-9]{2}:[0-9]{2}, cpu = [0-9]{2}:[0-9]{2}:[0-9]{2}, mem = -?[0-9]+\.[0-9]{2}MB\) $)");
  EXPECT_TRUE(std::regex_match(stats, pattern));
}

TEST(MonitorTest, IndividualGettersDoNotAdvanceTheBaseline)
{
  Monitor monitor;
  std::this_thread::sleep_for(kBaselineGap);
  (void) monitor.getElapsedTime();
  (void) monitor.getCPUTime();
  std::this_thread::sleep_for(kBaselineGap);
  // Two gaps have passed and neither plain getter touched the baseline, so the elapse
  // still covers both of them.
  EXPECT_GE(monitor.getElapsedSeconds(), kOneAndAHalfGaps);
}

TEST(MonitorTest, StatsInfoAdvancesTheLapBaseline)
{
  Monitor monitor;
  std::this_thread::sleep_for(kBaselineGap);
  (void) monitor.getStatsInfo();
  // The call that produced the string rebased the baseline, so nothing has elapsed yet.
  EXPECT_LT(monitor.getElapsedSeconds(), kBaselineGapSeconds);
}

TEST(MonitorTest, NestedMonitorsKeepIndependentBaselines)
{
  Monitor outer;
  std::this_thread::sleep_for(kBaselineGap);
  Monitor inner;
  std::this_thread::sleep_for(kBaselineGap);

  // The inner monitor started one gap later, so the outer one has seen both gaps and the
  // inner one only the second: the two baselines do not interfere.
  EXPECT_GE(outer.getElapsedSeconds(), kOneAndAHalfGaps);
  EXPECT_LT(inner.getElapsedSeconds(), kOneAndAHalfGaps);
}
}  // namespace
}  // namespace icts
