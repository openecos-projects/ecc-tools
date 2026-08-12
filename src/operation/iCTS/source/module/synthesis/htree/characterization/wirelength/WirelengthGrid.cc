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
 * @file WirelengthGrid.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-01
 * @brief H-tree characterization wirelength grid resolution implementation.
 */

#include "synthesis/htree/characterization/wirelength/WirelengthGrid.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ostream>
#include <ranges>
#include <vector>

#include "Logger.hh"
#include "Tree.hh"
#include "ValueLattice.hh"
#include "config/Config.hh"
#include "geometry/Geometry.hh"

namespace icts::htree {
namespace {

struct GridUnitCandidateScore
{
  double unit_um = 0.0;
  double total_overmodeled_length_um = 0.0;
  double total_prefix_overmodeled_length_um = 0.0;
  double mean_prefix_relative_error = 0.0;
  double max_relative_error = 0.0;
  unsigned unique_direct_bins = 0U;
  unsigned max_direct_length_idx = 0U;
  unsigned required_covering_iterations = 0U;
  bool valid = false;
};

auto MakeCoveringLengthIndex(double length_um, double length_step_um) -> unsigned
{
  return UniformValueLattice(length_step_um, std::numeric_limits<unsigned>::max()).coveringIndex(length_um);
}

auto AppendPositiveLengths(std::vector<double>& target, const std::vector<double>& values) -> void
{
  for (const double value : values) {
    if (value > 0.0) {
      target.push_back(value);
    }
  }
}

auto BuildPositiveLengths(const std::vector<double>& values) -> std::vector<double>
{
  std::vector<double> result;
  result.reserve(values.size());
  AppendPositiveLengths(result, values);
  return result;
}

auto MakeCombinedLengths(const std::vector<double>& lhs, const std::vector<double>& rhs) -> std::vector<double>
{
  std::vector<double> result;
  result.reserve(lhs.size() + rhs.size());
  AppendPositiveLengths(result, lhs);
  AppendPositiveLengths(result, rhs);
  return result;
}

auto MaxPositiveLength(const std::vector<double>& lengths_um) -> double
{
  if (lengths_um.empty()) {
    return 0.0;
  }
  return *std::ranges::max_element(lengths_um);
}

auto ScoreGridUnitCandidate(const std::vector<double>& direct_lengths_um, const std::vector<double>& coverage_lengths_um, double unit_um,
                            unsigned direct_length_idx_budget) -> GridUnitCandidateScore
{
  if (direct_lengths_um.empty() || unit_um <= 0.0 || direct_length_idx_budget == 0U) {
    return {};
  }

  GridUnitCandidateScore score;
  score.unit_um = unit_um;
  score.valid = true;
  std::vector<unsigned> direct_length_indices;
  std::vector<double> overmodeled_lengths_um;
  overmodeled_lengths_um.reserve(direct_lengths_um.size());
  direct_length_indices.reserve(direct_lengths_um.size());
  for (const double direct_length_um : direct_lengths_um) {
    const unsigned length_idx = MakeCoveringLengthIndex(direct_length_um, unit_um);
    if (length_idx == 0U || length_idx > direct_length_idx_budget) {
      return {};
    }
    const double modeled_length_um = static_cast<double>(length_idx) * unit_um;
    const double overmodeled_length_um = std::max(0.0, modeled_length_um - direct_length_um);
    score.total_overmodeled_length_um += overmodeled_length_um;
    if (direct_length_um > 0.0) {
      score.max_relative_error = std::max(score.max_relative_error, overmodeled_length_um / direct_length_um);
    }
    score.max_direct_length_idx = std::max(score.max_direct_length_idx, length_idx);
    overmodeled_lengths_um.push_back(overmodeled_length_um);
    direct_length_indices.push_back(length_idx);
  }

  double prefix_requested_length_um = 0.0;
  double prefix_overmodeled_length_um = 0.0;
  // H-tree depth candidates consume prefixes of the full level list; scoring
  // the prefix error keeps the grid aligned with both native and analytical
  // candidate selection instead of optimizing only the final full-depth sum.
  for (std::size_t length_index = 0; length_index < direct_lengths_um.size(); ++length_index) {
    prefix_requested_length_um += direct_lengths_um.at(length_index);
    prefix_overmodeled_length_um += overmodeled_lengths_um.at(length_index);
    score.total_prefix_overmodeled_length_um += prefix_overmodeled_length_um;
    if (prefix_requested_length_um > 0.0) {
      score.mean_prefix_relative_error += prefix_overmodeled_length_um / prefix_requested_length_um;
    }
  }
  score.mean_prefix_relative_error /= static_cast<double>(direct_lengths_um.size());

  std::ranges::sort(direct_length_indices);
  const auto unique_tail = std::ranges::unique(direct_length_indices);
  direct_length_indices.erase(unique_tail.begin(), unique_tail.end());
  score.unique_direct_bins = static_cast<unsigned>(direct_length_indices.size());

  const double max_required_length_um = std::max(MaxPositiveLength(direct_lengths_um), MaxPositiveLength(coverage_lengths_um));
  score.required_covering_iterations = max_required_length_um > 0.0 ? std::max(1U, static_cast<unsigned>(std::ceil(max_required_length_um / unit_um))) : 0U;
  return score;
}

auto IsBetterGridUnitCandidate(const GridUnitCandidateScore& candidate, const GridUnitCandidateScore& current_best) -> bool
{
  if (!candidate.valid) {
    return false;
  }
  if (!current_best.valid) {
    return true;
  }

  const double mean_prefix_relative_diff = candidate.mean_prefix_relative_error - current_best.mean_prefix_relative_error;
  if (std::abs(mean_prefix_relative_diff) > kValueLatticeEpsilon) {
    return mean_prefix_relative_diff < 0.0;
  }
  const double total_prefix_delta_diff = candidate.total_prefix_overmodeled_length_um - current_best.total_prefix_overmodeled_length_um;
  if (std::abs(total_prefix_delta_diff) > kValueLatticeEpsilon) {
    return total_prefix_delta_diff < 0.0;
  }
  const double total_delta_diff = candidate.total_overmodeled_length_um - current_best.total_overmodeled_length_um;
  if (std::abs(total_delta_diff) > kValueLatticeEpsilon) {
    return total_delta_diff < 0.0;
  }
  if (candidate.unique_direct_bins != current_best.unique_direct_bins) {
    return candidate.unique_direct_bins > current_best.unique_direct_bins;
  }

  const double max_relative_diff = candidate.max_relative_error - current_best.max_relative_error;
  if (std::abs(max_relative_diff) > kValueLatticeEpsilon) {
    return max_relative_diff < 0.0;
  }
  if (candidate.max_direct_length_idx != current_best.max_direct_length_idx) {
    return candidate.max_direct_length_idx < current_best.max_direct_length_idx;
  }
  if (candidate.required_covering_iterations != current_best.required_covering_iterations) {
    return candidate.required_covering_iterations < current_best.required_covering_iterations;
  }
  return candidate.unit_um > current_best.unit_um;
}

auto AppendUniqueUnitCandidate(std::vector<double>& candidates, double unit_um) -> void
{
  if (unit_um <= 0.0) {
    return;
  }
  for (const double existing_unit_um : candidates) {
    if (std::abs(existing_unit_um - unit_um) <= kValueLatticeEpsilon) {
      return;
    }
  }
  candidates.push_back(unit_um);
}

auto ResolveAutoDerivedGridUnit(const std::vector<double>& direct_lengths_um, const std::vector<double>& coverage_lengths_um) -> GridUnitCandidateScore
{
  if (direct_lengths_um.empty()) {
    return {};
  }

  const auto direct_length_idx_budget = static_cast<unsigned>(direct_lengths_um.size());
  const double max_direct_length_um = MaxPositiveLength(direct_lengths_um);
  if (max_direct_length_um <= 0.0 || direct_length_idx_budget == 0U) {
    return {};
  }

  std::vector<double> candidates;
  candidates.reserve(direct_lengths_um.size() * direct_lengths_um.size() + 2U);
  AppendUniqueUnitCandidate(candidates, max_direct_length_um / static_cast<double>(direct_length_idx_budget));

  const auto all_lengths_um = MakeCombinedLengths(direct_lengths_um, coverage_lengths_um);
  const double max_required_length_um = MaxPositiveLength(all_lengths_um);
  if (!all_lengths_um.empty() && max_required_length_um > 0.0) {
    AppendUniqueUnitCandidate(candidates, max_required_length_um / static_cast<double>(all_lengths_um.size()));
  }

  for (const double direct_length_um : direct_lengths_um) {
    for (unsigned length_idx = 1U; length_idx <= direct_length_idx_budget; ++length_idx) {
      const double unit_um = direct_length_um / static_cast<double>(length_idx);
      if (MakeCoveringLengthIndex(max_direct_length_um, unit_um) <= direct_length_idx_budget) {
        AppendUniqueUnitCandidate(candidates, unit_um);
      }
    }
  }

  GridUnitCandidateScore best_score;
  for (const double unit_um : candidates) {
    const auto candidate_score = ScoreGridUnitCandidate(direct_lengths_um, coverage_lengths_um, unit_um, direct_length_idx_budget);
    if (IsBetterGridUnitCandidate(candidate_score, best_score)) {
      best_score = candidate_score;
    }
  }
  return best_score;
}

}  // namespace

auto ToCharGridSourceName(CharGridSource source) -> const char*
{
  switch (source) {
    case CharGridSource::kNone:
      return "none";
    case CharGridSource::kRuntimeConfig:
      return "runtime_config";
    case CharGridSource::kAutoDerived:
      return "auto_derived";
  }
  return "none";
}

auto CountUniqueAlignedLengthBins(const std::vector<double>& requested_lengths_um, double length_step_um) -> unsigned
{
  if (requested_lengths_um.empty() || length_step_um <= 0.0) {
    return 0U;
  }

  std::vector<unsigned> aligned_bins;
  aligned_bins.reserve(requested_lengths_um.size());
  for (const double requested_length_um : requested_lengths_um) {
    const unsigned aligned_idx = MakeCoveringLengthIndex(requested_length_um, length_step_um);
    if (aligned_idx > 0U) {
      aligned_bins.push_back(aligned_idx);
    }
  }

  if (aligned_bins.empty()) {
    return 0U;
  }

  std::ranges::sort(aligned_bins);
  const auto unique_tail = std::ranges::unique(aligned_bins);
  aligned_bins.erase(unique_tail.begin(), unique_tail.end());
  return static_cast<unsigned>(aligned_bins.size());
}

auto CollectRequestedLevelLengthsUm(const Tree& topology, int32_t dbu_per_um) -> std::vector<double>
{
  if (dbu_per_um <= 0) {
    CTSLOG.error(Loc::current(), "HTree: DBU-per-micron must be positive when collecting requested level lengths.");
  }
  std::vector<double> requested_lengths_um;
  const auto levels = topology.levels();
  if (levels.size() <= 1U) {
    return requested_lengths_um;
  }

  requested_lengths_um.reserve(levels.size() - 1U);
  for (std::size_t level = 1; level < levels.size(); ++level) {
    long long distance_sum = 0;
    std::size_t distance_count = 0;
    for (const auto node_id : levels.at(level)) {
      const auto* node = topology.get_node(node_id);
      if (node == nullptr || node->get_parent() == std::numeric_limits<std::size_t>::max()) {
        continue;
      }

      const auto* parent = topology.get_node(node->get_parent());
      if (parent == nullptr) {
        continue;
      }

      distance_sum += geometry::Manhattan(node->get_position(), parent->get_position());
      ++distance_count;
    }

    if (distance_count == 0U) {
      continue;
    }

    const int requested_length_dbu = static_cast<int>(std::llround(static_cast<double>(distance_sum) / static_cast<double>(distance_count)));
    const double requested_length_um = static_cast<double>(std::max(requested_length_dbu, 0)) / static_cast<double>(dbu_per_um);
    if (requested_length_um > 0.0) {
      requested_lengths_um.push_back(requested_length_um);
    }
  }

  return requested_lengths_um;
}

auto ResolveCharacterizationGridPlan(const Config& config, const std::vector<double>& requested_lengths_um) -> CharacterizationGridPlan
{
  CharBuilder::Config char_config;
  if (config.get_wirelength_unit_um() > 0.0) {
    char_config.wirelength_unit_um = config.get_wirelength_unit_um();
  }
  char_config.wirelength_iterations = config.get_wirelength_iterations();
  return ResolveCharacterizationGridPlan(char_config, requested_lengths_um);
}

auto ResolveCharacterizationGridPlan(const CharBuilder::Config& config, const std::vector<double>& requested_lengths_um) -> CharacterizationGridPlan
{
  return ResolveCharacterizationGridPlan(config, requested_lengths_um, {});
}

auto ResolveCharacterizationGridPlan(const CharBuilder::Config& config, const std::vector<double>& direct_lengths_um,
                                     const std::vector<double>& coverage_lengths_um) -> CharacterizationGridPlan
{
  CharacterizationGridPlan plan;
  auto direct_positive_lengths_um = BuildPositiveLengths(direct_lengths_um);
  auto coverage_positive_lengths_um = BuildPositiveLengths(coverage_lengths_um);
  if (direct_positive_lengths_um.empty() && !coverage_positive_lengths_um.empty()) {
    direct_positive_lengths_um = coverage_positive_lengths_um;
    coverage_positive_lengths_um.clear();
  }
  if (direct_positive_lengths_um.empty()) {
    return plan;
  }
  plan.requested_level_lengths = static_cast<unsigned>(direct_positive_lengths_um.size());

  const double configured_unit_um = config.wirelength_unit_um.value_or(0.0);
  plan.configured_wirelength_iterations = std::max(1U, config.wirelength_iterations.value_or(1U));
  plan.configured_wirelength_unit_um = configured_unit_um;
  plan.configured_wirelength_missing = configured_unit_um <= 0.0;

  double effective_unit_um = configured_unit_um;
  if (effective_unit_um > 0.0) {
    plan.unique_level_bins = CountUniqueAlignedLengthBins(direct_positive_lengths_um, effective_unit_um);
    plan.source = CharGridSource::kRuntimeConfig;
  }

  const bool grid_collapsed = configured_unit_um > 0.0 && direct_positive_lengths_um.size() > 1U && plan.unique_level_bins <= 1U;
  plan.configured_grid_collapsed = grid_collapsed;
  if (plan.configured_wirelength_missing || grid_collapsed) {
    const auto auto_grid_score = ResolveAutoDerivedGridUnit(direct_positive_lengths_um, coverage_positive_lengths_um);
    effective_unit_um = auto_grid_score.unit_um;
    plan.adapted = effective_unit_um > 0.0;
    plan.source = plan.adapted ? CharGridSource::kAutoDerived : CharGridSource::kNone;
    plan.auto_derived_wirelength_unit_um = effective_unit_um;
    plan.unique_level_bins = CountUniqueAlignedLengthBins(direct_positive_lengths_um, effective_unit_um);
  }

  if (!plan.adapted || effective_unit_um <= 0.0) {
    return plan;
  }

  plan.wirelength_unit_um = effective_unit_um;
  const double max_required_length_um = std::max(MaxPositiveLength(direct_positive_lengths_um), MaxPositiveLength(coverage_positive_lengths_um));
  plan.required_covering_iterations = std::max(1U, static_cast<unsigned>(std::ceil(max_required_length_um / effective_unit_um)));
  plan.wirelength_iterations = plan.required_covering_iterations;
  return plan;
}

auto ResolveCharacterizationGridPlan(const Config& config, const Tree& topology, int32_t dbu_per_um) -> CharacterizationGridPlan
{
  return ResolveCharacterizationGridPlan(config, CollectRequestedLevelLengthsUm(topology, dbu_per_um));
}

auto ResolveDirectCharacterizationLengthIndices(const Tree& topology, const CharacterizationGridPlan& char_grid_plan, int32_t dbu_per_um)
    -> std::vector<unsigned>
{
  return ResolveDirectCharacterizationLengthIndices(CollectRequestedLevelLengthsUm(topology, dbu_per_um), char_grid_plan);
}

auto ResolveDirectCharacterizationLengthIndices(const std::vector<double>& requested_lengths_um, const CharacterizationGridPlan& char_grid_plan)
    -> std::vector<unsigned>
{
  if (!char_grid_plan.adapted || char_grid_plan.wirelength_iterations == 0U) {
    return {};
  }

  std::vector<unsigned> required_length_indices;
  required_length_indices.reserve(requested_lengths_um.size());
  for (const double requested_length_um : requested_lengths_um) {
    const unsigned length_idx = MakeCoveringLengthIndex(requested_length_um, char_grid_plan.wirelength_unit_um);
    if (length_idx > 0U) {
      required_length_indices.push_back(length_idx);
    }
  }
  std::ranges::sort(required_length_indices);
  const auto unique_tail = std::ranges::unique(required_length_indices);
  required_length_indices.erase(unique_tail.begin(), unique_tail.end());
  std::erase_if(required_length_indices, [&](unsigned length_idx) -> bool { return length_idx > char_grid_plan.wirelength_iterations; });
  return required_length_indices;
}

}  // namespace icts::htree
