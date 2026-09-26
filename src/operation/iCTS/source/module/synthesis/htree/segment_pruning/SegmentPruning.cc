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
 * @file SegmentPruning.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Segment frontier synthesis for required H-tree level lengths.
 */

#include "synthesis/htree/segment_pruning/SegmentPruning.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "LogTable.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"

namespace icts::htree {
namespace {

auto resolveSegmentCompositionState(const BufferPatternLibrary& pattern_library, PatternId pattern_id) -> PatternCompositionState
{
  return pattern_library.getCompositionState(pattern_id);
}

auto BuildSegmentStateFrontier(const std::vector<SegmentChar>& chars, const BufferPatternLibrary& pattern_library) -> std::vector<SegmentChar>
{
  return icts::BuildSegmentStateFrontier(
      chars, [&](const SegmentChar& entry) -> PatternCompositionState { return resolveSegmentCompositionState(pattern_library, entry.get_pattern_id()); });
}

auto FindNextSegmentPatternId(const std::vector<SegmentChar>& chars, const BufferPatternLibrary& pattern_library) -> unsigned
{
  unsigned next_id = 0U;
  for (const auto& entry : chars) {
    next_id = std::max(next_id, entry.get_pattern_id().local_id + 1U);
  }
  for (const auto& [pattern_id, pattern] : pattern_library.patterns) {
    static_cast<void>(pattern);
    next_id = std::max(next_id, pattern_id.local_id + 1U);
  }
  return next_id;
}

auto hasTerminalBranchBufferPattern(const BufferPatternLibrary& pattern_library, PatternId pattern_id) -> bool
{
  const auto* pattern = pattern_library.find(pattern_id);
  return pattern != nullptr && pattern->hasTerminalBranchBuffer();
}

auto CountSegmentCandidateFrontierEntries(const SegmentCandidateFrontierSet& entry_set) -> std::size_t
{
  return entry_set.countEntries(SegmentFrontierKindSet::full());
}

auto CountTotalSegmentCandidateFrontierEntries(const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets) -> std::size_t
{
  std::size_t total_entries = 0U;
  for (const auto& frontier_entry : entry_sets) {
    total_entries += CountSegmentCandidateFrontierEntries(frontier_entry.second);
  }
  return total_entries;
}

auto AppendRetainedSegmentPatternIds(const SegmentCandidateFrontierSet& entry_set, SegmentFrontierKindSet required_kinds, std::vector<PatternId>& pattern_ids)
    -> void
{
  static constexpr std::array<SegmentFrontierKind, 3> frontier_kinds
      = {SegmentFrontierKind::kAll, SegmentFrontierKind::kTerminalBranchBuffered, SegmentFrontierKind::kTerminalLeafUnbuffered};
  for (const auto kind : frontier_kinds) {
    if (!required_kinds.contains(kind)) {
      continue;
    }
    const auto* entries = entry_set.find(kind);
    if (entries == nullptr) {
      continue;
    }
    pattern_ids.reserve(pattern_ids.size() + entries->size());
    for (const auto& entry : *entries) {
      pattern_ids.push_back(entry.get_pattern_id());
    }
  }
}

auto RetainSegmentPatternsForEntrySets(const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets, SegmentFrontierKindSet required_kinds,
                                       BufferPatternLibrary& pattern_library) -> void
{
  std::vector<PatternId> retained_pattern_ids;
  for (const auto& frontier_entry : entry_sets) {
    AppendRetainedSegmentPatternIds(frontier_entry.second, required_kinds, retained_pattern_ids);
  }
  pattern_library.retainOnly(retained_pattern_ids);
}

auto FindSegmentCandidateFrontierSet(const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets, unsigned length_idx)
    -> const SegmentCandidateFrontierSet*
{
  const auto it = entry_sets.find(length_idx);
  return it == entry_sets.end() ? nullptr : &it->second;
}

struct StagedSegmentCandidate
{
  const SegmentChar* upstream = nullptr;
  const SegmentChar* downstream = nullptr;
  SegmentChar composed;
  PatternCompositionState composition_state;
  std::size_t ordinal = 0U;
};

auto BuildStagedSegmentStateKey(const StagedSegmentCandidate& candidate) -> SegmentFrontierStateKey
{
  return SegmentFrontierStateKey{
      .input_slew_idx = candidate.composed.get_input_slew_idx(),
      .driven_cap_idx = candidate.composed.get_driven_cap_idx(),
      .output_slew_idx = candidate.composed.get_output_slew_idx(),
      .load_cap_idx = candidate.composed.get_load_cap_idx(),
      .source_boundary_net_switch_power_w = candidate.composed.get_source_boundary_net_switch_power(),
      .terminal_semantic = candidate.composition_state.terminal_semantic,
      .monotonic_boundary_state = candidate.composition_state.monotonic_boundary_state,
      .source_exposed_load_count = candidate.composition_state.source_exposed_load_count,
      .geometry_state = candidate.composition_state.geometry_state,
  };
}

auto StagedSegmentCandidateDominates(const StagedSegmentCandidate& lhs, const StagedSegmentCandidate& rhs) -> bool
{
  const bool not_worse = lhs.composed.get_delay() <= rhs.composed.get_delay() && lhs.composed.get_power() <= rhs.composed.get_power();
  if (!not_worse) {
    return false;
  }
  if (lhs.composed.get_delay() < rhs.composed.get_delay() || lhs.composed.get_power() < rhs.composed.get_power()) {
    return true;
  }
  return lhs.ordinal < rhs.ordinal;
}

struct SegmentFrontierJoinInput
{
  const std::vector<SegmentChar>* upstream = nullptr;
  const std::vector<SegmentChar>* downstream = nullptr;
};

struct SegmentFrontierCompositionBuild
{
  std::vector<SegmentChar> entries;
  unsigned next_pattern_id = 0U;
  std::size_t join_attempt_count = 0U;
  bool join_budget_exceeded = false;
};

auto ComposeSegmentCandidateFrontierEntriesAcrossInputs(const std::vector<SegmentFrontierJoinInput>& join_inputs, BufferPatternLibrary& pattern_library,
                                                        unsigned start_pattern_id, std::size_t maximum_join_attempts) -> SegmentFrontierCompositionBuild
{
  SegmentPatternLibraryCombiner combiner(pattern_library, start_pattern_id);
  std::unordered_map<SegmentFrontierStateKey, std::size_t, SegmentFrontierStateKeyHash> group_to_index;
  std::vector<std::vector<StagedSegmentCandidate>> staged_groups;
  std::size_t ordinal = 0U;
  std::size_t join_attempt_count = 0U;

  for (const auto& join_input : join_inputs) {
    if (join_input.upstream == nullptr || join_input.downstream == nullptr || join_input.upstream->empty() || join_input.downstream->empty()) {
      continue;
    }
    const auto& upstream = *join_input.upstream;
    const auto& downstream = *join_input.downstream;
    std::unordered_map<unsigned, std::vector<std::size_t>> downstream_by_key;
    downstream_by_key.reserve(downstream.size());
    for (std::size_t index = 0U; index < downstream.size(); ++index) {
      downstream_by_key[SegmentTraits::buildKey(downstream[index])].push_back(index);
    }

    for (const auto& upstream_entry : upstream) {
      const auto downstream_iter = downstream_by_key.find(SegmentTraits::probeKey(upstream_entry));
      if (downstream_iter == downstream_by_key.end()) {
        continue;
      }
      for (const auto downstream_index : downstream_iter->second) {
        if (join_attempt_count >= maximum_join_attempts) {
          return SegmentFrontierCompositionBuild{
              .entries = {},
              .next_pattern_id = combiner.get_next_id(),
              .join_attempt_count = join_attempt_count,
              .join_budget_exceeded = true,
          };
        }
        ++join_attempt_count;
        const auto& downstream_entry = downstream[downstream_index];
        if (!combiner.canCompose(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id())) {
          continue;
        }

        StagedSegmentCandidate candidate{
            .upstream = &upstream_entry,
            .downstream = &downstream_entry,
            .composed = SegmentChar::compose(upstream_entry, downstream_entry, PatternId::segment(0U)),
            .composition_state = combiner.composeState(upstream_entry.get_pattern_id(), downstream_entry.get_pattern_id()),
            .ordinal = ordinal++,
        };
        const auto group_key = BuildStagedSegmentStateKey(candidate);
        auto [group_iter, inserted] = group_to_index.emplace(group_key, staged_groups.size());
        if (inserted) {
          staged_groups.emplace_back();
        }
        auto& frontier = staged_groups[group_iter->second];
        if (std::ranges::any_of(frontier,
                                [&](const StagedSegmentCandidate& existing) -> bool { return StagedSegmentCandidateDominates(existing, candidate); })) {
          continue;
        }
        const auto removed = std::ranges::remove_if(
            frontier, [&](const StagedSegmentCandidate& existing) -> bool { return StagedSegmentCandidateDominates(candidate, existing); });
        frontier.erase(removed.begin(), removed.end());
        frontier.push_back(std::move(candidate));
      }
    }
  }

  std::vector<SegmentChar> frontier_entries;
  std::size_t survivor_count = 0U;
  for (const auto& group : staged_groups) {
    survivor_count += group.size();
  }
  frontier_entries.reserve(survivor_count);
  for (const auto& group : staged_groups) {
    for (const auto& survivor : group) {
      const auto merged_pattern_id = combiner.combine(survivor.upstream->get_pattern_id(), survivor.downstream->get_pattern_id());
      frontier_entries.push_back(SegmentChar::compose(*survivor.upstream, *survivor.downstream, merged_pattern_id));
    }
  }
  SortSegmentFrontierEntries(frontier_entries);
  return SegmentFrontierCompositionBuild{
      .entries = std::move(frontier_entries),
      .next_pattern_id = combiner.get_next_id(),
      .join_attempt_count = join_attempt_count,
      .join_budget_exceeded = false,
  };
}

auto ComposeSegmentCandidateFrontierEntries(const std::vector<SegmentChar>& upstream, const std::vector<SegmentChar>& downstream,
                                            BufferPatternLibrary& pattern_library, unsigned start_pattern_id) -> std::pair<std::vector<SegmentChar>, unsigned>
{
  auto build = ComposeSegmentCandidateFrontierEntriesAcrossInputs(
      std::vector<SegmentFrontierJoinInput>{SegmentFrontierJoinInput{.upstream = &upstream, .downstream = &downstream}}, pattern_library, start_pattern_id,
      std::numeric_limits<std::size_t>::max());
  return {std::move(build.entries), build.next_pattern_id};
}

auto ComposeSegmentCandidateFrontierEntriesBidirectional(const std::vector<SegmentChar>& lhs, const std::vector<SegmentChar>& rhs,
                                                         BufferPatternLibrary& pattern_library, unsigned start_pattern_id)
    -> std::pair<std::vector<SegmentChar>, unsigned>
{
  std::vector<SegmentFrontierJoinInput> join_inputs{
      SegmentFrontierJoinInput{.upstream = &lhs, .downstream = &rhs},
  };
  if (&lhs != &rhs) {
    join_inputs.push_back(SegmentFrontierJoinInput{.upstream = &rhs, .downstream = &lhs});
  }
  auto build = ComposeSegmentCandidateFrontierEntriesAcrossInputs(join_inputs, pattern_library, start_pattern_id, std::numeric_limits<std::size_t>::max());
  return {std::move(build.entries), build.next_pattern_id};
}

auto ComposeSegmentCandidateFrontierSet(const SegmentCandidateFrontierSet& upstream, const SegmentCandidateFrontierSet& downstream,
                                        BufferPatternLibrary& pattern_library, unsigned start_pattern_id, SegmentFrontierKindSet required_kinds)
    -> std::pair<SegmentCandidateFrontierSet, unsigned>
{
  SegmentCandidateFrontierSet result;
  unsigned next_pattern_id = start_pattern_id;

  if (required_kinds.contains(SegmentFrontierKind::kAll)) {
    auto [all_frontier_entries, after_all_pattern_id] = ComposeSegmentCandidateFrontierEntries(
        upstream.require(SegmentFrontierKind::kAll), downstream.require(SegmentFrontierKind::kAll), pattern_library, next_pattern_id);
    result.mutableEntries(SegmentFrontierKind::kAll) = std::move(all_frontier_entries);
    next_pattern_id = after_all_pattern_id;
  }

  if (required_kinds.contains(SegmentFrontierKind::kTerminalBranchBuffered)) {
    auto [branch_frontier_entries, after_branch_pattern_id] = ComposeSegmentCandidateFrontierEntries(
        upstream.require(SegmentFrontierKind::kAll), downstream.require(SegmentFrontierKind::kTerminalBranchBuffered), pattern_library, next_pattern_id);
    result.mutableEntries(SegmentFrontierKind::kTerminalBranchBuffered) = std::move(branch_frontier_entries);
    next_pattern_id = after_branch_pattern_id;
  }

  if (required_kinds.contains(SegmentFrontierKind::kTerminalLeafUnbuffered)) {
    auto [leaf_frontier_entries, after_leaf_pattern_id] = ComposeSegmentCandidateFrontierEntries(
        upstream.require(SegmentFrontierKind::kAll), downstream.require(SegmentFrontierKind::kTerminalLeafUnbuffered), pattern_library, next_pattern_id);
    result.mutableEntries(SegmentFrontierKind::kTerminalLeafUnbuffered) = std::move(leaf_frontier_entries);
    next_pattern_id = after_leaf_pattern_id;
  }

  return {std::move(result), next_pattern_id};
}

auto ComposeSegmentCandidateFrontierSetBidirectional(const SegmentCandidateFrontierSet& lhs, const SegmentCandidateFrontierSet& rhs,
                                                     BufferPatternLibrary& pattern_library, unsigned start_pattern_id, SegmentFrontierKindSet required_kinds)
    -> std::pair<SegmentCandidateFrontierSet, unsigned>
{
  SegmentCandidateFrontierSet result;
  unsigned next_pattern_id = start_pattern_id;

  if (required_kinds.contains(SegmentFrontierKind::kAll)) {
    auto [entries, after_pattern_id] = ComposeSegmentCandidateFrontierEntriesBidirectional(
        lhs.require(SegmentFrontierKind::kAll), rhs.require(SegmentFrontierKind::kAll), pattern_library, next_pattern_id);
    result.mutableEntries(SegmentFrontierKind::kAll) = std::move(entries);
    next_pattern_id = after_pattern_id;
  }
  if (required_kinds.contains(SegmentFrontierKind::kTerminalBranchBuffered)) {
    std::vector<SegmentFrontierJoinInput> join_inputs{
        SegmentFrontierJoinInput{.upstream = &lhs.require(SegmentFrontierKind::kAll), .downstream = &rhs.require(SegmentFrontierKind::kTerminalBranchBuffered)},
        SegmentFrontierJoinInput{.upstream = &rhs.require(SegmentFrontierKind::kAll), .downstream = &lhs.require(SegmentFrontierKind::kTerminalBranchBuffered)},
    };
    auto build = ComposeSegmentCandidateFrontierEntriesAcrossInputs(join_inputs, pattern_library, next_pattern_id, std::numeric_limits<std::size_t>::max());
    result.mutableEntries(SegmentFrontierKind::kTerminalBranchBuffered) = std::move(build.entries);
    next_pattern_id = build.next_pattern_id;
  }
  if (required_kinds.contains(SegmentFrontierKind::kTerminalLeafUnbuffered)) {
    std::vector<SegmentFrontierJoinInput> join_inputs{
        SegmentFrontierJoinInput{.upstream = &lhs.require(SegmentFrontierKind::kAll), .downstream = &rhs.require(SegmentFrontierKind::kTerminalLeafUnbuffered)},
        SegmentFrontierJoinInput{.upstream = &rhs.require(SegmentFrontierKind::kAll), .downstream = &lhs.require(SegmentFrontierKind::kTerminalLeafUnbuffered)},
    };
    auto build = ComposeSegmentCandidateFrontierEntriesAcrossInputs(join_inputs, pattern_library, next_pattern_id, std::numeric_limits<std::size_t>::max());
    result.mutableEntries(SegmentFrontierKind::kTerminalLeafUnbuffered) = std::move(build.entries);
    next_pattern_id = build.next_pattern_id;
  }
  return {std::move(result), next_pattern_id};
}

auto BuildBaseSegmentCandidateLengthEntrySets(const std::vector<SegmentChar>& chars, const BufferPatternLibrary& pattern_library,
                                              SegmentFrontierKindSet required_kinds) -> std::unordered_map<unsigned, SegmentCandidateFrontierSet>
{
  std::unordered_map<unsigned, std::vector<SegmentChar>> raw_all_by_length;
  std::unordered_map<unsigned, std::vector<SegmentChar>> raw_leaf_unbuffered_by_length;
  std::unordered_map<unsigned, std::vector<SegmentChar>> raw_branch_by_length;
  const bool build_all = required_kinds.contains(SegmentFrontierKind::kAll);
  const bool build_branch = required_kinds.contains(SegmentFrontierKind::kTerminalBranchBuffered);
  const bool build_leaf = required_kinds.contains(SegmentFrontierKind::kTerminalLeafUnbuffered);
  raw_all_by_length.reserve(chars.size());
  if (build_leaf) {
    raw_leaf_unbuffered_by_length.reserve(chars.size());
  }
  if (build_branch) {
    raw_branch_by_length.reserve(chars.size());
  }
  for (const auto& entry : chars) {
    if (build_all) {
      raw_all_by_length[entry.get_length_idx()].push_back(entry);
    }
    if (!build_branch && !build_leaf) {
      continue;
    }
    if (hasTerminalBranchBufferPattern(pattern_library, entry.get_pattern_id())) {
      if (build_branch) {
        raw_branch_by_length[entry.get_length_idx()].push_back(entry);
      }
    } else if (build_leaf) {
      raw_leaf_unbuffered_by_length[entry.get_length_idx()].push_back(entry);
    }
  }

  std::unordered_map<unsigned, SegmentCandidateFrontierSet> entry_sets_by_length;
  entry_sets_by_length.reserve(raw_all_by_length.size());
  for (auto& [length_idx, raw_entries] : raw_all_by_length) {
    auto& entry_set = entry_sets_by_length[length_idx];
    entry_set.mutableEntries(SegmentFrontierKind::kAll) = BuildSegmentStateFrontier(raw_entries, pattern_library);
  }

  for (auto& frontier_entry : entry_sets_by_length) {
    auto& entry_set = frontier_entry.second;
    if (build_branch) {
      entry_set.mutableEntries(SegmentFrontierKind::kTerminalBranchBuffered);
    }
    if (build_leaf) {
      entry_set.mutableEntries(SegmentFrontierKind::kTerminalLeafUnbuffered);
    }
  }

  if (build_branch) {
    for (auto& [length_idx, raw_entries] : raw_branch_by_length) {
      auto& entry_set = entry_sets_by_length[length_idx];
      entry_set.mutableEntries(SegmentFrontierKind::kTerminalBranchBuffered) = BuildSegmentStateFrontier(raw_entries, pattern_library);
    }
  }
  if (build_leaf) {
    for (auto& [length_idx, raw_entries] : raw_leaf_unbuffered_by_length) {
      auto& entry_set = entry_sets_by_length[length_idx];
      entry_set.mutableEntries(SegmentFrontierKind::kTerminalLeafUnbuffered) = BuildSegmentStateFrontier(raw_entries, pattern_library);
    }
  }
  return entry_sets_by_length;
}

auto NormalizeRequiredLengths(std::vector<unsigned> lengths) -> std::vector<unsigned>
{
  std::erase(lengths, 0U);
  std::ranges::sort(lengths);
  const auto unique_tail = std::ranges::unique(lengths);
  lengths.erase(unique_tail.begin(), unique_tail.end());
  return lengths;
}

auto EntrySetSatisfiesRequiredKinds(const SegmentCandidateFrontierSet* entry_set, SegmentFrontierKindSet required_kinds) -> bool
{
  static constexpr std::array<SegmentFrontierKind, 3> frontier_kinds
      = {SegmentFrontierKind::kAll, SegmentFrontierKind::kTerminalBranchBuffered, SegmentFrontierKind::kTerminalLeafUnbuffered};
  if (entry_set == nullptr) {
    return false;
  }
  for (const auto kind : frontier_kinds) {
    if (!required_kinds.contains(kind)) {
      continue;
    }
    const auto* entries = entry_set->find(kind);
    if (entries == nullptr || entries->empty()) {
      return false;
    }
  }
  return true;
}

auto BuildPendingLengthKey(const std::vector<unsigned>& pending_lengths, const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& base_entry_sets,
                           SegmentFrontierKindSet required_kinds) -> RequiredLengthStateKey
{
  std::vector<unsigned> canonical_lengths;
  canonical_lengths.reserve(pending_lengths.size());
  for (const unsigned length_idx : pending_lengths) {
    const auto* base_entry_set = FindSegmentCandidateFrontierSet(base_entry_sets, length_idx);
    if (EntrySetSatisfiesRequiredKinds(base_entry_set, required_kinds)) {
      continue;
    }
    canonical_lengths.push_back(length_idx);
  }

  return RequiredLengthStateKey{.pending_lengths = NormalizeRequiredLengths(std::move(canonical_lengths))};
}

auto ResolveSegmentCandidateFrontierSet(unsigned length_idx, const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& base_entry_sets,
                                        const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& synthesized_entry_sets)
    -> const SegmentCandidateFrontierSet*
{
  if (const auto* synthesized_entry_set = FindSegmentCandidateFrontierSet(synthesized_entry_sets, length_idx); synthesized_entry_set != nullptr) {
    return synthesized_entry_set;
  }
  return FindSegmentCandidateFrontierSet(base_entry_sets, length_idx);
}

auto PreferSegmentClosureSolution(const SegmentClosureSolution& lhs, const SegmentClosureSolution& rhs) -> bool
{
  if (!lhs.feasible) {
    return false;
  }
  if (!rhs.feasible) {
    return true;
  }
  if (lhs.total_cost != rhs.total_cost) {
    return lhs.total_cost < rhs.total_cost;
  }

  const std::size_t lhs_frontier_entries = CountTotalSegmentCandidateFrontierEntries(lhs.synthesized_entry_sets);
  const std::size_t rhs_frontier_entries = CountTotalSegmentCandidateFrontierEntries(rhs.synthesized_entry_sets);
  if (lhs_frontier_entries != rhs_frontier_entries) {
    return lhs_frontier_entries > rhs_frontier_entries;
  }

  return lhs.synthesized_entry_sets.size() < rhs.synthesized_entry_sets.size();
}

auto SolveRequiredLengthState(const RequiredLengthStateKey& state_key, const std::unordered_map<unsigned, SegmentCandidateFrontierSet>& base_entry_sets,
                              BufferPatternLibrary& pattern_library, unsigned& next_pattern_id,
                              std::unordered_map<RequiredLengthStateKey, SegmentClosureSolution, RequiredLengthStateKeyHash>& memo,
                              SegmentFrontierKindSet required_kinds) -> SegmentClosureSolution
{
  std::vector<RequiredLengthStateKey> pending_states = {state_key};
  while (!pending_states.empty()) {
    const auto current_key = pending_states.back();
    if (memo.contains(current_key)) {
      pending_states.pop_back();
      continue;
    }
    if (current_key.pending_lengths.empty()) {
      memo[current_key] = SegmentClosureSolution{
          .feasible = true,
          .total_cost = 0U,
          .synthesized_entry_sets = {},
      };
      pending_states.pop_back();
      continue;
    }

    const unsigned target_length_idx = current_key.pending_lengths.back();
    std::vector<unsigned> remaining_lengths = current_key.pending_lengths;
    remaining_lengths.pop_back();

    std::vector<RequiredLengthStateKey> unresolved_sub_states;
    for (unsigned left_length_idx = 1U; left_length_idx < target_length_idx; ++left_length_idx) {
      const unsigned right_length_idx = target_length_idx - left_length_idx;
      auto sub_pending_lengths = remaining_lengths;
      sub_pending_lengths.push_back(left_length_idx);
      sub_pending_lengths.push_back(right_length_idx);
      const RequiredLengthStateKey sub_state_key = BuildPendingLengthKey(sub_pending_lengths, base_entry_sets, required_kinds);
      if (!memo.contains(sub_state_key)) {
        unresolved_sub_states.push_back(sub_state_key);
      }
    }
    if (!unresolved_sub_states.empty()) {
      for (const auto& sub_state_key : std::views::reverse(unresolved_sub_states)) {
        pending_states.push_back(sub_state_key);
      }
      continue;
    }

    SegmentClosureSolution best_solution;
    for (unsigned left_length_idx = 1U; left_length_idx < target_length_idx; ++left_length_idx) {
      const unsigned right_length_idx = target_length_idx - left_length_idx;

      auto sub_pending_lengths = remaining_lengths;
      sub_pending_lengths.push_back(left_length_idx);
      sub_pending_lengths.push_back(right_length_idx);
      const RequiredLengthStateKey sub_state_key = BuildPendingLengthKey(sub_pending_lengths, base_entry_sets, required_kinds);
      const auto& sub_solution = memo.at(sub_state_key);
      if (!sub_solution.feasible) {
        continue;
      }

      const auto* left_entry_set = ResolveSegmentCandidateFrontierSet(left_length_idx, base_entry_sets, sub_solution.synthesized_entry_sets);
      const auto* right_entry_set = ResolveSegmentCandidateFrontierSet(right_length_idx, base_entry_sets, sub_solution.synthesized_entry_sets);
      if (left_entry_set == nullptr || right_entry_set == nullptr) {
        continue;
      }
      const auto* left_all_frontier = left_entry_set->find(SegmentFrontierKind::kAll);
      const auto* right_all_frontier = right_entry_set->find(SegmentFrontierKind::kAll);
      if (left_all_frontier == nullptr || right_all_frontier == nullptr || left_all_frontier->empty() || right_all_frontier->empty()) {
        continue;
      }

      auto [composed_entry_set, updated_next_pattern_id]
          = ComposeSegmentCandidateFrontierSet(*left_entry_set, *right_entry_set, pattern_library, next_pattern_id, required_kinds);
      next_pattern_id = updated_next_pattern_id;
      const auto* composed_all_frontier = composed_entry_set.find(SegmentFrontierKind::kAll);
      if (composed_all_frontier == nullptr || composed_all_frontier->empty()) {
        continue;
      }

      auto candidate_solution = sub_solution;
      candidate_solution.feasible = true;
      candidate_solution.total_cost += target_length_idx;
      candidate_solution.synthesized_entry_sets[target_length_idx] = std::move(composed_entry_set);
      if (PreferSegmentClosureSolution(candidate_solution, best_solution)) {
        best_solution = std::move(candidate_solution);
      }
    }

    memo[current_key] = std::move(best_solution);
    pending_states.pop_back();
  }

  return memo.at(state_key);
}

auto CollectCanonicalBoundaryPrimitiveLengths(const std::vector<SegmentChar>& chars, const BufferPatternLibrary& pattern_library) -> std::vector<unsigned>
{
  std::vector<unsigned> lengths;
  lengths.reserve(chars.size());
  for (const auto& entry : chars) {
    const unsigned length_idx = entry.get_length_idx();
    if (length_idx == 0U || (length_idx & (length_idx - 1U)) != 0U) {
      return {};
    }
    const auto* pattern = pattern_library.find(entry.get_pattern_id());
    if (pattern == nullptr) {
      return {};
    }
    const auto& positions = pattern->get_buffer_positions();
    const auto& masters = pattern->get_cell_masters();
    const bool is_wire = positions.empty() && masters.empty();
    const bool is_terminal_buffer = positions.size() == 1U && masters.size() == 1U && positions.front() == 1.0 && pattern->hasTerminalBranchBuffer();
    if (!is_wire && !is_terminal_buffer) {
      return {};
    }
    lengths.push_back(length_idx);
  }
  std::ranges::sort(lengths);
  const auto unique_tail = std::ranges::unique(lengths);
  lengths.erase(unique_tail.begin(), unique_tail.end());
  if (lengths.empty() || lengths.front() != 1U) {
    return {};
  }
  return lengths;
}

auto EntrySetHasEntries(const SegmentCandidateFrontierSet* entry_set, SegmentFrontierKind kind) -> bool
{
  if (entry_set == nullptr) {
    return false;
  }
  const auto* entries = entry_set->find(kind);
  return entries != nullptr && !entries->empty();
}

auto MarkFrontierKindsBuilt(SegmentCandidateFrontierSet& entry_set, SegmentFrontierKindSet kinds) -> void
{
  static constexpr std::array<SegmentFrontierKind, 3> frontier_kinds
      = {SegmentFrontierKind::kAll, SegmentFrontierKind::kTerminalBranchBuffered, SegmentFrontierKind::kTerminalLeafUnbuffered};
  for (const auto kind : frontier_kinds) {
    if (kinds.contains(kind) && !entry_set.hasKind(kind)) {
      entry_set.mutableEntries(kind);
    }
  }
}

auto MergeSegmentCandidateFrontierSet(SegmentCandidateFrontierSet& destination, const SegmentCandidateFrontierSet& source, SegmentFrontierKindSet merged_kinds,
                                      const BufferPatternLibrary& pattern_library) -> void
{
  static constexpr std::array<SegmentFrontierKind, 3> frontier_kinds
      = {SegmentFrontierKind::kAll, SegmentFrontierKind::kTerminalBranchBuffered, SegmentFrontierKind::kTerminalLeafUnbuffered};
  for (const auto kind : frontier_kinds) {
    if (!merged_kinds.contains(kind)) {
      continue;
    }
    const auto* source_entries = source.find(kind);
    if (source_entries == nullptr || source_entries->empty()) {
      continue;
    }
    std::vector<SegmentChar> merged_entries;
    if (const auto* destination_entries = destination.find(kind); destination_entries != nullptr) {
      merged_entries = *destination_entries;
    }
    merged_entries.insert(merged_entries.end(), source_entries->begin(), source_entries->end());
    destination.mutableEntries(kind) = BuildSegmentStateFrontier(merged_entries, pattern_library);
  }
}

auto SynthesizeCanonicalBoundaryPrimitiveFrontiers(std::unordered_map<unsigned, SegmentCandidateFrontierSet>& entry_sets_by_length,
                                                   const std::vector<unsigned>& required_length_indices, const std::vector<unsigned>& primitive_lengths,
                                                   BufferPatternLibrary& pattern_library, unsigned& next_pattern_id, SegmentFrontierKindSet required_kinds)
    -> bool
{
  const auto normalized_required_lengths = NormalizeRequiredLengths(required_length_indices);
  if (normalized_required_lengths.empty()) {
    return true;
  }

  std::vector<unsigned> dependency_lengths = normalized_required_lengths;
  for (std::size_t dependency_index = 0U; dependency_index < dependency_lengths.size(); ++dependency_index) {
    const unsigned target_length_idx = dependency_lengths.at(dependency_index);
    if (target_length_idx <= 1U) {
      continue;
    }
    auto left_length_iter = std::ranges::lower_bound(primitive_lengths, target_length_idx);
    if (left_length_iter == primitive_lengths.begin()) {
      return false;
    }
    --left_length_iter;
    const unsigned left_length_idx = *left_length_iter;
    const unsigned right_length_idx = target_length_idx - left_length_idx;
    if (std::ranges::find(dependency_lengths, left_length_idx) == dependency_lengths.end()) {
      dependency_lengths.push_back(left_length_idx);
    }
    if (std::ranges::find(dependency_lengths, right_length_idx) == dependency_lengths.end()) {
      dependency_lengths.push_back(right_length_idx);
    }
  }
  std::ranges::sort(dependency_lengths);

  const auto log_location = Loc::current();
  for (const unsigned target_length_idx : dependency_lengths) {
    if (EntrySetSatisfiesRequiredKinds(FindSegmentCandidateFrontierSet(entry_sets_by_length, target_length_idx), required_kinds) || target_length_idx <= 1U) {
      continue;
    }
    auto left_length_iter = std::ranges::lower_bound(primitive_lengths, target_length_idx);
    if (left_length_iter == primitive_lengths.begin()) {
      continue;
    }
    --left_length_iter;
    const unsigned left_length_idx = *left_length_iter;
    const unsigned right_length_idx = target_length_idx - left_length_idx;
    const auto* left_entry_set = FindSegmentCandidateFrontierSet(entry_sets_by_length, left_length_idx);
    const auto* right_entry_set = FindSegmentCandidateFrontierSet(entry_sets_by_length, right_length_idx);
    if (!EntrySetHasEntries(left_entry_set, SegmentFrontierKind::kAll) || !EntrySetHasEntries(right_entry_set, SegmentFrontierKind::kAll)) {
      continue;
    }
    const auto step_start = std::chrono::steady_clock::now();
    CTSLOG.info(log_location, "HTree: compose canonical boundary-primitive length ", target_length_idx, " from ", left_length_idx, "+", right_length_idx,
                " (left entries=", CountSegmentCandidateFrontierEntries(*left_entry_set),
                ", right entries=", CountSegmentCandidateFrontierEntries(*right_entry_set), ").");
    auto [composed_entry_set, updated_next_pattern_id]
        = ComposeSegmentCandidateFrontierSetBidirectional(*left_entry_set, *right_entry_set, pattern_library, next_pattern_id, required_kinds);
    const double step_wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - step_start).count();
    next_pattern_id = updated_next_pattern_id;
    CTSLOG.info(log_location, "HTree: completed canonical boundary-primitive length ", target_length_idx,
                " (entries=", CountSegmentCandidateFrontierEntries(composed_entry_set), ", wall_s=", step_wall_s, ").");
    auto& target_entry_set = entry_sets_by_length[target_length_idx];
    MergeSegmentCandidateFrontierSet(target_entry_set, composed_entry_set, required_kinds, pattern_library);
    MarkFrontierKindsBuilt(target_entry_set, required_kinds);
  }

