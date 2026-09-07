// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file SourceTrunkLabelSolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-26
 * @brief Canonical label-setting solver for one source-trunk segment.
 */

#include "synthesis/topology/trunk/SourceTrunkLabelSolver.hh"

#include <algorithm>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CharCore.hh"
#include "PatternId.hh"

namespace icts::source_trunk {
namespace {

constexpr std::size_t kNoPredecessor = std::numeric_limits<std::size_t>::max();

auto PackBoundary(unsigned slew_idx, unsigned cap_idx) -> unsigned
{
  return (slew_idx << 16U) | cap_idx;
}

struct LabelState
{
  unsigned output_slew_idx = 0U;
  unsigned load_cap_idx = 0U;
  BoundaryBufferState sink_boundary{};

  auto operator==(const LabelState& rhs) const -> bool = default;
};

struct LabelStateHash
{
  auto operator()(const LabelState& state) const -> std::size_t
  {
    std::size_t seed = std::hash<unsigned>{}(state.output_slew_idx);
    seed ^= std::hash<unsigned>{}(state.load_cap_idx) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<bool>{}(state.sink_boundary.has_buffer) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    seed ^= std::hash<unsigned>{}(state.sink_boundary.strength_rank) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

struct Label
{
  LabelState state;
  unsigned initial_input_slew_idx = 0U;
  unsigned initial_driven_cap_idx = 0U;
  double delay = 0.0;
  double power = 0.0;
  double source_boundary_switch_power = 0.0;
  std::size_t predecessor = kNoPredecessor;
  PatternId primitive_pattern_id = PatternId::segment(0U);
};

using StateFrontier = std::unordered_map<LabelState, std::vector<std::size_t>, LabelStateHash>;

auto IsResolved(const BoundaryBufferState& state) -> bool
{
  return !state.has_buffer || state.strength_rank > 0U;
}

auto CanAppend(const BoundaryBufferState& current_sink, const MonotonicBoundaryState& next_state) -> bool
{
  if (!IsResolved(current_sink) || !IsResolved(next_state.source) || !IsResolved(next_state.sink)) {
    return false;
  }
  if (!current_sink.has_buffer || !next_state.source.has_buffer) {
    return true;
  }
  return current_sink.strength_rank >= next_state.source.strength_rank;
}

auto ComposeSinkBoundary(const BoundaryBufferState& current_sink, const MonotonicBoundaryState& next_state) -> BoundaryBufferState
{
  return next_state.sink.has_buffer ? next_state.sink : current_sink;
}

auto CostDominates(const Label& lhs, const Label& rhs) -> bool
{
  const bool not_worse = lhs.delay <= rhs.delay && lhs.power <= rhs.power;
  return not_worse && (lhs.delay < rhs.delay || lhs.power < rhs.power);
}

auto PreferLabel(const Label& lhs, const Label& rhs) -> bool
{
  if (lhs.power != rhs.power) {
    return lhs.power < rhs.power;
  }
  if (lhs.delay != rhs.delay) {
    return lhs.delay < rhs.delay;
  }
  if (lhs.initial_driven_cap_idx != rhs.initial_driven_cap_idx) {
    return lhs.initial_driven_cap_idx < rhs.initial_driven_cap_idx;
  }
  if (lhs.state.output_slew_idx != rhs.state.output_slew_idx) {
    return lhs.state.output_slew_idx < rhs.state.output_slew_idx;
  }
  if (lhs.state.load_cap_idx != rhs.state.load_cap_idx) {
    return lhs.state.load_cap_idx < rhs.state.load_cap_idx;
  }
  if (lhs.initial_input_slew_idx != rhs.initial_input_slew_idx) {
    return lhs.initial_input_slew_idx > rhs.initial_input_slew_idx;
  }
  return lhs.primitive_pattern_id.pack() < rhs.primitive_pattern_id.pack();
}

}  // namespace

auto SolveLabels(const LabelSolverInput& input, const LabelSolverConfig& config) -> LabelSolverBuild
{
  LabelSolverBuild result;
  if (input.primitive_chars == nullptr || input.primitive_patterns == nullptr || input.target_length_idx == 0U || input.required_load_cap_idx == 0U
      || input.source_drive_cap_idx == 0U) {
    result.failure_reason = "source_trunk_label_invalid_input";
    return result;
  }

  std::unordered_map<PatternId, const BufferingPattern*> pattern_by_id;
  pattern_by_id.reserve(input.primitive_patterns->size());
  unsigned next_pattern_local_id = 0U;
  for (const auto& pattern : *input.primitive_patterns) {
    pattern_by_id.emplace(pattern.get_pattern_id(), &pattern);
    next_pattern_local_id = std::max(next_pattern_local_id, pattern.get_pattern_id().local_id + 1U);
  }

  std::unordered_map<unsigned, std::vector<const SegmentChar*>> primitives_by_input;
  for (const auto& primitive : *input.primitive_chars) {
    if (primitive.get_length_idx() == 0U || primitive.get_length_idx() > input.target_length_idx) {
      continue;
    }
    if (!pattern_by_id.contains(primitive.get_pattern_id())) {
      result.status = LabelSolverStatus::kMissingPattern;
      result.failure_reason = "source_trunk_label_missing_primitive_pattern";
      return result;
    }
    primitives_by_input[PackBoundary(primitive.get_input_slew_idx(), primitive.get_driven_cap_idx())].push_back(&primitive);
  }

  std::vector<Label> labels;
  labels.reserve(std::min<std::size_t>(config.max_retained_labels, 100000U));
  std::vector<StateFrontier> frontiers(input.target_length_idx + 1U);
  bool budget_failed = false;

  auto insert_label = [&](unsigned length_idx, Label candidate) -> bool {
    ++result.summary.generated_label_count;
    if (result.summary.generated_label_count > config.max_generated_labels) {
      result.status = LabelSolverStatus::kGeneratedLabelBudgetExceeded;
      result.failure_reason = "source_trunk_label_generated_budget_exceeded";
      budget_failed = true;
      return false;
    }

    auto& frontier = frontiers.at(length_idx)[candidate.state];
    if (std::ranges::any_of(frontier, [&](std::size_t label_id) -> bool {
          const auto& existing = labels.at(label_id);
          return CostDominates(existing, candidate) || (existing.delay == candidate.delay && existing.power == candidate.power);
        })) {
      return true;
    }
    const auto removed = std::ranges::remove_if(frontier, [&](std::size_t label_id) -> bool { return CostDominates(candidate, labels.at(label_id)); });
    frontier.erase(removed.begin(), removed.end());

    if (labels.size() >= config.max_retained_labels) {
      result.status = LabelSolverStatus::kRetainedLabelBudgetExceeded;
      result.failure_reason = "source_trunk_label_retained_budget_exceeded";
      budget_failed = true;
      return false;
    }
    const std::size_t label_id = labels.size();
    labels.push_back(std::move(candidate));
    frontier.push_back(label_id);
    result.summary.retained_label_count = labels.size();
    return true;
  };

  for (const auto& primitive : *input.primitive_chars) {
    if (primitive.get_length_idx() == 0U || primitive.get_length_idx() > input.target_length_idx || primitive.get_driven_cap_idx() > input.source_drive_cap_idx
        || (input.min_input_slew_idx.has_value() && primitive.get_input_slew_idx() < *input.min_input_slew_idx)) {
      continue;
    }
    const auto pattern_it = pattern_by_id.find(primitive.get_pattern_id());
    if (pattern_it == pattern_by_id.end()) {
      continue;
    }
    const auto& pattern_state = pattern_it->second->get_monotonic_boundary_state();
    if (!IsResolved(pattern_state.source) || !IsResolved(pattern_state.sink)) {
      continue;
    }
    if (!insert_label(primitive.get_length_idx(), Label{
                                                       .state = LabelState{
                                                           .output_slew_idx = primitive.get_output_slew_idx(),
                                                           .load_cap_idx = primitive.get_load_cap_idx(),
                                                           .sink_boundary = pattern_state.sink,
                                                       },
                                                       .initial_input_slew_idx = primitive.get_input_slew_idx(),
                                                       .initial_driven_cap_idx = primitive.get_driven_cap_idx(),
                                                       .delay = primitive.get_delay(),
                                                       .power = primitive.get_power(),
                                                       .source_boundary_switch_power = primitive.get_source_boundary_net_switch_power(),
                                                       .predecessor = kNoPredecessor,
                                                       .primitive_pattern_id = primitive.get_pattern_id(),
                                                   })) {
      return result;
    }
  }

  for (unsigned length_idx = 1U; length_idx < input.target_length_idx && !budget_failed; ++length_idx) {
    auto& length_frontier = frontiers.at(length_idx);
    if (length_frontier.empty()) {
      continue;
    }
    ++result.summary.visited_length_count;
    result.summary.visited_state_count += length_frontier.size();

    for (const auto& [state, state_labels] : length_frontier) {
      const auto primitive_it = primitives_by_input.find(PackBoundary(state.output_slew_idx, state.load_cap_idx));
      if (primitive_it == primitives_by_input.end()) {
        continue;
      }
      for (const auto label_id : state_labels) {
        const Label current = labels.at(label_id);
        for (const auto* primitive : primitive_it->second) {
          const unsigned next_length_idx = length_idx + primitive->get_length_idx();
          if (next_length_idx > input.target_length_idx) {
            continue;
          }
          const auto pattern_it = pattern_by_id.find(primitive->get_pattern_id());
          if (pattern_it == pattern_by_id.end()) {
            result.status = LabelSolverStatus::kMissingPattern;
            result.failure_reason = "source_trunk_label_missing_transition_pattern";
            return result;
          }
          const auto& next_pattern_state = pattern_it->second->get_monotonic_boundary_state();
          if (!CanAppend(current.state.sink_boundary, next_pattern_state)) {
            continue;
          }

          Label candidate{
              .state = LabelState{
                  .output_slew_idx = primitive->get_output_slew_idx(),
                  .load_cap_idx = primitive->get_load_cap_idx(),
                  .sink_boundary = ComposeSinkBoundary(current.state.sink_boundary, next_pattern_state),
              },
              .initial_input_slew_idx = current.initial_input_slew_idx,
              .initial_driven_cap_idx = current.initial_driven_cap_idx,
              .delay = current.delay + primitive->get_delay(),
              .power = current.power + primitive->get_power() - primitive->get_source_boundary_net_switch_power(),
              .source_boundary_switch_power = current.source_boundary_switch_power,
              .predecessor = label_id,
              .primitive_pattern_id = primitive->get_pattern_id(),
          };
          if (!insert_label(next_length_idx, std::move(candidate))) {
            return result;
          }
        }
      }
    }
  }

  std::vector<std::size_t> final_pareto;
  for (const auto& [state, state_labels] : frontiers.at(input.target_length_idx)) {
    if (state.load_cap_idx < input.required_load_cap_idx) {
      continue;
    }
    for (const auto label_id : state_labels) {
      ++result.summary.final_candidate_count;
      const auto& candidate = labels.at(label_id);
      if (std::ranges::any_of(final_pareto, [&](std::size_t existing_id) -> bool { return CostDominates(labels.at(existing_id), candidate); })) {
        continue;
      }
      const auto removed
          = std::ranges::remove_if(final_pareto, [&](std::size_t existing_id) -> bool { return CostDominates(candidate, labels.at(existing_id)); });
      final_pareto.erase(removed.begin(), removed.end());
      final_pareto.push_back(label_id);
    }
  }
  result.summary.final_pareto_count = final_pareto.size();
  if (final_pareto.empty()) {
    result.failure_reason = "source_trunk_label_no_legal_path";
    return result;
  }
  std::ranges::sort(final_pareto, [&](std::size_t lhs, std::size_t rhs) -> bool { return PreferLabel(labels.at(lhs), labels.at(rhs)); });
  const std::size_t selected_label_id = final_pareto.at((final_pareto.size() - 1U) / 2U);
  const auto& selected_label = labels.at(selected_label_id);

  std::vector<PatternId> primitive_sequence;
  for (std::size_t label_id = selected_label_id; label_id != kNoPredecessor; label_id = labels.at(label_id).predecessor) {
    primitive_sequence.push_back(labels.at(label_id).primitive_pattern_id);
  }
  std::ranges::reverse(primitive_sequence);
  result.summary.selected_primitive_count = primitive_sequence.size();
  if (primitive_sequence.empty()) {
    result.failure_reason = "source_trunk_label_empty_selected_path";
    return result;
  }

  auto first_pattern_it = pattern_by_id.find(primitive_sequence.front());
  if (first_pattern_it == pattern_by_id.end()) {
    result.status = LabelSolverStatus::kMissingPattern;
    result.failure_reason = "source_trunk_label_missing_reconstruction_pattern";
    return result;
  }
  BufferingPattern combined_pattern = *first_pattern_it->second;
  for (std::size_t sequence_index = 1U; sequence_index < primitive_sequence.size(); ++sequence_index) {
    const auto pattern_it = pattern_by_id.find(primitive_sequence.at(sequence_index));
    if (pattern_it == pattern_by_id.end()) {
      result.status = LabelSolverStatus::kMissingPattern;
      result.failure_reason = "source_trunk_label_missing_reconstruction_pattern";
      return result;
    }
    combined_pattern = BufferingPattern::concat(combined_pattern, *pattern_it->second);
  }

  const PatternId selected_pattern_id = PatternId::segment(next_pattern_local_id);
  result.best_pattern
      = BufferingPattern(input.target_length_idx, selected_pattern_id, combined_pattern.get_buffer_positions(), combined_pattern.get_cell_masters(),
                         combined_pattern.hasTerminalBranchBuffer(), combined_pattern.get_monotonic_boundary_state());
  result.best_char = SegmentChar(
      CharCore(selected_label.initial_input_slew_idx, selected_label.state.output_slew_idx, selected_label.initial_driven_cap_idx,
               selected_label.state.load_cap_idx, selected_label.delay, selected_label.power, selected_pattern_id, selected_label.source_boundary_switch_power),
      input.target_length_idx);
  result.status = LabelSolverStatus::kFinished;
  result.failure_reason.clear();
  return result;
}

}  // namespace icts::source_trunk
