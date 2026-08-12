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
 * @file SegmentPatternLibrary.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief H-tree segment buffering-pattern lookup and monotonic composition.
 */

#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "Logger.hh"
#include "PatternId.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/segment_pruning/BufferStrength.hh"

namespace icts::htree {

struct BufferPatternLibrary
{
  explicit BufferPatternLibrary(Wrapper& wrapper) : _strength_table(wrapper) {}

  auto add(BufferingPattern pattern) -> bool
  {
    _last_failure_reason.clear();
    auto enriched_pattern = enrichBoundaryState(std::move(pattern));
    if (!enriched_pattern.has_value()) {
      return false;
    }
    pattern = std::move(*enriched_pattern);
    const PatternId pattern_id = pattern.get_pattern_id();
    composition_states[pattern_id] = PatternCompositionState{
        .terminal_semantic = pattern.hasTerminalBranchBuffer() ? TerminalSemantic::kBranchBuffered : TerminalSemantic::kLeafUnbuffered,
        .monotonic_boundary_state = pattern.get_monotonic_boundary_state(),
    };
    patterns[pattern_id] = std::move(pattern);
    return true;
  }

  auto getLastFailureReason() const -> const std::string& { return _last_failure_reason; }

  auto find(PatternId pattern_id) const -> const BufferingPattern*
  {
    auto it = patterns.find(pattern_id);
    return it == patterns.end() ? nullptr : &it->second;
  }

  auto getCompositionState(PatternId pattern_id) const -> PatternCompositionState
  {
    const auto it = composition_states.find(pattern_id);
    if (it == composition_states.end()) {
      CTSLOG.error(Loc::current(), "HTree: missing segment pattern composition-state cache entry.");
    }
    return it->second;
  }

  auto getTerminalSemantic(PatternId pattern_id) const -> TerminalSemantic { return getCompositionState(pattern_id).terminal_semantic; }

  auto retainOnly(const std::vector<PatternId>& retained_pattern_ids) -> void
  {
    std::unordered_set<PatternId> retained;
    retained.reserve(retained_pattern_ids.size());
    for (const auto pattern_id : retained_pattern_ids) {
      retained.insert(pattern_id);
    }

    std::unordered_map<PatternId, BufferingPattern> retained_patterns;
    retained_patterns.reserve(retained.size());
    for (const auto pattern_id : retained) {
      auto pattern_it = patterns.find(pattern_id);
      if (pattern_it != patterns.end()) {
        retained_patterns.emplace(pattern_id, std::move(pattern_it->second));
      }
    }

    std::unordered_map<PatternId, PatternCompositionState> retained_composition_states;
    retained_composition_states.reserve(retained.size());
    for (const auto pattern_id : retained) {
      auto state_it = composition_states.find(pattern_id);
      if (state_it != composition_states.end()) {
        retained_composition_states.emplace(pattern_id, state_it->second);
      }
    }

    patterns = std::move(retained_patterns);
    composition_states = std::move(retained_composition_states);
  }

 private:
  auto resolveBoundaryBufferState(const BoundaryBufferState& explicit_state, const std::string& cell_master) -> std::optional<BoundaryBufferState>
  {
    if (explicit_state.has_buffer) {
      if (explicit_state.strength_rank > 0U) {
        return explicit_state;
      }
      _last_failure_reason = "invalid_explicit_buffer_strength_rank:" + cell_master;
      return std::nullopt;
    }
    if (cell_master.empty()) {
      return explicit_state;
    }
    const auto strength_rank = _strength_table.getStrengthRank(cell_master);
    if (!strength_rank.has_value()) {
      _last_failure_reason = "unavailable_buffer_strength_rank:" + cell_master;
      return std::nullopt;
    }
    return BoundaryBufferState{.has_buffer = true, .strength_rank = *strength_rank};
  }

  auto resolveBoundaryState(const BufferingPattern& pattern) -> std::optional<MonotonicBoundaryState>
  {
    const auto explicit_state = pattern.get_monotonic_boundary_state();
    if (!pattern.isBufferPattern()) {
      return explicit_state;
    }

    const auto& cell_masters = pattern.get_cell_masters();
    if (cell_masters.empty()) {
      return explicit_state;
    }

    const auto source = resolveBoundaryBufferState(explicit_state.source, cell_masters.front());
    if (!source.has_value()) {
      return std::nullopt;
    }
    const auto sink = resolveBoundaryBufferState(explicit_state.sink, cell_masters.back());
    if (!sink.has_value()) {
      return std::nullopt;
    }
    return MonotonicBoundaryState{.source = *source, .sink = *sink};
  }

  auto enrichBoundaryState(BufferingPattern pattern) -> std::optional<BufferingPattern>
  {
    const auto boundary_state = resolveBoundaryState(pattern);
    if (!boundary_state.has_value()) {
      return std::nullopt;
    }
    if (*boundary_state == pattern.get_monotonic_boundary_state()) {
      return pattern;
    }

    return BufferingPattern(pattern.get_length_idx(), pattern.get_pattern_id(), pattern.get_buffer_positions(), pattern.get_cell_masters(),
                            pattern.hasTerminalBranchBuffer(), *boundary_state);
  }

 public:
  std::unordered_map<PatternId, BufferingPattern> patterns;
  std::unordered_map<PatternId, PatternCompositionState> composition_states;

 private:
  BufferStrengthTable _strength_table;
  std::string _last_failure_reason;
};

class SegmentPatternLibraryCombiner
{
 public:
  SegmentPatternLibraryCombiner(BufferPatternLibrary& library, unsigned start_id) : _library(&library), _next_id(start_id) {}

  auto canCompose(PatternId upstream, PatternId downstream) const -> bool
  {
    const auto* upstream_pattern = _library->find(upstream);
    const auto* downstream_pattern = _library->find(downstream);
    if (upstream_pattern == nullptr || downstream_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: missing segment pattern during monotonic-state validation.");
    }
    return BufferingPattern::canConcatMonotonic(*upstream_pattern, *downstream_pattern);
  }

  auto combine(PatternId upstream, PatternId downstream) const -> PatternId
  {
    const auto* upstream_pattern = _library->find(upstream);
    const auto* downstream_pattern = _library->find(downstream);
    if (upstream_pattern == nullptr || downstream_pattern == nullptr) {
      CTSLOG.error(Loc::current(), "HTree: missing segment pattern during composition.");
    }
    if (!BufferingPattern::canConcatMonotonic(*upstream_pattern, *downstream_pattern)) {
      CTSLOG.error(Loc::current(), "HTree: invalid non-monotonic segment pattern composition.");
    }

    const PatternId merged_pattern_id = PatternId::segment(_next_id++);
    auto merged_pattern = BufferingPattern::concat(*upstream_pattern, *downstream_pattern);
    if (!_library->add(BufferingPattern(merged_pattern.get_length_idx(), merged_pattern_id, merged_pattern.get_buffer_positions(),
                                        merged_pattern.get_cell_masters(), merged_pattern.hasTerminalBranchBuffer(),
                                        merged_pattern.get_monotonic_boundary_state()))) {
      CTSLOG.error(Loc::current(), "HTree: composed segment pattern lost a validated buffer-strength rank: ", _library->getLastFailureReason());
    }
    return merged_pattern_id;
  }

  auto get_next_id() const -> unsigned { return _next_id; }

 private:
  BufferPatternLibrary* _library = nullptr;
  mutable unsigned _next_id;
};

}  // namespace icts::htree
