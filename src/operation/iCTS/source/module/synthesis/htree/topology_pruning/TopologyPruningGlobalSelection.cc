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
 * @file TopologyPruningGlobalSelection.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Global H-tree candidate Pareto selection and coverage filtering.
 */
#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "Logger.hh"
#include "PatternId.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts {
class Tree;

namespace htree {
struct BufferPatternLibrary;
}  // namespace htree
}  // namespace icts

namespace icts::htree {
namespace {

auto PreferDelayPowerTieBreak(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool
{
  return (lhs.get_driven_cap_idx() < rhs.get_driven_cap_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() < rhs.get_output_slew_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() < rhs.get_delay())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() < rhs.get_power())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() == rhs.get_power() && lhs.get_load_cap_idx() < rhs.get_load_cap_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() == rhs.get_power() && lhs.get_load_cap_idx() == rhs.get_load_cap_idx()
             && lhs.get_input_slew_idx() > rhs.get_input_slew_idx())
         || (lhs.get_driven_cap_idx() == rhs.get_driven_cap_idx() && lhs.get_output_slew_idx() == rhs.get_output_slew_idx()
             && lhs.get_delay() == rhs.get_delay() && lhs.get_power() == rhs.get_power() && lhs.get_load_cap_idx() == rhs.get_load_cap_idx()
             && lhs.get_input_slew_idx() == rhs.get_input_slew_idx() && lhs.get_pattern_id().pack() < rhs.get_pattern_id().pack());
}

auto PreferPowerMedianOrder(const HTreeTopologyChar& lhs, const HTreeTopologyChar& rhs) -> bool
{
  if (lhs.get_power() != rhs.get_power()) {
    return lhs.get_power() < rhs.get_power();
  }
  if (lhs.get_delay() != rhs.get_delay()) {
    return lhs.get_delay() < rhs.get_delay();
  }
  return PreferDelayPowerTieBreak(lhs, rhs);
}

auto BuildDelayPowerParetoFront(const std::vector<CandidateCharRef>& entries) -> std::vector<CandidateCharRef>
{
  std::vector<CandidateCharRef> sorted_entries;
  sorted_entries.reserve(entries.size());
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry == nullptr) {
      continue;
    }
    sorted_entries.push_back(entry_ref);
  }

  std::ranges::sort(sorted_entries, [](const CandidateCharRef& lhs, const CandidateCharRef& rhs) -> bool {
    if (lhs.entry == nullptr || rhs.entry == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null global candidate frontier entry encountered.");
    }
    if (lhs.entry->get_delay() != rhs.entry->get_delay()) {
      return lhs.entry->get_delay() < rhs.entry->get_delay();
    }
    if (lhs.entry->get_power() != rhs.entry->get_power()) {
      return lhs.entry->get_power() < rhs.entry->get_power();
    }
    return PreferPowerMedianOrder(*lhs.entry, *rhs.entry);
  });

  std::vector<CandidateCharRef> pareto_front;
  pareto_front.reserve(sorted_entries.size());
  double best_power_before_delay = std::numeric_limits<double>::infinity();
  std::size_t delay_group_begin = 0U;
  while (delay_group_begin < sorted_entries.size()) {
    std::size_t delay_group_end = delay_group_begin + 1U;
    while (delay_group_end < sorted_entries.size()
           && sorted_entries.at(delay_group_end).entry->get_delay() == sorted_entries.at(delay_group_begin).entry->get_delay()) {
      ++delay_group_end;
    }

    double delay_group_min_power = std::numeric_limits<double>::infinity();
    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      delay_group_min_power = std::min(delay_group_min_power, sorted_entries.at(index).entry->get_power());
    }

    for (std::size_t index = delay_group_begin; index < delay_group_end; ++index) {
      const auto& entry_ref = sorted_entries.at(index);
      if (best_power_before_delay <= entry_ref.entry->get_power() || delay_group_min_power < entry_ref.entry->get_power()) {
        continue;
      }
      pareto_front.push_back(entry_ref);
    }

    best_power_before_delay = std::min(best_power_before_delay, delay_group_min_power);
    delay_group_begin = delay_group_end;
  }
  std::ranges::sort(pareto_front, [](const CandidateCharRef& lhs, const CandidateCharRef& rhs) -> bool {
    if (lhs.entry == nullptr || rhs.entry == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null global candidate frontier entry encountered.");
    }
    return PreferPowerMedianOrder(*lhs.entry, *rhs.entry);
  });
  return pareto_front;
}