  for (const unsigned required_length_idx : normalized_required_lengths) {
    if (!EntrySetSatisfiesRequiredKinds(FindSegmentCandidateFrontierSet(entry_sets_by_length, required_length_idx), required_kinds)) {
      return false;
    }
  }

  std::erase_if(entry_sets_by_length,
                [&](const auto& entry) -> bool { return entry.first != 1U && !std::ranges::binary_search(normalized_required_lengths, entry.first); });
  RetainSegmentPatternsForEntrySets(entry_sets_by_length, required_kinds, pattern_library);
  return true;
}

auto SynthesizeSegmentFrontierSets(const std::vector<SegmentChar>& base_segment_chars, BufferPatternLibrary& pattern_library,
                                   const RequiredSegmentFrontiers& required_frontiers, bool use_canonical_boundary_primitive_basis)
    -> std::unordered_map<unsigned, SegmentCandidateFrontierSet>
{
  const SegmentFrontierKindSet required_kinds = required_frontiers.required_kinds.normalized();
  if (required_kinds.empty()) {
    return {};
  }

  std::vector<unsigned> boundary_primitive_lengths;
  if (use_canonical_boundary_primitive_basis) {
    boundary_primitive_lengths = CollectCanonicalBoundaryPrimitiveLengths(base_segment_chars, pattern_library);
    if (boundary_primitive_lengths.empty()) {
      return {};
    }
  }

  auto entry_sets_by_length = BuildBaseSegmentCandidateLengthEntrySets(base_segment_chars, pattern_library, required_kinds);
  const RequiredLengthStateKey root_state_key = BuildPendingLengthKey(required_frontiers.required_length_indices, entry_sets_by_length, required_kinds);
  if (root_state_key.pending_lengths.empty()) {
    RetainSegmentPatternsForEntrySets(entry_sets_by_length, required_kinds, pattern_library);
    return entry_sets_by_length;
  }

  unsigned next_pattern_id = FindNextSegmentPatternId(base_segment_chars, pattern_library);
  if (use_canonical_boundary_primitive_basis) {
    if (!SynthesizeCanonicalBoundaryPrimitiveFrontiers(entry_sets_by_length, required_frontiers.required_length_indices, boundary_primitive_lengths,
                                                       pattern_library, next_pattern_id, required_kinds)) {
      return {};
    }
    RetainSegmentPatternsForEntrySets(entry_sets_by_length, required_kinds, pattern_library);
    return entry_sets_by_length;
  }
  std::unordered_map<RequiredLengthStateKey, SegmentClosureSolution, RequiredLengthStateKeyHash> memo;
  auto closure_solution = SolveRequiredLengthState(root_state_key, entry_sets_by_length, pattern_library, next_pattern_id, memo, required_kinds);
  if (!closure_solution.feasible) {
    return {};
  }

  for (auto& [length_idx, entry_set] : closure_solution.synthesized_entry_sets) {
    entry_sets_by_length[length_idx] = std::move(entry_set);
  }
  RetainSegmentPatternsForEntrySets(entry_sets_by_length, required_kinds, pattern_library);
  return entry_sets_by_length;
}

}  // namespace

