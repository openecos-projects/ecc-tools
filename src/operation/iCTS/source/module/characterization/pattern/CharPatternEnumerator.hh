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
 * @file CharPatternEnumerator.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-20
 * @brief CharBuilder topology / buffer-combination enumeration component.
 *        Maps each wirelength to its candidate buffering patterns by lazily
 *        iterating every buffer-slot selection and every monotonic-non-decreasing
 *        buffer-master combination, then dispatches each (topology, masters)
 *        pair to CharStaSampler.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace icts::char_builder::detail {

class CharBuilderImpl;
struct BuildProgress;
using TopologySlotSelection = std::vector<std::uint8_t>;

class CharPatternEnumerator
{
 public:
  explicit CharPatternEnumerator(CharBuilderImpl& impl) : _impl(impl) {}
  ~CharPatternEnumerator() = default;
  CharPatternEnumerator(const CharPatternEnumerator&) = delete;
  auto operator=(const CharPatternEnumerator&) -> CharPatternEnumerator& = delete;

  auto calcTopologySlotCount(double wirelength_um) const -> unsigned;
  static auto countSelectedSlots(const TopologySlotSelection& selected_slots) -> unsigned;
  static auto estimatePatternCount(unsigned num_slots, std::size_t num_buf_types) -> std::size_t;
  auto estimatePatternCountPerWirelength(double wirelength_um) const -> std::size_t;
  auto enumerateWirelength(unsigned length_idx, double wirelength_um, BuildProgress& build_progress) -> void;

 private:
  auto enumerateTopology(unsigned length_idx, double wirelength_um, const TopologySlotSelection& selected_slots, BuildProgress& build_progress) -> void;
  static auto advanceToNextSlotSelection(TopologySlotSelection& selected_slots) -> bool;
  static auto getMonotonicComboCount(std::size_t num_buf_types, std::size_t num_positions) -> std::size_t;
  static auto advanceToNextMonotonic(std::vector<std::size_t>& buf_indices, std::size_t num_buf_types) -> bool;

  CharBuilderImpl& _impl;
};

}  // namespace icts::char_builder::detail