auto CalcFrontMinDelayNs(const std::vector<CandidateCharRef>& front) -> double
{
  if (front.empty()) {
    CTSLOG.error(Loc::current(), "HTree: empty global selection front.");
  }
  double front_min_delay = front.front().entry->get_delay();
  for (const auto& entry_ref : front) {
    if (entry_ref.entry == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: null global selection front entry.");
    }
    front_min_delay = std::min(front_min_delay, entry_ref.entry->get_delay());
  }
  return front_min_delay;
}

auto SelectMinDelayIndex(const std::vector<CandidateCharRef>& front) -> std::optional<std::size_t>
{
  if (front.empty()) {
    return std::nullopt;
  }

  std::size_t selected_index = 0U;
  for (std::size_t index = 1U; index < front.size(); ++index) {
    const auto& entry = *front.at(index).entry;
    const auto& selected_entry = *front.at(selected_index).entry;
    if (entry.get_delay() < selected_entry.get_delay() || (entry.get_delay() == selected_entry.get_delay() && entry.get_power() < selected_entry.get_power())) {
      selected_index = index;
    }
  }
  return selected_index;
}

auto SelectDelayMidpointPowerIndex(const std::vector<CandidateCharRef>& front, std::size_t median_index, std::size_t min_delay_index)
    -> std::optional<std::size_t>
{
  if (front.empty() || median_index >= front.size() || min_delay_index >= front.size()) {
    return std::nullopt;
  }

  const double median_delay = front.at(median_index).entry->get_delay();
  const double min_delay = front.at(min_delay_index).entry->get_delay();
  const double midpoint_delay = min_delay + (median_delay - min_delay) / 2.0;

  std::optional<std::size_t> selected_index;
  for (std::size_t index = 0U; index < front.size(); ++index) {
    const auto& entry = *front.at(index).entry;
    if (entry.get_delay() > midpoint_delay) {
      continue;
    }
    if (!selected_index.has_value()) {
      selected_index = index;
      continue;
    }
    const auto& selected_entry = *front.at(*selected_index).entry;
    if (entry.get_power() < selected_entry.get_power() || (entry.get_power() == selected_entry.get_power() && entry.get_delay() < selected_entry.get_delay())) {
      selected_index = index;
    }
  }
  return selected_index.has_value() ? selected_index : std::optional<std::size_t>{min_delay_index};
}

struct GlobalCandidateShape
{
  unsigned depth = 0U;
  std::size_t buffered_levels = 0U;
  unsigned physical_depth = 0U;
  std::size_t weighted_htree_buffer_count = 0U;
  std::size_t split_extra_buffer_count = 0U;
  std::size_t physical_buffer_count = 0U;
};

auto CountBufferedLevels(PatternId topology_pattern_id, const TopologyPatternLibrary& topology_pattern_library,
                         const BufferPatternLibrary& segment_pattern_library) -> std::size_t
{
  std::size_t buffered_levels = 0U;
  const auto topology_pattern = topology_pattern_library.materialize(topology_pattern_id);
  for (const auto segment_pattern_id : topology_pattern.get_level_segment_pattern_ids()) {
    const auto* segment_pattern = segment_pattern_library.find(segment_pattern_id);
    if (segment_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: missing segment pattern while comparing global candidate shape.");
    }
    if (!segment_pattern->get_cell_masters().empty()) {
      ++buffered_levels;
    }
  }
  return buffered_levels;
}

auto SaturatingAdd(std::size_t lhs, std::size_t rhs) -> std::size_t
{
  if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
    return std::numeric_limits<std::size_t>::max();
  }
  return lhs + rhs;
}

auto CalcLevelMultiplicity(std::size_t level_index, std::size_t branching_factor) -> std::size_t
{
  branching_factor = std::max<std::size_t>(1U, branching_factor);
  std::size_t multiplicity = 1U;
  for (std::size_t level = 0U; level < level_index; ++level) {
    if (multiplicity > std::numeric_limits<std::size_t>::max() / branching_factor) {
      return std::numeric_limits<std::size_t>::max();
    }
    multiplicity *= branching_factor;
  }
  return multiplicity;
}

auto CountWeightedHtreeBuffers(PatternId topology_pattern_id, const TopologyPatternLibrary& topology_pattern_library,
                               const BufferPatternLibrary& segment_pattern_library, std::size_t branching_factor) -> std::size_t
{
  std::size_t buffer_count = 0U;
  const auto topology_pattern = topology_pattern_library.materialize(topology_pattern_id);
  const auto& level_segment_pattern_ids = topology_pattern.get_level_segment_pattern_ids();
  for (std::size_t level_index = 0U; level_index < level_segment_pattern_ids.size(); ++level_index) {
    const auto* segment_pattern = segment_pattern_library.find(level_segment_pattern_ids.at(level_index));
    if (segment_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: missing segment pattern while comparing global candidate shape.");
    }
    const auto level_buffer_count = segment_pattern->get_cell_masters().size();
    const auto level_multiplicity = CalcLevelMultiplicity(level_index, branching_factor);
    if (level_buffer_count > 0U && level_multiplicity > std::numeric_limits<std::size_t>::max() / level_buffer_count) {
      buffer_count = std::numeric_limits<std::size_t>::max();
    } else {
      buffer_count = SaturatingAdd(buffer_count, level_multiplicity * level_buffer_count);
    }
  }
  return buffer_count;
}

