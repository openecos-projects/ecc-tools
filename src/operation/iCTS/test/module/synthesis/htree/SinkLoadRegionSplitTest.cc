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
 * @file SinkLoadRegionSplitTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-06-12
 * @brief Unit tests for sink-load-region local split remediation.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"

namespace icts_test {
namespace {

class SplitPinFactory
{
 public:
  auto make(int x, int y) -> icts::Pin*
  {
    auto pin = std::make_unique<icts::Pin>("split_pin_" + std::to_string(_pins.size()), icts::PinType::kIn, icts::Point<int>(x, y), nullptr, nullptr, false);
    _pins.push_back(std::move(pin));
    return _pins.back().get();
  }

 private:
  std::vector<std::unique_ptr<icts::Pin>> _pins;
};

auto collectAllPins(const icts::htree::SinkLoadRegionSplitPlan& plan) -> std::vector<icts::Pin*>
{
  std::vector<icts::Pin*> all_pins;
  for (const auto& subgroup : plan.subgroups) {
    all_pins.insert(all_pins.end(), subgroup.begin(), subgroup.end());
  }
  std::ranges::sort(all_pins);
  return all_pins;
}

auto collectLeafGroups(const std::vector<icts::htree::SinkLoadRegionSplitNode>& nodes) -> std::vector<std::vector<icts::Pin*>>
{
  std::vector<std::vector<icts::Pin*>> leaves;
  std::vector<const icts::htree::SinkLoadRegionSplitNode*> pending_nodes;
  pending_nodes.reserve(nodes.size());
  for (std::size_t reverse_index = nodes.size(); reverse_index > 0U; --reverse_index) {
    pending_nodes.push_back(&nodes.at(reverse_index - 1U));
  }
  while (!pending_nodes.empty()) {
    const auto* node = pending_nodes.back();
    pending_nodes.pop_back();
    if (node->children.empty()) {
      leaves.push_back(node->loads);
      continue;
    }
    for (std::size_t reverse_index = node->children.size(); reverse_index > 0U; --reverse_index) {
      pending_nodes.push_back(&node->children.at(reverse_index - 1U));
    }
  }
  return leaves;
}

auto countTreeNodes(const std::vector<icts::htree::SinkLoadRegionSplitNode>& nodes) -> std::size_t
{
  std::size_t count = 0U;
  std::vector<const icts::htree::SinkLoadRegionSplitNode*> pending_nodes;
  pending_nodes.reserve(nodes.size());
  for (const auto& node : nodes) {
    pending_nodes.push_back(&node);
  }
  while (!pending_nodes.empty()) {
    const auto* node = pending_nodes.back();
    pending_nodes.pop_back();
    ++count;
    for (const auto& child : node->children) {
      pending_nodes.push_back(&child);
    }
  }
  return count;
}

auto expectTreeFanoutLegal(const std::vector<icts::htree::SinkLoadRegionSplitNode>& nodes, std::size_t max_fanout) -> void
{
  std::vector<const icts::htree::SinkLoadRegionSplitNode*> pending_nodes;
  pending_nodes.reserve(nodes.size());
  for (const auto& node : nodes) {
    pending_nodes.push_back(&node);
  }
  while (!pending_nodes.empty()) {
    const auto* node = pending_nodes.back();
    pending_nodes.pop_back();
    EXPECT_LE(node->children.size(), max_fanout);
    if (node->children.empty()) {
      EXPECT_LE(node->loads.size(), max_fanout);
    } else {
      for (const auto& child : node->children) {
        pending_nodes.push_back(&child);
      }
    }
  }
}

TEST(SinkLoadRegionSplitTest, FiveLoadsSplitIntoTwoSubgroups)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads = {factory.make(0, 0), factory.make(10, 0), factory.make(20, 0), factory.make(30, 0), factory.make(40, 0)};

  const auto plan = icts::htree::SplitSinkLoadRegionGroup(loads, 4U);
  ASSERT_TRUE(plan.feasible);
  ASSERT_EQ(plan.subgroups.size(), 2U);
  ASSERT_EQ(plan.centers.size(), 2U);
  for (const auto& subgroup : plan.subgroups) {
    EXPECT_LE(subgroup.size(), 4U);
    EXPECT_GE(subgroup.size(), 2U);
  }

  auto original = loads;
  std::ranges::sort(original);
  EXPECT_EQ(collectAllPins(plan), original);

  // Median cut on the x axis: lower half {0,10}, upper half {20,30,40}.
  EXPECT_EQ(plan.centers.at(0).get_x(), 5);
  EXPECT_EQ(plan.centers.at(1).get_x(), 30);
  EXPECT_EQ(plan.local_depth, 1U);
  EXPECT_EQ(plan.buffer_count, 2U);
  EXPECT_EQ(plan.leaf_group_count, 2U);
}

TEST(SinkLoadRegionSplitTest, NotApplicableWithinFanout)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads = {factory.make(0, 0), factory.make(1, 1), factory.make(2, 2), factory.make(3, 3)};
  const auto plan = icts::htree::SplitSinkLoadRegionGroup(loads, 4U);
  EXPECT_FALSE(plan.feasible);
}

