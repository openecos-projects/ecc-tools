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
 * @file CharPatternEnumerator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-20
 * @brief Buffer-topology and monotonic buffer-combination enumeration.
 */

#include "characterization/pattern/CharPatternEnumerator.hh"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <vector>

#include "Logger.hh"
#include "ValueLattice.hh"
#include "characterization/buffer_cell/CharacterizationBufferCell.hh"
#include "characterization/builder/CharBuilderImpl.hh"
#include "characterization/builder/CharTopologyPlanner.hh"
#include "characterization/sampling/CharSTASampler.hh"

namespace icts::char_builder::detail {
namespace {

auto SaturatingAdd(std::size_t lhs, std::size_t rhs) -> std::size_t
{
  const auto maximum = std::numeric_limits<std::size_t>::max();
  return rhs > maximum - lhs ? maximum : lhs + rhs;
}

auto SaturatingMultiply(std::size_t lhs, std::size_t rhs) -> std::size_t
{
  const auto maximum = std::numeric_limits<std::size_t>::max();
  return lhs != 0U && rhs > maximum / lhs ? maximum : lhs * rhs;
}

auto SaturatingCombination(std::size_t n, std::size_t k) -> std::size_t
{
  if (k > n) {
    return 0U;
  }
  k = std::min(k, n - k);
  unsigned __int128 result = 1U;
  const auto maximum = std::numeric_limits<std::size_t>::max();
  for (std::size_t index = 0U; index < k; ++index) {
    result = result * static_cast<unsigned __int128>(n - index) / static_cast<unsigned __int128>(index + 1U);
    if (result > maximum) {
      return maximum;
    }
  }
  return static_cast<std::size_t>(result);
}

}  // namespace

auto CharPatternEnumerator::calcTopologySlotCount(double wirelength_um) const -> unsigned
{
  const ::icts::UniformValueLattice length_lattice(_impl._length_unit_um, _impl._wirelength_iterations);
  const auto length_idx = length_lattice.tryObservedIndex(wirelength_um);
  return length_idx.value_or(length_lattice.coveringIndex(wirelength_um));
}

auto CharPatternEnumerator::countSelectedSlots(const TopologySlotSelection& selected_slots) -> unsigned
{
  return static_cast<unsigned>(std::ranges::count_if(selected_slots, [](std::uint8_t selected) -> bool { return selected != 0U; }));
}

auto CharPatternEnumerator::estimatePatternCount(unsigned num_slots, std::size_t num_buf_types) -> std::size_t
{
  std::size_t total_patterns = 1U;
  for (unsigned num_buffer_positions = 1U; num_buffer_positions <= num_slots; ++num_buffer_positions) {
    const auto topology_count = SaturatingCombination(num_slots, num_buffer_positions);
    const auto master_count = getMonotonicComboCount(num_buf_types, num_buffer_positions);
    total_patterns = SaturatingAdd(total_patterns, SaturatingMultiply(topology_count, master_count));
    if (total_patterns == std::numeric_limits<std::size_t>::max()) {
      return total_patterns;
    }
  }
  return total_patterns;
}

auto CharPatternEnumerator::estimatePatternCountPerWirelength(double wirelength_um) const -> std::size_t
{
  if (_impl._use_boundary_primitive_patterns) {
    return SaturatingAdd(1U, _impl._sorted_buffers.size());
  }
  const unsigned num_slots = calcTopologySlotCount(wirelength_um);
  return estimatePatternCount(num_slots, _impl._sorted_buffers.size());
}

auto CharPatternEnumerator::enumerateWirelength(unsigned length_idx, double wirelength_um, BuildProgress& build_progress) -> void
{
  const unsigned num_slots = calcTopologySlotCount(wirelength_um);
  TopologySlotSelection selected_slots(num_slots, 0U);
  enumerateTopology(length_idx, wirelength_um, selected_slots, build_progress);
  if (_impl._use_boundary_primitive_patterns) {
    if (!selected_slots.empty()) {
      selected_slots.back() = 1U;
      enumerateTopology(length_idx, wirelength_um, selected_slots, build_progress);
    }
    return;
  }
  while (advanceToNextSlotSelection(selected_slots)) {
    enumerateTopology(length_idx, wirelength_um, selected_slots, build_progress);
  }
}

auto CharPatternEnumerator::advanceToNextSlotSelection(TopologySlotSelection& selected_slots) -> bool
{
  for (auto& selected : selected_slots) {
    if (selected == 0U) {
      selected = 1U;
      return true;
    }
    selected = 0U;
  }
  return false;
}

auto CharPatternEnumerator::enumerateTopology(unsigned length_idx, double wirelength_um, const TopologySlotSelection& selected_slots,
                                              BuildProgress& build_progress) -> void
{
  const TopologyDesc topo = _impl.topologyPlanner().buildTopologyDesc(wirelength_um, selected_slots);
  const std::size_t num_buf_positions = topo.buffer_positions.size();

  if (num_buf_positions == 0) {
    const std::vector<std::string> empty_masters;
    _impl.staSampler().characterizeTopology(length_idx, topo, empty_masters, build_progress);
    return;
  }

  const std::size_t num_buf_types = _impl._sorted_buffers.size();
  if (num_buf_types == 0) {
    return;
  }

  std::vector<std::size_t> buf_indices(num_buf_positions, 0);
  while (true) {
    std::vector<std::string> buf_masters;
    buf_masters.reserve(num_buf_positions);
    for (const auto buffer_index : std::ranges::reverse_view(buf_indices)) {
      buf_masters.push_back(_impl._sorted_buffers.at(buffer_index).cell_master);
    }

    _impl.staSampler().characterizeTopology(length_idx, topo, buf_masters, build_progress);
    if (!advanceToNextMonotonic(buf_indices, num_buf_types)) {
      break;
    }
  }
}

auto CharPatternEnumerator::getMonotonicComboCount(std::size_t num_buf_types, std::size_t num_positions) -> std::size_t
{
  if (num_buf_types == 0 || num_positions == 0) {
    return 0;
  }
  if (num_positions > std::numeric_limits<std::size_t>::max() - num_buf_types + 1U) {
    return std::numeric_limits<std::size_t>::max();
  }
  const std::size_t combination_n = num_buf_types + num_positions - 1;
  return SaturatingCombination(combination_n, num_positions);
}

auto CharPatternEnumerator::advanceToNextMonotonic(std::vector<std::size_t>& buf_indices, std::size_t num_buf_types) -> bool
{
  if (buf_indices.empty() || num_buf_types == 0) {
    return false;
  }

  int position = static_cast<int>(buf_indices.size()) - 1;

  while (position >= 0) {
    const auto index = static_cast<std::size_t>(position);
    if (buf_indices.at(index) + 1 < num_buf_types) {
      ++buf_indices.at(index);
      for (std::size_t tail_index = index + 1; tail_index < buf_indices.size(); ++tail_index) {
        buf_indices.at(tail_index) = buf_indices.at(index);
      }
      return true;
    }
    --position;
  }

  return false;
}

}  // namespace icts::char_builder::detail