auto BuildGlobalCandidateShape(const CandidateCharRef& entry_ref, const std::vector<CandidateBuildEvaluation>& evaluations,
                               const BufferPatternLibrary& segment_pattern_library) -> GlobalCandidateShape
{
  if (entry_ref.entry == nullptr) {
    CTSLOG.error(Loc::current(), "HTree: null global candidate while comparing shape.");
  }
  if (entry_ref.candidate_index >= evaluations.size()) {
    CTSLOG.error(Loc::current(), "HTree: global candidate index exceeds evaluated depth count.");
  }
  const auto& evaluation = evaluations.at(entry_ref.candidate_index);
  const auto weighted_htree_buffer_count = CountWeightedHtreeBuffers(entry_ref.entry->get_pattern_id(), evaluation.topology_pattern_library,
                                                                     segment_pattern_library, evaluation.topology_branching_factor);
  return GlobalCandidateShape{
      .depth = evaluation.depth,
      .buffered_levels = CountBufferedLevels(entry_ref.entry->get_pattern_id(), evaluation.topology_pattern_library, segment_pattern_library),
      .physical_depth = evaluation.depth + entry_ref.split_local_depth,
      .weighted_htree_buffer_count = weighted_htree_buffer_count,
      .split_extra_buffer_count = entry_ref.split_extra_buffer_count,
      .physical_buffer_count = SaturatingAdd(weighted_htree_buffer_count, entry_ref.split_extra_buffer_count),
  };
}

auto IsSameGlobalCandidateShape(const GlobalCandidateShape& lhs, const GlobalCandidateShape& rhs) -> bool
{
  return lhs.physical_depth == rhs.physical_depth && lhs.physical_buffer_count == rhs.physical_buffer_count && lhs.depth == rhs.depth
         && lhs.buffered_levels == rhs.buffered_levels;
}

auto IsNoMoreComplexShape(const GlobalCandidateShape& candidate_shape, const GlobalCandidateShape& reference_shape) -> bool
{
  return candidate_shape.physical_depth <= reference_shape.physical_depth && candidate_shape.physical_buffer_count <= reference_shape.physical_buffer_count;
}

auto FillSelectionDecision(const std::vector<CandidateCharRef>& front, GlobalSelectionDecision& selection_decision) -> void
{
  if (front.empty()) {
    CTSLOG.error(Loc::current(), "HTree: invalid empty global selection decision front.");
  }
  selection_decision.front_size = front.size();
  selection_decision.front_min_delay_ns = CalcFrontMinDelayNs(front);
}

auto PreferPhysicalComplexityOrder(const CandidateCharRef& lhs, const CandidateCharRef& rhs, const std::vector<CandidateBuildEvaluation>& evaluations,
                                   const BufferPatternLibrary& segment_pattern_library) -> bool
{
  const auto lhs_shape = BuildGlobalCandidateShape(lhs, evaluations, segment_pattern_library);
  const auto rhs_shape = BuildGlobalCandidateShape(rhs, evaluations, segment_pattern_library);
  if (lhs_shape.physical_buffer_count != rhs_shape.physical_buffer_count) {
    return lhs_shape.physical_buffer_count < rhs_shape.physical_buffer_count;
  }
  if (lhs_shape.physical_depth != rhs_shape.physical_depth) {
    return lhs_shape.physical_depth < rhs_shape.physical_depth;
  }
  if (lhs.entry->get_power() != rhs.entry->get_power()) {
    return lhs.entry->get_power() < rhs.entry->get_power();
  }
  if (lhs.entry->get_delay() != rhs.entry->get_delay()) {
    return lhs.entry->get_delay() < rhs.entry->get_delay();
  }
  return PreferDelayPowerTieBreak(*lhs.entry, *rhs.entry);
}

