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
 * @file FastSTATimingLookup.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Shared Liberty, RC and transition lookup for timing propagation.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

#include "clock_state/FastSTAClockState.hh"
#include "liberty/FastSTALibertyModel.hh"

namespace icts::fast_sta {

struct ArcIndexSelection
{
  std::vector<std::size_t> indexes;
  std::size_t fallback_count = 0U;
  std::size_t extrema_bundle_count = 0U;
};

template <typename Predicate>
auto SelectArcIndexes(const FastStaLibertyCell& cell, Predicate predicate) -> ArcIndexSelection
{
  ArcIndexSelection selection;
  std::array<std::vector<std::size_t>, 4U> indexes_by_sense;
  for (std::size_t index = 0U; index < cell.timing_arcs.size(); ++index) {
    const auto& arc = cell.timing_arcs.at(index);
    if (predicate(arc)) {
      const auto sense_index = (arc.positive_unate ? 1U : 0U) | (arc.negative_unate ? 2U : 0U);
      indexes_by_sense.at(sense_index).push_back(index);
    }
  }
  for (const auto& indexes : indexes_by_sense) {
    if (indexes.empty()) {
      continue;
    }
    selection.indexes.insert(selection.indexes.end(), indexes.begin(), indexes.end());
    selection.extrema_bundle_count += std::ranges::any_of(indexes, [&](std::size_t index) -> bool { return cell.timing_arcs.at(index).conditional; }) ? 1U : 0U;
  }
  std::ranges::sort(selection.indexes);
  return selection;
}

inline auto TransitionIndex(FastStaTransition transition) -> std::size_t
{
  return transition == FastStaTransition::kRise ? 0U : 1U;
}

inline auto OppositeTransition(FastStaTransition transition) -> FastStaTransition
{
  return transition == FastStaTransition::kRise ? FastStaTransition::kFall : FastStaTransition::kRise;
}

inline auto FindRcTerminalElmore(const FastStaNetParasitic& parasitic, FastStaRcNodeId rc_node_id, std::size_t analysis_index = 1U,
                                 std::size_t transition_index = 0U) -> double
{
  if (rc_node_id >= parasitic.rc_nodes.size()) {
    return 0.0;
  }
  const auto& rc_node = parasitic.rc_nodes.at(rc_node_id);
  const auto profiled_delay = rc_node.elmore_delay_ns_by_timing.at(analysis_index).at(transition_index);
  return parasitic.valid ? profiled_delay : rc_node.elmore_delay_ns;
}

inline auto FindLoadLibertyCell(const FastStaContext& context, FastStaNodeId node_id) -> const FastStaLibertyCell*
{
  if (node_id >= context.nodes.size()) {
    return nullptr;
  }
  const auto& node = context.nodes.at(node_id);
  if (node.cell_master.empty()) {
    return nullptr;
  }
  const auto iter = context.liberty_cell_by_master.find(node.cell_master);
  return iter == context.liberty_cell_by_master.end() ? nullptr : &iter->second;
}

}  // namespace icts::fast_sta
