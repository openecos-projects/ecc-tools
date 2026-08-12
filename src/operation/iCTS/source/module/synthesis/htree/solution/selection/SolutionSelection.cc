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
 * @file SolutionSelection.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Selected H-tree solution metadata helpers.
 */

#include "synthesis/htree/solution/selection/SolutionSelection.hh"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyPattern.hh"
#include "Logger.hh"
#include "PatternId.hh"
#include "io/Wrapper.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"

namespace icts::htree {

auto ResolveSelectedRootDriverCellMaster(const std::vector<HTree::LevelPlan>& levels) -> std::string
{
  for (const auto& level : levels) {
    if (!level.selected_leaf_buffer_cell_master.empty()) {
      return level.selected_leaf_buffer_cell_master;
    }
    if (!level.selected_terminal_cell_master.empty()) {
      return level.selected_terminal_cell_master;
    }
  }
  return {};
}

namespace {

auto ResolveBinaryTopologyLevelMultiplicity(std::size_t level_index) -> std::size_t
{
  if (level_index >= static_cast<std::size_t>(std::numeric_limits<std::size_t>::digits - 1)) {
    return std::numeric_limits<std::size_t>::max();
  }
  return std::size_t{1U} << level_index;
}

auto ResolveTopologyLevelMultiplicity(const HTree::Build& result, std::size_t level_index) -> std::size_t
{
  const auto topology_levels = result.output.topology.levels();
  if (level_index < topology_levels.size()) {
    return topology_levels.at(level_index).size();
  }
  return ResolveBinaryTopologyLevelMultiplicity(level_index);
}

auto SaturatingMultiply(std::size_t lhs, std::size_t rhs) -> std::size_t
{
  if (lhs == 0U || rhs == 0U) {
    return 0U;
  }
  if (lhs > std::numeric_limits<std::size_t>::max() / rhs) {
    return std::numeric_limits<std::size_t>::max();
  }
  return lhs * rhs;
}

auto CalcCellMastersAreaUm2(Wrapper& wrapper, const std::vector<std::string>& cell_masters) -> std::optional<double>
{
  double area_um2 = 0.0;
  for (const auto& cell_master : cell_masters) {
    const auto cell_area_um2 = wrapper.queryCellAreaUm2(cell_master);
    if (!cell_area_um2.has_value()) {
      return std::nullopt;
    }
    area_um2 += *cell_area_um2;
  }
  return area_um2;
}

}  // namespace

auto ApplySelectedPatternToLevelPlans(Wrapper& wrapper, HTree::Build& result, const BufferPatternLibrary& segment_pattern_library) -> void
{
  if (!result.output.best_pattern.has_value()) {
    CTSLOG.error(Loc::current(), "HTree: selected topology pattern is missing.");
  }
  const auto& best_level_segment_pattern_ids = result.output.best_pattern->get_level_segment_pattern_ids();
  if (best_level_segment_pattern_ids.size() != result.output.levels.size()) {
    CTSLOG.error(Loc::current(), "HTree: best H-tree pattern level count does not match selected depth.");
  }

  for (std::size_t level_index = 0; level_index < result.output.levels.size(); ++level_index) {
    auto& level = result.output.levels.at(level_index);
    const auto segment_pattern_id = best_level_segment_pattern_ids.at(level_index);
    level.segment_pattern_id = segment_pattern_id;
    const auto* segment_pattern = segment_pattern_library.find(segment_pattern_id);
    if (segment_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: selected segment pattern metadata is missing.");
    }

    const auto& cell_masters = segment_pattern->get_cell_masters();
    const auto level_multiplicity = ResolveTopologyLevelMultiplicity(result, level_index);
    level.selected_has_any_buffer = !cell_masters.empty();
    level.selected_leaf_buffer_cell_master = cell_masters.empty() ? "" : cell_masters.back();
    level.selected_has_terminal_branch_buffer = segment_pattern->hasTerminalBranchBuffer();
    level.selected_terminal_cell_master = segment_pattern->hasTerminalBranchBuffer() && !cell_masters.empty() ? cell_masters.back() : "";
    level.selected_buffer_count = cell_masters.size();
    const auto selected_buffer_area_um2 = CalcCellMastersAreaUm2(wrapper, cell_masters);
    level.selected_buffer_area_um2 = selected_buffer_area_um2;
    level.selected_weighted_buffer_count = SaturatingMultiply(level_multiplicity, level.selected_buffer_count);
    level.selected_weighted_buffer_area_um2
        = selected_buffer_area_um2.has_value() ? std::optional<double>{static_cast<double>(level_multiplicity) * *selected_buffer_area_um2} : std::nullopt;
  }
}

}  // namespace icts::htree
