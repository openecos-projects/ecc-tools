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
 * @file SolutionFinalizer.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-26
 * @brief Shared H-tree selected-solution finalization implementation.
 */

#include "synthesis/htree/solution/finalization/SolutionFinalizer.hh"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Inst.hh"
#include "LogTable.hh"
#include "Logger.hh"
#include "synthesis/htree/constraint/Constraint.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"
#include "synthesis/htree/embedding/Embedding.hh"
#include "synthesis/htree/segment_pruning/TopologyPatternLibrary.hh"
#include "synthesis/htree/solution/selection/SolutionSelection.hh"
#include "synthesis/htree/synthesis_state/SynthesisState.hh"

namespace icts::htree {

auto ToStageValue(HTreeSelectionEngine engine) -> std::string
{
  switch (engine) {
    case HTreeSelectionEngine::kDiscrete:
      return "discrete";
    case HTreeSelectionEngine::kAnalytical:
      return "analytical";
  }
  return "unknown";
}

auto FinalizeSelectedHTreeSolution(HTreeSynthesisState& state, const HTreeSelectedSolution& selected_solution) -> bool
{
  if (state.input == nullptr) {
    CTSLOG.error(Loc::current(), "HTree selected-solution finalization requires synthesis input.");
  }
  if (state.config == nullptr) {
    CTSLOG.error(Loc::current(), "HTree selected-solution finalization requires synthesis config.");
  }
  auto& result = state.result;
  const auto& input = *state.input;
  const auto& config = *state.config;
  auto& segment_pattern_library = state.segmentPatterns();

  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "HTree selected-solution finalization requires explicit Design dependency.");
  }
  if (input.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "HTree selected-solution finalization requires explicit Wrapper dependency.");
  }
  auto& design = *input.design;
  auto& wrapper = *input.wrapper;
  const auto& selected_evaluation = selected_solution.evaluation;
  const auto& selected_summary = selected_solution.summary;

  if (!selected_evaluation.best_char.has_value()) {
    result.summary.failure_reason = "missing_selected_best_char";
    return false;
  }

  result.summary.selected_depth = selected_evaluation.depth;
  result.output.best_char = *selected_evaluation.best_char;
  ApplyRootDriverCompensationSummary(result, selected_solution.compensation_stats, selected_solution.compensation_detail, *result.output.best_char);
  result.diagnostics.root_driver_compensation.clock_period_source = selected_solution.root_driver_clock_period_source;
  result.output.levels = selected_evaluation.levels;
  result.diagnostics.selected_final_frontier_count = selected_summary.final_frontier_count;
  result.diagnostics.selected_candidate_solution_count = selected_summary.candidate_solution_count;
  result.diagnostics.selected_candidate_frontier_entry_count = selected_summary.candidate_frontier_entry_count;
  result.diagnostics.selected_feasible_solution_count = selected_summary.feasible_solution_count;
  result.diagnostics.selected_feasible_frontier_entry_count = selected_summary.feasible_frontier_entry_count;
  result.diagnostics.min_top_input_slew_ns = selected_evaluation.boundary_constraints.min_top_input_slew_ns;
  result.diagnostics.top_input_slew_covering_idx = selected_evaluation.boundary_constraints.top_input_slew_covering_idx;
  result.diagnostics.htree_load_group_count = selected_summary.htree_load_group_count;
  result.diagnostics.htree_load_cap_min_pf = selected_summary.htree_load_cap_min_pf;
  result.diagnostics.htree_load_cap_max_pf = selected_summary.htree_load_cap_max_pf;
  result.diagnostics.htree_load_cap_mean_pf = selected_summary.htree_load_cap_mean_pf;
  result.diagnostics.htree_load_cap_median_pf = selected_summary.htree_load_cap_median_pf;

  if (selected_solution.used_boundary_relaxation) {
    result.summary.used_boundary_relaxation = true;
    result.diagnostics.boundary_relaxation_reason = selected_solution.boundary_relaxation_reason;
    result.diagnostics.boundary_relaxation_score = selected_solution.boundary_relaxation_score;
    CTSLOG.warn(Loc::current(), "HTree: selected a boundary-relaxed solution: ", result.diagnostics.boundary_relaxation_reason);
  }

  result.output.best_pattern = selected_evaluation.topology_pattern_library.materialize(result.output.best_char->get_pattern_id());
  ApplySelectedPatternToLevelPlans(wrapper, result, segment_pattern_library);
  const std::string selected_root_driver_cell_master = ResolveSelectedRootDriverCellMaster(result.output.levels);
  if (config.enable_root_driver_sizing && !ValidateRootDriverSizing(design, wrapper, result, selected_root_driver_cell_master)) {
    result.summary.failure_reason = "root_driver_sizing_precheck_failed";
    return false;
  }

  BuildEmbedding(wrapper, result, segment_pattern_library, config);
  result.summary.success = result.summary.failure_reason.empty() && result.output.best_char.has_value() && result.output.best_pattern.has_value()
                           && result.output.root_output_pin != nullptr && result.output.root_net != nullptr;
  if (result.summary.success && config.enable_root_driver_sizing) {
    if (!ApplyRootDriverSizing(design, wrapper, result, selected_root_driver_cell_master)) {
      CTSLOG.error(Loc::current(), "HTree: prevalidated root-driver sizing failed during embedding construction.");
    }
  } else if (result.summary.success && result.output.root_inst != nullptr) {
    result.diagnostics.selected_root_driver_cell_master = result.output.root_inst->get_cell_master();
  }
  if (result.summary.success) {
    std::size_t selected_level_buffer_count = 0U;
    std::optional<double> selected_level_buffer_area_um2 = 0.0;
    for (const auto& level : result.output.levels) {
      selected_level_buffer_count += level.selected_buffer_count;
      if (selected_level_buffer_area_um2.has_value() && level.selected_buffer_area_um2.has_value()) {
        *selected_level_buffer_area_um2 += *level.selected_buffer_area_um2;
      } else {
        selected_level_buffer_area_um2 = std::nullopt;
      }
    }
    EmitLogTable(Loc::current(), "Selected HTree Summary", {"Property", "Value"},
                 {{"Clock", input.log_context.clock_name},
                  {"Net", input.log_context.clock_net_name},
                  {"Sink Domain", input.log_context.sink_domain},
                  {"Selection Engine", ToStageValue(selected_solution.engine)},
                  {"Depth", ToLogTableCell(selected_evaluation.depth)},
                  {"Leaves", ToLogTableCell(selected_summary.leaf_count)},
                  {"Candidate Solutions", ToLogTableCell(selected_summary.candidate_solution_count)},
                  {"Candidate Frontier Entries", ToLogTableCell(selected_summary.candidate_frontier_entry_count)},
                  {"Feasible Solutions", ToLogTableCell(selected_summary.feasible_solution_count)},
                  {"Feasible Frontier Entries", ToLogTableCell(selected_summary.feasible_frontier_entry_count)},
                  {"Final Frontier", ToLogTableCell(selected_summary.final_frontier_count)},
                  {"Delay (ns)", ToLogTableCell(result.output.best_char->get_delay())},
                  {"Power (W)", ToLogTableCell(result.output.best_char->get_power())},
                  {"Boundary Relaxed", ToLogTableCell(result.summary.used_boundary_relaxation)}});
    EmitLogTable(
        Loc::current(), "HTree Synthesis Overview", {"Metric", "Value"},
        {{"Clock", input.log_context.clock_name},
         {"Sink Domain", input.log_context.sink_domain},
         {"Levels", ToLogTableCell(result.output.levels.size())},
         {"Inserted Instances", ToLogTableCell(result.output.inserted_insts.size())},
         {"Inserted Pins", ToLogTableCell(result.output.inserted_pins.size())},
         {"Inserted Nets", ToLogTableCell(result.output.inserted_nets.size())},
         {"Selected Level Buffers", ToLogTableCell(selected_level_buffer_count)},
         {"Selected Level Buffer Area (um^2)", selected_level_buffer_area_um2.has_value() ? ToLogTableCell(*selected_level_buffer_area_um2) : "unavailable"},
         {"Root Driver", result.diagnostics.selected_root_driver_cell_master.empty() ? "n/a" : result.diagnostics.selected_root_driver_cell_master},
         {"Pruned Leaf Single-load Buffers", ToLogTableCell(result.diagnostics.pruned_leaf_single_load_buffers)},
         {"Split Extra Buffers", ToLogTableCell(result.diagnostics.embedded_split_sub_buffer_count)},
         {"Analytical Selected", ToLogTableCell(result.diagnostics.analytical_mode_selected)}});
  }
  return result.summary.success;
}

}  // namespace icts::htree
