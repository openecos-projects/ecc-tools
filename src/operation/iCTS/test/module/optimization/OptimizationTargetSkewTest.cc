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
 * @file OptimizationTargetSkewTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-06-13
 * @brief Per-clock skew target resolution contract tests for CTS optimization.
 */

#include <gtest/gtest.h>

#include "data_manager/config/Config.hh"
#include "optimization/preparation/OptimizationPreparation.hh"

namespace icts_test {
namespace {

namespace oi = icts::clock_sizing_optimization;

auto makeConfig(double skew_bound_ns) -> icts::Config
{
  icts::Config config;
  config.reset();
  config.set_skew_bound(skew_bound_ns);
  return config;
}

TEST(OptimizationTargetSkewTest, UsesSkewBound)
{
  const auto config = makeConfig(0.08);

  EXPECT_DOUBLE_EQ(oi::ResolveClockTargetSkewNs(config), 0.08);
}

TEST(OptimizationTargetSkewTest, NegativeSkewBoundIsDefended)
{
  const auto negative_bound_config = makeConfig(-0.08);
  EXPECT_DOUBLE_EQ(oi::ResolveClockTargetSkewNs(negative_bound_config), 0.0);
}

}  // namespace
}  // namespace icts_test
