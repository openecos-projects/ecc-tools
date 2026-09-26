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
 * @file CharTopologyPlanner.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-20
 * @brief Converts topology slot selections into segment-wire descriptions.
 */

#include "characterization/builder/CharTopologyPlanner.hh"

#include <algorithm>
#include <cmath>

#include "ValueLattice.hh"
#include "characterization/builder/CharBuilderImpl.hh"

namespace icts::char_builder::detail {
namespace {

auto hasTerminalLatticeBuffer(double wirelength_um, double length_unit_um, const TopologySlotSelection& selected_slots) -> bool
{
  const auto num_slots = static_cast<unsigned>(selected_slots.size());
  if (wirelength_um <= 0.0 || length_unit_um <= 0.0 || num_slots == 0U) {
    return false;
  }

  const unsigned terminal_slot_index = num_slots - 1U;
  if (selected_slots.at(terminal_slot_index) == 0U) {
    return false;
  }

  const double terminal_slot_boundary_um = std::min(static_cast<double>(num_slots) * length_unit_um, wirelength_um);
  return std::abs(terminal_slot_boundary_um - wirelength_um) <= ::icts::kValueLatticeEpsilon;
}

}  // namespace

auto BuildTopologyDesc(double wirelength_um, double length_unit_um, const TopologySlotSelection& selected_slots) -> TopologyDesc
{
  TopologyDesc desc;
  const auto num_slots = static_cast<unsigned>(selected_slots.size());
  desc.has_terminal_branch_buffer = hasTerminalLatticeBuffer(wirelength_um, length_unit_um, selected_slots);

  if (num_slots == 0U || length_unit_um <= 0.0) {
    desc.wire_segments_um.push_back(wirelength_um);
    return desc;
  }

  double previous_boundary_um = 0.0;

  for (unsigned slot_index = 0; slot_index < num_slots; ++slot_index) {
    const double slot_boundary_um = std::min((static_cast<double>(slot_index) + 1.0) * length_unit_um, wirelength_um);
    if (selected_slots.at(slot_index) != 0U) {
      desc.buffer_positions.push_back(slot_index);
      desc.wire_segments_um.push_back(slot_boundary_um - previous_boundary_um);
      previous_boundary_um = slot_boundary_um;
    }
  }

  desc.wire_segments_um.push_back(std::max(0.0, wirelength_um - previous_boundary_um));

  return desc;
}

auto CharTopologyPlanner::buildTopologyDesc(double wirelength_um, const TopologySlotSelection& selected_slots) const -> TopologyDesc
{
  return BuildTopologyDesc(wirelength_um, _impl._length_unit_um, selected_slots);
}

}  // namespace icts::char_builder::detail