auto DominatesPhysically(const CandidateCharRef& lhs, const CandidateCharRef& rhs, const std::vector<CandidateBuildEvaluation>& evaluations,
                         const BufferPatternLibrary& segment_pattern_library) -> bool
{
  const auto lhs_shape = BuildGlobalCandidateShape(lhs, evaluations, segment_pattern_library);
  const auto rhs_shape = BuildGlobalCandidateShape(rhs, evaluations, segment_pattern_library);
  const bool no_worse = lhs.entry->get_delay() <= rhs.entry->get_delay() && lhs.entry->get_power() <= rhs.entry->get_power()
                        && lhs_shape.physical_depth <= rhs_shape.physical_depth && lhs_shape.physical_buffer_count <= rhs_shape.physical_buffer_count;
  const bool strictly_better = lhs.entry->get_delay() < rhs.entry->get_delay() || lhs.entry->get_power() < rhs.entry->get_power()
                               || lhs_shape.physical_depth < rhs_shape.physical_depth || lhs_shape.physical_buffer_count < rhs_shape.physical_buffer_count;
  return no_worse && strictly_better;
}

auto BuildPhysicalDelayPowerParetoFront(const std::vector<CandidateCharRef>& entries, const std::vector<CandidateBuildEvaluation>& evaluations,
                                        const BufferPatternLibrary& segment_pattern_library) -> std::vector<CandidateCharRef>
{
  std::vector<CandidateCharRef> filtered_entries;
  filtered_entries.reserve(entries.size());
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry != nullptr) {
      filtered_entries.push_back(entry_ref);
    }
  }

  std::vector<CandidateCharRef> pareto_front;
  pareto_front.reserve(filtered_entries.size());
  for (std::size_t candidate_index = 0U; candidate_index < filtered_entries.size(); ++candidate_index) {
    bool dominated = false;
    for (std::size_t other_index = 0U; other_index < filtered_entries.size(); ++other_index) {
      if (candidate_index == other_index) {
        continue;
      }
      if (DominatesPhysically(filtered_entries.at(other_index), filtered_entries.at(candidate_index), evaluations, segment_pattern_library)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      pareto_front.push_back(filtered_entries.at(candidate_index));
    }
  }

  std::ranges::sort(pareto_front, [&evaluations, &segment_pattern_library](const CandidateCharRef& lhs, const CandidateCharRef& rhs) -> bool {
    return PreferPhysicalComplexityOrder(lhs, rhs, evaluations, segment_pattern_library);
  });
  return pareto_front;
}

}  // namespace

auto CalcBoundaryRelaxationScore(const HTreeTopologyChar& entry, const BoundaryConstraints& boundary_constraints, unsigned slew_steps) -> double
{
  double score = 0.0;
  if (boundary_constraints.top_input_slew_covering_idx.has_value() && slew_steps > 0U) {
    score += static_cast<double>(entry.get_input_slew_idx()) / static_cast<double>(slew_steps);
  }
  return score;
}

auto BuildPerDepthDelayPowerParetoRefs(const std::vector<CandidateCharRef>& entries) -> std::vector<CandidateCharRef>
{
  std::vector<CandidateCharRef> pareto_entries;
  pareto_entries.reserve(entries.size());

  std::unordered_map<std::size_t, std::vector<CandidateCharRef>> entries_by_candidate;
  std::vector<std::size_t> candidate_indices;
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry == nullptr) {
      continue;
    }
    if (!entries_by_candidate.contains(entry_ref.candidate_index)) {
      candidate_indices.push_back(entry_ref.candidate_index);
    }
    entries_by_candidate[entry_ref.candidate_index].push_back(entry_ref);
  }

  for (const auto candidate_index : candidate_indices) {
    const auto group_it = entries_by_candidate.find(candidate_index);
    if (group_it == entries_by_candidate.end()) {
      CTSLOG.error(Loc::current(), "HTree: missing per-depth candidate group during Pareto compression.");
    }
    auto local_pareto = BuildDelayPowerParetoFront(group_it->second);
    pareto_entries.insert(pareto_entries.end(), local_pareto.begin(), local_pareto.end());
  }
  return pareto_entries;
}

auto SelectBestGlobalEntry(const std::vector<CandidateCharRef>& entries, GlobalSelectionDecision* selection_decision) -> std::optional<CandidateCharRef>
{
  if (entries.empty()) {
    return std::nullopt;
  }

  auto pareto_front = BuildDelayPowerParetoFront(entries);
  if (pareto_front.empty()) {
    return std::nullopt;
  }

  if (selection_decision != nullptr) {
    selection_decision->policy = "pareto_median";
    selection_decision->front_size = pareto_front.size();
    selection_decision->front_min_delay_ns = CalcFrontMinDelayNs(pareto_front);
  }

  return pareto_front.at((pareto_front.size() - 1U) / 2U);
}

