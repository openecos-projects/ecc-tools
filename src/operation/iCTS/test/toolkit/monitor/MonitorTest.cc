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

auto elapsedSeconds(const std::string& stats) -> int
{
  const std::regex pattern(R"(elapsed = ([0-9]{2}):([0-9]{2}):([0-9]{2}))");
  std::smatch match;
  if (!std::regex_search(stats, match, pattern)) {
    return -1;
  }
  return std::stoi(match[1].str()) * 3600 + std::stoi(match[2].str()) * 60 + std::stoi(match[3].str());
}

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
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  (void) monitor.getElapsedTime();
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  EXPECT_GE(elapsedSeconds(monitor.getStatsInfo()), 2);
}

TEST(MonitorTest, StatsInfoAdvancesTheLapBaseline)
{
  Monitor monitor;
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  EXPECT_GE(elapsedSeconds(monitor.getStatsInfo()), 1);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  const auto lap_seconds = elapsedSeconds(monitor.getStatsInfo());
  EXPECT_GE(lap_seconds, 1);
  EXPECT_LE(lap_seconds, 2);
}

TEST(MonitorTest, NestedMonitorsKeepIndependentBaselines)
{
  Monitor outer;
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  Monitor inner;
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  EXPECT_GE(elapsedSeconds(outer.getStatsInfo()), 2);
  const auto inner_seconds = elapsedSeconds(inner.getStatsInfo());
  EXPECT_GE(inner_seconds, 1);
  EXPECT_LE(inner_seconds, 2);
}

}  // namespace
}  // namespace icts
