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

#include "Inst.hh"
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
    const auto index = _pins.size();
    auto inst = std::make_unique<icts::Inst>("split_inst_" + std::to_string(index), "DFF_X1", icts::InstType::kFlipFlop, icts::Point<int>(x, y));
    auto* inst_ptr = inst.get();
    auto pin = std::make_unique<icts::Pin>("CLK", icts::PinType::kClock, icts::Point<int>(x, y), inst_ptr, nullptr, false);
    inst_ptr->add_pin(pin.get());
    _insts.push_back(std::move(inst));
    _pins.push_back(std::move(pin));
    return _pins.back().get();
  }

 private:
  std::vector<std::unique_ptr<icts::Inst>> _insts;
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

auto makeRecoveryInput(const std::vector<icts::Pin*>& loads, double pin_cap_pf, std::size_t max_fanout, double max_cap_pf)
    -> icts::htree::SinkLoadRegionLegalityInput
{
  icts::htree::SinkLoadRegionLegalityInput input{
      .max_fanout = max_fanout,
      .has_max_cap = true,
      .max_cap_pf = max_cap_pf,
      .clock_route_segment_rc = {.dbu_per_um = 1000, .resistance_per_um_ohm = 0.01, .capacitance_per_um_pf = 0.000001},
      .split_buffer_input_cap_pf = 0.01,
      .split_buffer_available = true,
      .sink_pin_cap_pf_by_pin = {},
  };
  for (const auto* load : loads) {
    input.sink_pin_cap_pf_by_pin.emplace(load, pin_cap_pf);
  }
  return input;
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

TEST(SinkLoadRegionSplitTest, FanoutLegalCapIllegalGroupUsesDeterministicRecursiveRecovery)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(27U);
  for (int index = 0; index < 27; ++index) {
    loads.push_back(factory.make(index * 7, (index % 3) * 5));
  }
  const auto input = makeRecoveryInput(loads, 0.02, 32U, 0.15);
  const auto plan = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), input);
  ASSERT_TRUE(plan.feasible) << plan.failure_reason;
  EXPECT_TRUE(plan.required);
  EXPECT_FALSE(plan.triggered_by_fanout);
  EXPECT_TRUE(plan.triggered_by_capacitance);
  EXPECT_EQ(plan.original_loads.size(), loads.size());
  EXPECT_GT(plan.buffer_count, 0U);
  EXPECT_LE(plan.root_cap_pf, input.max_cap_pf);

  std::ranges::reverse(loads);
  const auto reordered = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), input);
  ASSERT_TRUE(reordered.feasible) << reordered.failure_reason;
  EXPECT_EQ(reordered.subgroups, plan.subgroups);
  EXPECT_EQ(reordered.centers, plan.centers);
  EXPECT_EQ(reordered.original_loads, plan.original_loads);
  EXPECT_EQ(reordered.buffer_count, plan.buffer_count);
  EXPECT_EQ(reordered.local_depth, plan.local_depth);
}

TEST(SinkLoadRegionSplitTest, CoincidentCommonPinNamesUseFullIdentityForDeterministicRecovery)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(33U);
  for (int index = 0; index < 33; ++index) {
    loads.push_back(factory.make(0, 0));
  }
  const auto input = makeRecoveryInput(loads, 0.02, 64U, 0.15);
  const auto reference = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), input);
  ASSERT_TRUE(reference.feasible) << reference.failure_reason;

  std::ranges::reverse(loads);
  const auto reordered = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), input);
  ASSERT_TRUE(reordered.feasible) << reordered.failure_reason;
  EXPECT_EQ(reordered.original_loads, reference.original_loads);
  EXPECT_EQ(reordered.subgroups, reference.subgroups);
  EXPECT_EQ(reordered.centers, reference.centers);
  EXPECT_EQ(reordered.buffer_count, reference.buffer_count);
  EXPECT_EQ(reordered.local_depth, reference.local_depth);
}

TEST(SinkLoadRegionSplitTest, SingleLoadCapFailureIsTyped)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads = {factory.make(0, 0)};
  const auto input = makeRecoveryInput(loads, 0.2, 32U, 0.1);
  const auto plan = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), input);
  EXPECT_FALSE(plan.feasible);
  EXPECT_EQ(plan.failure, icts::htree::SinkLoadRegionRecoveryFailure::kSingleLoadCapacitance);
  EXPECT_NE(plan.failure_reason.find("single_load_cap_violation"), std::string::npos);
}

TEST(SinkLoadRegionSplitTest, MissingBufferCandidateAndRecoveryLimitsAreTyped)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads;
  loads.reserve(16U);
  for (int index = 0; index < 16; ++index) {
    loads.push_back(factory.make(index, 0));
  }
  auto no_buffer_input = makeRecoveryInput(loads, 0.02, 32U, 0.05);
  no_buffer_input.split_buffer_available = false;
  const auto no_buffer = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), no_buffer_input);
  EXPECT_EQ(no_buffer.failure, icts::htree::SinkLoadRegionRecoveryFailure::kNoBufferCandidate);

  auto depth_limited_input = makeRecoveryInput(loads, 0.02, 32U, 0.05);
  depth_limited_input.max_split_depth = 1U;
  const auto depth_limited = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), depth_limited_input);
  EXPECT_EQ(depth_limited.failure, icts::htree::SinkLoadRegionRecoveryFailure::kDepthLimit);

  auto count_limited_input = makeRecoveryInput(loads, 0.02, 32U, 0.05);
  count_limited_input.max_split_buffer_count = 1U;
  const auto count_limited = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(0, 0), count_limited_input);
  EXPECT_EQ(count_limited.failure, icts::htree::SinkLoadRegionRecoveryFailure::kBufferLimit);
}

TEST(SinkLoadRegionSplitTest, BufferInputCapNoProgressIsTyped)
{
  SplitPinFactory factory;
  std::vector<icts::Pin*> loads = {factory.make(0, 0), factory.make(10, 0)};
  auto input = makeRecoveryInput(loads, 0.04, 32U, 0.05);
  input.split_buffer_input_cap_pf = 0.04;
  const auto plan = icts::htree::RecoverSinkLoadRegionGroup(loads, icts::Point<int>(5, 0), input);
  EXPECT_FALSE(plan.feasible);
  EXPECT_EQ(plan.failure, icts::htree::SinkLoadRegionRecoveryFailure::kNoProgress);
  EXPECT_NE(plan.failure_reason.find("split_root_cap_violation"), std::string::npos);
}

}  // namespace
}  // namespace icts_test
