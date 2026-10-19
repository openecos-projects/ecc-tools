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

#include <gtest/gtest.h>

#include <limits>

#include "FastSTAIncremental.hh"
#include "FastSTAPower.hh"
#include "FastSTATiming.hh"
#include "fixture/FastSTATestModel.hh"

namespace icts_test {
namespace {

TEST(FastSTAPowerTest, UnusedLibraryVoltageDoesNotChangeClockPower)
{
  auto context = MakeTinyContext();
  context.liberty_cell_by_master.at("BUF_X1").voltage_v = 1.08;
  context.liberty_cell_by_master.at("BUF_X2").voltage_v = 1.08;
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  ASSERT_TRUE(icts::FastStaPower::update(context));
  const auto reference_power = context.power.total_power_w;

  // BUF_X2 is loaded as a candidate, but is not instantiated on this tree.
  for (const auto voltage : {2.97, std::numeric_limits<double>::quiet_NaN()}) {
    context.liberty_cell_by_master.at("BUF_X2").voltage_v = voltage;
    ASSERT_TRUE(icts::FastStaPower::update(context));
    EXPECT_DOUBLE_EQ(context.power.total_power_w, reference_power);
  }
}

TEST(FastSTAPowerTest, EachNetUsesItsDriverVoltageAndIncrementalMatchesFullUpdate)
{
  auto context = MakeTwoLevelContext();
  context.liberty_cell_by_master.at("BUF_X1").voltage_v = 1.08;
  context.liberty_cell_by_master.at("BUF_X2").voltage_v = 2.97;
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  ASSERT_TRUE(icts::FastStaPower::update(context));
  const auto original_leaf_power = context.nets.at(2U).switching_power_w;

  auto full = context;
  ASSERT_TRUE(icts::FastStaIncremental::changeBufferMaster(full, 3U, "BUF_X2"));
  ASSERT_TRUE(icts::FastStaTiming::update(full));
  ASSERT_TRUE(icts::FastStaPower::update(full));

  const auto region = icts::FastStaIncremental::changeBufferMasterIncremental(context, 3U, "BUF_X2");
  ASSERT_TRUE(region.has_value());
  ASSERT_TRUE(icts::FastStaTiming::updateRegion(context, *region));
  ASSERT_TRUE(context.power_valid);
  ASSERT_TRUE(icts::FastStaPower::updateRegion(context, *region));
  EXPECT_NEAR(context.nets.at(2U).switching_power_w, original_leaf_power * (2.97 / 1.08) * (2.97 / 1.08), 1e-15);
  ASSERT_EQ(context.nets.size(), full.nets.size());
  for (std::size_t index = 0; index < context.nets.size(); ++index) {
    EXPECT_DOUBLE_EQ(context.nets.at(index).switching_power_w, full.nets.at(index).switching_power_w);
  }
  EXPECT_DOUBLE_EQ(context.power.total_power_w, full.power.total_power_w);
}

TEST(FastSTAPowerTest, InvalidConnectedVoltageInvalidatesFullAndIncrementalPower)
{
  for (const auto voltage : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
    auto context = MakeTinyContext();
    ASSERT_TRUE(icts::FastStaTiming::update(context));
    ASSERT_TRUE(icts::FastStaPower::update(context));
    context.liberty_cell_by_master.at("BUF_X1").voltage_v = voltage;
    auto full = context;
    const icts::FastStaDirtyRegion region{.valid = true, .node_ids = {1U, 2U}, .net_ids = {0U, 1U}};
    EXPECT_FALSE(icts::FastStaPower::updateRegion(context, region));
    EXPECT_FALSE(context.power_valid);
    EXPECT_DOUBLE_EQ(context.power.total_power_w, 0.0);
    EXPECT_FALSE(icts::FastStaPower::update(full));
    EXPECT_FALSE(full.power_valid);
    EXPECT_DOUBLE_EQ(full.power.total_power_w, 0.0);
  }
}

TEST(FastSTAPowerTest, MissingDriverSupplyCannotBeReplacedByAnUnrelatedCell)
{
  auto context = MakeTinyContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  context.nodes.at(0U).cell_master = "UNKNOWN_DRIVER";
  EXPECT_FALSE(icts::FastStaPower::update(context));
  EXPECT_FALSE(context.power_valid);
}

TEST(FastSTAPowerTest, PrimaryInputWithConflictingLoadSuppliesHasNoInferredSwing)
{
  auto context = MakeTinyContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  context.liberty_cell_by_master.at("BUF_X2").voltage_v = 2.97;
  context.nodes.at(3U).cell_master = "BUF_X2";
  context.nets.at(0U).load_node_ids.push_back(3U);
  EXPECT_FALSE(icts::FastStaPower::update(context));
  EXPECT_FALSE(context.power_valid);
}

}  // namespace
}  // namespace icts_test
