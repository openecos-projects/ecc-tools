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
 * @file DiscreteSolution.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-26
 * @brief Discrete H-tree selected-solution search implementation.
 */

#include "synthesis/htree/solution/discrete/DiscreteSolution.hh"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "LogTable.hh"
#include "Logger.hh"
#include "PatternId.hh"
#include "SegmentChar.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"
#include "synthesis/htree/plan/DepthPlan.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"
#include "synthesis/htree/segment_pruning/SegmentFrontierCatalog.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"
#include "synthesis/htree/segment_pruning/SegmentPruning.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/synthesis_state/SynthesisState.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::htree::discrete_solution {

namespace {

auto EmitDepthCandidateSummary(const HTree::LogContext& context, const std::vector<DepthSummary>& summaries) -> void
{
  LogTableRows rows;
  rows.reserve(summaries.size());
  for (const auto& summary : summaries) {
    const char* status = "failed";
    if (summary.selected) {
      status = "selected";
    } else if (summary.success) {
      status = "feasible";
    }
    rows.push_back({ToLogTableCell(summary.depth), ToLogTableCell(summary.leaf_count), status, ToLogTableCell(summary.used_explicit_target_depth),
                    ToLogTableCell(summary.candidate_solution_count), ToLogTableCell(summary.candidate_frontier_entry_count),
                    ToLogTableCell(summary.feasible_solution_count), ToLogTableCell(summary.feasible_frontier_entry_count),
                    ToLogTableCell(summary.final_frontier_count), ToLogTableCell(summary.split_group_count), ToLogTableCell(summary.split_extra_buffer_count),
                    ToLogTableCell(summary.split_local_depth), ToLogTableCell(summary.used_boundary_relaxation), ToLogTableCell(summary.selected_delay_ns),
                    ToLogTableCell(summary.selected_power_w), summary.failure_reason.empty() ? "n/a" : summary.failure_reason});
  }
  EmitLogTable(Loc::current(), "HTree Depth Candidate Summary",
               {"Depth", "Leaves", "Status", "Explicit", "Candidates", "Candidate Front", "Feasible", "Feasible Front", "Final Front", "Split Groups",
                "Extra Buffers", "Local Depth", "Relaxed", "Delay (ns)", "Power (W)", "Failure"},
               rows);
  EmitLogTable(Loc::current(), "HTree Candidate Scope", {"Property", "Value"},
               {{"Clock", context.clock_name}, {"Net", context.clock_net_name}, {"Sink Domain", context.sink_domain}, {"Stage", context.stage}});
}

}  // namespace

