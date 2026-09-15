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
 * @file TopologyRealTechHTreeAssertions.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Real-tech Topology H-tree result assertions.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "Inst.hh"
#include "Pin.hh"
#include "TopologyRealTechScenario.hh"
#include "Tree.hh"
#include "data_manager/config/Config.hh"
#include "data_manager/design/Net.hh"
#include "synthesis/htree/HTree.hh"

namespace icts_test::synthesis_realtech_smoke {
namespace {

auto FindLeafLevelPlan(const icts::HTree::Output& htree_output) -> const icts::HTree::LevelPlan*
{
  const auto level_it = std::ranges::find_if(htree_output.levels, [](const icts::HTree::LevelPlan& level) -> bool { return level.is_leaf_level; });
  if (level_it == htree_output.levels.end()) {
    return nullptr;
  }
  return &(*level_it);
}

auto FindUniqueOutputPin(const icts::Inst* inst) -> const icts::Pin*
{
  const icts::Pin* output = nullptr;
  for (const auto* pin : inst->get_pins()) {
    if (pin == nullptr || pin->get_type() != icts::PinType::kOut) {
      continue;
    }
    if (output != nullptr) {
      return nullptr;
    }
    output = pin;
  }
  return output;
}

template <typename Output>
auto AssertExplicitSynthesizedPropagationPayload(const Output& output) -> void
{
  ASSERT_EQ(output.propagation_arcs.size(), output.inserted_insts.size());
  std::unordered_set<const icts::Inst*> inserted_insts;
  std::unordered_set<const icts::Pin*> inserted_pins;
  for (const auto& inst : output.inserted_insts) {
    inserted_insts.insert(inst.get());
  }
  for (const auto& pin : output.inserted_pins) {
    inserted_pins.insert(pin.get());
  }
  for (const auto& arc : output.propagation_arcs) {
    EXPECT_EQ(arc.origin, icts::ClockPropagationOrigin::kSynthesized);
    EXPECT_TRUE(inserted_insts.contains(arc.inst));
    EXPECT_TRUE(inserted_pins.contains(arc.input_pin));
    EXPECT_TRUE(inserted_pins.contains(arc.output_pin));
    ASSERT_NE(arc.inst, nullptr);
    EXPECT_EQ(arc.input_pin == nullptr ? nullptr : arc.input_pin->get_inst(), arc.inst);
    EXPECT_EQ(arc.output_pin == nullptr ? nullptr : arc.output_pin->get_inst(), arc.inst);
  }
}

}  // namespace

auto AssertNoSingleLoadExternalLeafBuffer(const icts::HTree::Output& htree_output) -> void
{
  std::unordered_set<const icts::Inst*> inserted_insts;
  inserted_insts.reserve(htree_output.inserted_insts.size());
  for (const auto& inst_owner : htree_output.inserted_insts) {
    inserted_insts.insert(inst_owner.get());
  }

  for (const auto& inst_owner : htree_output.inserted_insts) {
    const auto* inst = inst_owner.get();
    if (inst == nullptr || !inst->is_buffer()) {
      continue;
    }

    const auto* output_pin = FindUniqueOutputPin(inst);
    ASSERT_NE(output_pin, nullptr);
    const auto* output_net = output_pin->get_net();
    if (output_net == nullptr || output_net->get_loads().size() != 1U || output_net->get_loads().front() == nullptr) {
      continue;
    }

    const auto* downstream_load = output_net->get_loads().front();
    const auto* downstream_inst = downstream_load->get_inst();
    const bool drives_internal_htree_inst = downstream_inst != nullptr && inserted_insts.contains(downstream_inst);
    EXPECT_TRUE(drives_internal_htree_inst) << "Expected leaf single-load buffer pruning to remove " << inst->get_name() << " but it still drives "
                                            << downstream_load->get_name();
  }
}

auto AssertTopologyHTreePayload(const icts::Topology::Build& result) -> void
{
  ASSERT_TRUE(result.summary.success);
  AssertExplicitSynthesizedPropagationPayload(result.output);
  const auto& htree_output = result.output.htree_output;
  ASSERT_FALSE(htree_output.levels.empty());
  ASSERT_TRUE(result.summary.selected_htree_depth.has_value());
  EXPECT_EQ(result.summary.selected_htree_depth.value_or(0U), htree_output.levels.size());
  EXPECT_EQ(result.summary.selected_htree_level_count, htree_output.levels.size());
  EXPECT_LE(result.summary.htree_inserted_buffer_count, result.output.inserted_insts.size());
  EXPECT_LE(result.summary.htree_inserted_net_count, result.output.inserted_nets.size());
  EXPECT_TRUE(htree_output.best_char.has_value());
}

auto AssertBranchBufferedHTreePayload(const icts::HTree::Output& htree_output) -> void
{
  ASSERT_FALSE(htree_output.levels.empty());
  AssertExplicitSynthesizedPropagationPayload(htree_output);

  const auto* leaf_level = FindLeafLevelPlan(htree_output);
  ASSERT_NE(leaf_level, nullptr);
  EXPECT_TRUE(leaf_level->selected_has_terminal_branch_buffer);
  EXPECT_FALSE(leaf_level->selected_terminal_cell_master.empty());
}

auto AssertSelectedTopologyDepth(const icts::Topology::Build& result) -> void
{
  ASSERT_TRUE(result.summary.selected_htree_depth.has_value());

  const auto& htree_output = result.output.htree_output;
  const auto topology_levels = htree_output.topology.levels();
  ASSERT_GT(topology_levels.size(), 1U);
  EXPECT_EQ(result.summary.selected_htree_depth.value_or(0U), htree_output.levels.size());
  EXPECT_LE(result.summary.selected_htree_depth.value_or(0U), static_cast<unsigned>(topology_levels.size() - 1U));
  EXPECT_TRUE(result.summary.success);
}

}  // namespace icts_test::synthesis_realtech_smoke
