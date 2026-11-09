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
 * @file ConstraintBudgetsTest.cc
 * @brief CTS budget consumption is independent of parsing and query syntax.
 */
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

#include "ConstraintBudgets.hh"
#include "data_manager/config/Config.hh"
#include "gtest/gtest.h"
#include "timing/TimingConstraints.hh"

namespace icts_test {
namespace {

auto limits(double fanout, double transition, double cap) -> std::vector<icts::SdcElectricalLimit>
{
  return {{.rule = icts::SdcElectricalRule::kFanout, .object_name = "top", .value = fanout},
          {.rule = icts::SdcElectricalRule::kTransition, .object_name = "top", .value = transition},
          {.rule = icts::SdcElectricalRule::kCapacitance, .object_name = "top", .value = cap}};
}

TEST(ConstraintBudgetsTest, NativeDesignRulesOverrideBothTighterAndLooserConfiguration)
{
  for (const double configured : {0.1, 0.9}) {
    icts::Config config;
    config.set_max_fanout(configured < 0.5 ? 2U : 32U);
    config.set_max_buf_tran(configured);
    config.set_max_sink_tran(configured);
    config.set_max_cap(configured);
    const auto rows = icts::ApplyConstraintBudgets(limits(8, 0.4, 0.5), config);
    EXPECT_EQ(config.get_max_fanout(), 8U);
    EXPECT_DOUBLE_EQ(config.get_max_buf_tran(), 0.4);
    EXPECT_DOUBLE_EQ(config.get_max_sink_tran(), 0.4);
    EXPECT_DOUBLE_EQ(config.get_max_cap(), 0.5);
    EXPECT_TRUE(config.has_sdc_max_transition());
    EXPECT_DOUBLE_EQ(config.get_sdc_max_transition_ns(), 0.4);
    EXPECT_TRUE(config.has_sdc_max_cap());
    EXPECT_DOUBLE_EQ(config.get_sdc_max_cap_pf(), 0.5);
    EXPECT_TRUE(std::ranges::all_of(rows, [](const auto& row) -> bool { return row.from_sdc && !row.rejected; }));
  }
}

TEST(ConstraintBudgetsTest, MissingNativeRulesLeaveJsonAndDefaultProvenanceIntact)
{
  icts::Config config;
  const auto defaults = icts::ApplyConstraintBudgets({}, config);
  EXPECT_TRUE(std::ranges::all_of(defaults, [](const auto& row) -> bool { return !row.from_sdc && !row.configured && !row.rejected; }));
  config.set_max_fanout(7U);
  config.set_max_buf_tran(0.6);
  config.set_max_sink_tran(0.7);
  config.set_max_cap(0.8);
  const auto rows = icts::ApplyConstraintBudgets({}, config);
  EXPECT_TRUE(std::ranges::all_of(rows, [](const auto& row) -> bool { return !row.from_sdc && row.configured && !row.rejected; }));
  EXPECT_EQ(config.get_max_fanout(), 7U);
  EXPECT_DOUBLE_EQ(config.get_max_buf_tran(), 0.6);
  EXPECT_DOUBLE_EQ(config.get_max_sink_tran(), 0.7);
  EXPECT_DOUBLE_EQ(config.get_max_cap(), 0.8);
  EXPECT_FALSE(config.has_sdc_max_transition());
  EXPECT_FALSE(config.has_sdc_max_cap());
}

TEST(ConstraintBudgetsTest, FractionalFanoutFloorsACountAndUsesExistingConfigRange)
{
  icts::Config config;
  static_cast<void>(icts::ApplyConstraintBudgets(limits(8.75, 0.2, 0.3), config));
  EXPECT_EQ(config.get_max_fanout(), 8U);
  for (const double value : {0.0, -1.0, static_cast<double>(icts::Config::kMaxFanout) + 1.0, std::numeric_limits<double>::infinity()}) {
    const auto rows = icts::ApplyConstraintBudgets(limits(value, 0.2, 0.3), config);
    EXPECT_EQ(config.get_max_fanout(), 8U);
    EXPECT_TRUE(rows.front().rejected);
    EXPECT_FALSE(rows.front().from_sdc);
  }
}

TEST(ConstraintBudgetsTest, InvalidElectricalValuesAreReportedNotPromotedToBudgets)
{
  for (const double value : {0.0, -0.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    icts::Config config;
    config.set_max_buf_tran(0.6);
    config.set_max_sink_tran(0.7);
    config.set_max_cap(0.8);
    const auto rows = icts::ApplyConstraintBudgets(limits(8, value, value), config);
    EXPECT_TRUE(rows.at(1U).rejected);
    EXPECT_TRUE(rows.at(2U).rejected);
    EXPECT_DOUBLE_EQ(config.get_max_buf_tran(), 0.6);
    EXPECT_DOUBLE_EQ(config.get_max_sink_tran(), 0.7);
    EXPECT_DOUBLE_EQ(config.get_max_cap(), 0.8);
    EXPECT_FALSE(config.has_sdc_max_transition());
    EXPECT_FALSE(config.has_sdc_max_cap());
  }
}

TEST(ConstraintBudgetsTest, LocalRulesNeverBecomeGlobalLimits)
{
  for (const auto scope :
       {icts::SdcElectricalScope::kPort, icts::SdcElectricalScope::kPin, icts::SdcElectricalScope::kLibPin, icts::SdcElectricalScope::kClock}) {
    auto local = limits(2, 0.1, 0.1);
    for (auto& entry : local) {
      entry.scope = scope;
      entry.object_name = "object";
    }
    icts::Config config;
    const auto rows = icts::ApplyConstraintBudgets(local, config);
    EXPECT_EQ(config.get_max_fanout(), 32U);
    EXPECT_FALSE(config.has_sdc_max_transition());
    EXPECT_FALSE(config.has_sdc_max_cap());
    EXPECT_TRUE(std::ranges::all_of(rows, [](const auto& row) -> bool { return !row.from_sdc && !row.rejected; }));
  }
}

TEST(ConstraintBudgetsTest, ResetDropsConstraintProvenance)
{
  icts::Config config;
  static_cast<void>(icts::ApplyConstraintBudgets(limits(8, 0.2, 0.3), config));
  config.reset();
  EXPECT_FALSE(config.has_sdc_max_transition());
  EXPECT_FALSE(config.has_sdc_max_cap());
  EXPECT_EQ(config.get_max_fanout(), 32U);
}

}  // namespace
}  // namespace icts_test