auto SelectDiscreteHTreeSolution(HTreeSynthesisState& state) -> HTreeSelectionBuild
{
  if (state.input == nullptr) {
    CTSLOG.error(Loc::current(), "HTree discrete solution requires synthesis input.");
  }
  if (state.config == nullptr) {
    CTSLOG.error(Loc::current(), "HTree discrete solution requires synthesis config.");
  }
  auto& result = state.result;
  const auto& config = *state.config;
  auto& segment_pattern_library = state.segmentPatterns();
  const auto& char_builder = state.charBuilder();

  const auto required_segment_frontiers
      = htree::ResolveRequiredSegmentFrontiers(htree::CollectRequiredLengthIndices(state.full_level_plans), state.search_boundary_constraints);
  const auto segment_frontier_catalog
      = htree::SynthesizeSegmentFrontiers(char_builder.get_segment_chars(), segment_pattern_library, required_segment_frontiers);
  if (segment_frontier_catalog.empty()) {
    CTSLOG.warn(Loc::current(), "HTree: segment frontier synthesis failed for the required aligned lengths.");
    HTreeSelectionBuild selection_build;
    selection_build.engine = htree::HTreeSelectionEngine::kDiscrete;
    selection_build.failure_reason = "missing_required_segment_frontiers";
    return selection_build;
  }
  auto exploration = htree::SearchTopologyDepthCandidates(result.output.topology, state.full_level_plans, state.depth_candidates, segment_frontier_catalog,
                                                          segment_pattern_library, state.search_boundary_constraints, char_builder.get_cap_lattice(),
                                                          result.diagnostics.char_slew_steps, config.target_depth.has_value(),
                                                          state.root_driver_compensation_input, state.sink_load_region_input, state.fanout_pruning_config);
  result.diagnostics.depth_candidate_count = exploration.summary.depth_summaries.size();

  auto covered_global_feasible_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
      exploration.output.global_feasible_pool, exploration.output.candidate_evaluations, result.output.topology, segment_pattern_library,
      exploration.output.sink_load_region_legality_context);
  auto covered_global_candidate_pool = htree::FilterGlobalEntriesBySinkLoadRegionCoverage(
      exploration.output.global_candidate_pool, exploration.output.candidate_evaluations, result.output.topology, segment_pattern_library,
      exploration.output.sink_load_region_legality_context);

  std::vector<htree::CandidateCharRef> per_depth_feasible_pareto_pool;
  std::optional<htree::CandidateCharRef> selected_feasible_ref;
  std::optional<htree::CandidateCharRef> selected_relaxed_ref;
  per_depth_feasible_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_feasible_pool.output.entries);
  selected_feasible_ref = htree::SelectAdaptiveGlobalEntry(per_depth_feasible_pareto_pool, exploration.output.candidate_evaluations, segment_pattern_library);
  if (!selected_feasible_ref.has_value() && config.allow_boundary_relaxation) {
    const auto per_depth_candidate_pareto_pool = htree::BuildPerDepthDelayPowerParetoRefs(covered_global_candidate_pool.output.entries);
    selected_relaxed_ref = htree::SelectAdaptiveGlobalEntry(per_depth_candidate_pareto_pool, exploration.output.candidate_evaluations, segment_pattern_library);
  }
  const auto selected_ref = selected_feasible_ref.has_value() ? selected_feasible_ref : selected_relaxed_ref;
  if (!selected_ref.has_value() || selected_ref->entry == nullptr) {
    HTreeSelectionBuild selection_build;
    selection_build.engine = htree::HTreeSelectionEngine::kDiscrete;
    if (!selected_feasible_ref.has_value() && !config.allow_boundary_relaxation) {
      selection_build.failure_reason = "no_strict_boundary_feasible_solution_any_depth";
    } else if (!covered_global_candidate_pool.summary.first_failure_reason.empty()) {
      selection_build.failure_reason = covered_global_candidate_pool.summary.first_failure_reason;
    } else {
      selection_build.failure_reason = exploration.output.global_candidate_pool.empty() ? "no_legal_depth_candidates" : "missing_best_char";
    }
    EmitDepthCandidateSummary(state.input->log_context, exploration.summary.depth_summaries);
    CTSLOG.warn(Loc::current(), "HTree: failed to select a strict-feasible H-tree characterization entry across depth candidates.");
    return selection_build;
  }

  const std::size_t selected_candidate_index = selected_ref->candidate_index;
  auto& selected_evaluation = exploration.output.candidate_evaluations.at(selected_candidate_index);
  auto& selected_summary = exploration.summary.depth_summaries.at(selected_candidate_index);
  selected_summary.selected = true;
  selected_summary.selected_power_w = selected_ref->entry->get_power();
  selected_summary.selected_delay_ns = selected_ref->entry->get_delay();
  selected_summary.split_group_count = selected_ref->split_group_count;
  selected_summary.split_extra_buffer_count = selected_ref->split_extra_buffer_count;
  selected_summary.split_local_depth = selected_ref->split_local_depth;
  const auto selected_sink_load_region_legality
      = htree::ResolveSinkLoadRegionLegality(result.output.topology, selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library,
                                             segment_pattern_library, exploration.output.sink_load_region_legality_context);
  if (!selected_sink_load_region_legality.legal) {
    EmitDepthCandidateSummary(state.input->log_context, exploration.summary.depth_summaries);
    CTSLOG.warn(Loc::current(), "HTree: selected global frontier entry is missing sink-load-region legality coverage.");
    return HTreeSelectionBuild{
        .selected = false,
        .failure_reason = "sink_load_region_legality_missing",
        .engine = htree::HTreeSelectionEngine::kDiscrete,
        .selected_solution = {},
    };
  }
  selected_summary.htree_load_group_count = selected_sink_load_region_legality.cap_distribution.group_count;
  selected_summary.htree_load_cap_min_pf = selected_sink_load_region_legality.cap_distribution.cap_min_pf;
  selected_summary.htree_load_cap_max_pf = selected_sink_load_region_legality.cap_distribution.cap_max_pf;
  selected_summary.htree_load_cap_mean_pf = selected_sink_load_region_legality.cap_distribution.cap_mean_pf;
  selected_summary.htree_load_cap_median_pf = selected_sink_load_region_legality.cap_distribution.cap_median_pf;
  selected_evaluation.best_char = *selected_ref->entry;
  EmitDepthCandidateSummary(state.input->log_context, exploration.summary.depth_summaries);

  htree::RootDriverCompensationPass selected_compensation_pass(state.root_driver_compensation_input);
  const auto selected_compensation_detail = selected_compensation_pass.evaluate(
      selected_ref->entry->get_pattern_id(), selected_evaluation.topology_pattern_library, segment_pattern_library, result.output.topology);

  const bool used_boundary_relaxation = !selected_feasible_ref.has_value();
  const std::string boundary_relaxation_reason = used_boundary_relaxation ? "no_strict_boundary_feasible_solution_any_depth" : "";
  const std::optional<double> boundary_relaxation_score
      = used_boundary_relaxation ? std::optional<double>(htree::CalcBoundaryRelaxationScore(
                                       *selected_evaluation.best_char, selected_evaluation.boundary_constraints, result.diagnostics.char_slew_steps))
                                 : std::nullopt;

  HTreeSelectionBuild selection_build;
  selection_build.selected = true;
  selection_build.engine = htree::HTreeSelectionEngine::kDiscrete;
  selection_build.selected_solution = htree::HTreeSelectedSolution{
      .engine = htree::HTreeSelectionEngine::kDiscrete,
      .evaluation = selected_evaluation,
      .summary = selected_summary,
      .compensation_stats = exploration.summary.root_driver_compensation_stats,
      .compensation_detail = selected_compensation_detail,
      .root_driver_clock_period_source = state.root_driver_clock_period_source,
      .used_boundary_relaxation = used_boundary_relaxation,
      .boundary_relaxation_reason = boundary_relaxation_reason,
      .boundary_relaxation_score = boundary_relaxation_score,
  };
  return selection_build;
}

}  // namespace icts::htree::discrete_solution