TEST(SinkLoadRegionSplitTest, SeventeenLoadsUseTwoLocalLevels)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(17U);
  for (int index = 0; index < 17; ++index) {
    loads.push_back(factory.make(index, 0));
  }
  const auto plan = icts::htree::SplitSinkLoadRegionGroup(loads, 4U);
  ASSERT_TRUE(plan.feasible);
  EXPECT_EQ(plan.children.size(), 2U);
  EXPECT_EQ(plan.local_depth, 2U);
  EXPECT_EQ(plan.leaf_group_count, 5U);
  EXPECT_EQ(plan.buffer_count, 7U);
  expectTreeFanoutLegal(plan.children, 4U);
}

TEST(SinkLoadRegionSplitTest, SixteenLoadsQuadSplit)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(16U);
  for (int index = 0; index < 16; ++index) {
    loads.push_back(factory.make(index * 5, (index % 2) * 3));
  }
  const auto plan = icts::htree::SplitSinkLoadRegionGroup(loads, 4U);
  ASSERT_TRUE(plan.feasible);
  EXPECT_EQ(plan.subgroups.size(), 4U);
  EXPECT_EQ(plan.local_depth, 1U);
  EXPECT_EQ(plan.buffer_count, 4U);
  EXPECT_EQ(plan.leaf_group_count, 4U);
  for (const auto& subgroup : plan.subgroups) {
    EXPECT_EQ(subgroup.size(), 4U);
  }
}

TEST(SinkLoadRegionSplitTest, DeterministicAcrossInputOrder)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(7U);
  for (int index = 0; index < 7; ++index) {
    loads.push_back(factory.make(index * 11, 97 - index * 13));
  }

  const auto reference_plan = icts::htree::SplitSinkLoadRegionGroup(loads, 4U);
  ASSERT_TRUE(reference_plan.feasible);

  const std::vector<icts::Pin*> shuffled = {
      loads.at(3), loads.at(0), loads.at(6), loads.at(1), loads.at(5), loads.at(2), loads.at(4),
  };
  const auto shuffled_plan = icts::htree::SplitSinkLoadRegionGroup(shuffled, 4U);
  ASSERT_TRUE(shuffled_plan.feasible);

  ASSERT_EQ(shuffled_plan.subgroups.size(), reference_plan.subgroups.size());
  for (std::size_t subgroup_index = 0; subgroup_index < reference_plan.subgroups.size(); ++subgroup_index) {
    EXPECT_EQ(shuffled_plan.subgroups.at(subgroup_index), reference_plan.subgroups.at(subgroup_index));
    EXPECT_EQ(shuffled_plan.centers.at(subgroup_index).get_x(), reference_plan.centers.at(subgroup_index).get_x());
    EXPECT_EQ(shuffled_plan.centers.at(subgroup_index).get_y(), reference_plan.centers.at(subgroup_index).get_y());
  }
}

TEST(SinkLoadRegionSplitTest, ExactFanoutPowerUsesSingleLocalLevel)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(9U);
  for (int index = 0; index < 9; ++index) {
    loads.push_back(factory.make(index, index));
  }
  const auto plan = icts::htree::SplitSinkLoadRegionGroup(loads, 3U);
  ASSERT_TRUE(plan.feasible);
  EXPECT_EQ(plan.children.size(), 3U);
  EXPECT_EQ(plan.local_depth, 1U);
  EXPECT_EQ(plan.buffer_count, 3U);
  EXPECT_EQ(plan.leaf_group_count, 3U);
  expectTreeFanoutLegal(plan.children, 3U);
}

TEST(SinkLoadRegionSplitTest, TwentyFiveLoadsUseRecursiveTree)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(25U);
  for (int index = 0; index < 25; ++index) {
    loads.push_back(factory.make(index * 3, (index % 5) * 7));
  }
  const auto plan = icts::htree::SplitSinkLoadRegionGroup(loads, 4U);
  ASSERT_TRUE(plan.feasible);
  EXPECT_EQ(plan.children.size(), 2U);
  EXPECT_EQ(plan.local_depth, 2U);
  EXPECT_EQ(plan.leaf_group_count, 7U);
  EXPECT_EQ(plan.buffer_count, countTreeNodes(plan.children));
  expectTreeFanoutLegal(plan.children, 4U);

  auto leaves = collectLeafGroups(plan.children);
  std::vector<icts::Pin*> collected;
  collected.reserve(loads.size());
  for (const auto& leaf : leaves) {
    collected.insert(collected.end(), leaf.begin(), leaf.end());
  }
  std::ranges::sort(collected);
  auto original = loads;
  std::ranges::sort(original);
  EXPECT_EQ(collected, original);
}

}  // namespace
}  // namespace icts_test