auto SelectAdaptiveGlobalEntry(const std::vector<CandidateCharRef>& entries, const std::vector<CandidateBuildEvaluation>& evaluations,
                               const BufferPatternLibrary& segment_pattern_library, GlobalSelectionDecision* selection_decision)
    -> std::optional<CandidateCharRef>
{
  if (entries.empty()) {
    return std::nullopt;
  }

  auto pareto_front = BuildPhysicalDelayPowerParetoFront(entries, evaluations, segment_pattern_library);
  if (pareto_front.empty()) {
    return std::nullopt;
  }

  const auto median_index = std::optional<std::size_t>{(pareto_front.size() - 1U) / 2U};
  const auto min_delay_index = SelectMinDelayIndex(pareto_front);
  if (!median_index.has_value() || !min_delay_index.has_value()) {
    return std::nullopt;
  }
  const auto median_shape = BuildGlobalCandidateShape(pareto_front.at(*median_index), evaluations, segment_pattern_library);
  const auto min_delay_shape = BuildGlobalCandidateShape(pareto_front.at(*min_delay_index), evaluations, segment_pattern_library);
  std::size_t selected_index = *median_index;
  std::string selection_policy;
  std::string adaptive_reason;

  if (!IsSameGlobalCandidateShape(median_shape, min_delay_shape)) {
    if (IsNoMoreComplexShape(min_delay_shape, median_shape)) {
      selected_index = *min_delay_index;
      selection_policy = "adaptive_physical_shape_timing";
      adaptive_reason = "timing_candidate_reduces_physical_depth_or_buffer_count";
    } else {
      selection_policy = "adaptive_physical_shape_median";
      adaptive_reason = "timing_candidate_adds_physical_depth_or_buffer_count";
    }
  } else {
    const auto midpoint_index = SelectDelayMidpointPowerIndex(pareto_front, *median_index, *min_delay_index);
    if (!midpoint_index.has_value()) {
      return std::nullopt;
    }
    selected_index = *midpoint_index;
    selection_policy = "adaptive_physical_front_midpoint";
    adaptive_reason = "same_shape_use_delay_midpoint_power";
  }

  if (selection_decision != nullptr) {
    FillSelectionDecision(pareto_front, *selection_decision);
    selection_decision->policy = selection_policy;
    selection_decision->adaptive_reason = adaptive_reason;
  }

  return pareto_front.at(selected_index);
}

auto FilterGlobalEntriesBySinkLoadRegionCoverage(const std::vector<CandidateCharRef>& entries, const std::vector<CandidateBuildEvaluation>& evaluations,
                                                 const Tree& topology, const BufferPatternLibrary& segment_pattern_library,
                                                 SinkLoadRegionLegalityContext& legality_context) -> CandidateCharRefFilterBuild
{
  CandidateCharRefFilterBuild result;
  result.output.entries.reserve(entries.size());
  for (const auto& entry_ref : entries) {
    if (entry_ref.entry == nullptr || entry_ref.candidate_index >= evaluations.size()) {
      if (result.summary.first_failure_reason.empty()) {
        result.summary.first_failure_reason = "invalid_global_candidate_ref";
      }
      continue;
    }

    const auto& evaluation = evaluations[entry_ref.candidate_index];
    const auto legality = ResolveSinkLoadRegionLegality(topology, entry_ref.entry->get_pattern_id(), evaluation.topology_pattern_library,
                                                        segment_pattern_library, legality_context);
    if (!legality.legal) {
      if (result.summary.first_failure_reason.empty()) {
        result.summary.first_failure_reason = legality.failure_reason;
      }
      continue;
    }

    if (legality.required_leaf_load_cap_covering_idx.has_value() && entry_ref.entry->get_leaf_load_cap_idx() < *legality.required_leaf_load_cap_covering_idx) {
      if (result.summary.first_failure_reason.empty()) {
        std::ostringstream detail;
        detail << "sink_load_region_boundary_load_coverage_violation required_leaf_load_cap_idx=" << *legality.required_leaf_load_cap_covering_idx
               << ", entry_leaf_load_cap_idx=" << entry_ref.entry->get_leaf_load_cap_idx() << ", max_real_load_cap_pf=" << legality.required_leaf_load_cap_pf;
        result.summary.first_failure_reason = detail.str();
      }
      continue;
    }

    auto covered_ref = entry_ref;
    covered_ref.split_group_count = legality.split_group_count;
    covered_ref.split_extra_buffer_count = legality.split_extra_buffer_count;
    covered_ref.split_local_depth = legality.split_local_depth;
    result.output.entries.push_back(covered_ref);
  }
  return result;
}

}  // namespace icts::htree