auto CollectRequiredLengthIndices(const std::vector<HTree::LevelPlan>& levels) -> std::vector<unsigned>
{
  std::vector<unsigned> required_lengths;
  required_lengths.reserve(levels.size());
  for (const auto& level : levels) {
    if (level.aligned_length_idx > 0U) {
      required_lengths.push_back(level.aligned_length_idx);
    }
  }
  return NormalizeRequiredLengths(std::move(required_lengths));
}

auto ResolveRequiredSegmentFrontiers(std::vector<unsigned> required_length_indices, const BoundaryConstraints& boundary_constraints) -> RequiredSegmentFrontiers
{
  return RequiredSegmentFrontiers{
      .required_length_indices = std::move(required_length_indices),
      .required_kinds = boundary_constraints.force_branch_buffer ? SegmentFrontierKindSet::branchConstrained() : SegmentFrontierKindSet::allOnly(),
  };
}

auto SynthesizeSegmentFrontiers(const std::vector<SegmentChar>& base_segment_chars, BufferPatternLibrary& pattern_library,
                                const RequiredSegmentFrontiers& required_frontiers, bool use_canonical_boundary_primitive_basis) -> SegmentFrontierCatalog
{
  const auto closure_start = std::chrono::steady_clock::now();
  const auto registrations_before = pattern_library.compositionRegistrationCount();
  auto entry_sets = SynthesizeSegmentFrontierSets(base_segment_chars, pattern_library, required_frontiers, use_canonical_boundary_primitive_basis);
  const auto composed_patterns = pattern_library.compositionRegistrationCount() - registrations_before;
  const double closure_wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - closure_start).count();
  EmitLogTable(Loc::current(), "HTree Segment Closure", {"Metric", "Value"},
               {{"Mode", use_canonical_boundary_primitive_basis ? "canonical_boundary_primitive_basis" : "sparse_recursive"},
                {"Base Characters", ToLogTableCell(base_segment_chars.size())},
                {"Required Lengths", ToLogTableCell(required_frontiers.required_length_indices.size())},
                {"Available Lengths", ToLogTableCell(entry_sets.size())},
                {"Frontier Entries", ToLogTableCell(CountTotalSegmentCandidateFrontierEntries(entry_sets))},
                {"Composed Patterns", ToLogTableCell(composed_patterns)},
                {"Closure Wall (s)", ToLogTableCell(closure_wall_s)}});
  return SegmentFrontierCatalog(std::move(entry_sets));
}

}  // namespace icts::htree
