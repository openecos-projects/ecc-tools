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
 * @file AnalyticalSolution.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-19
 * @brief Analytical H-tree build-stage orchestration
 */

#include "synthesis/htree/solution/analytical/AnalyticalSolution.hh"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "Logger.hh"
#include "synthesis/htree/analytical_solver/selection/AnalyticalSelection.hh"
#include "synthesis/htree/compensation/RootDriverCompensation.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"
#include "synthesis/htree/plan/DepthPlan.hh"
#include "synthesis/htree/solution/finalization/SolutionFinalizer.hh"
#include "synthesis/htree/synthesis_state/SynthesisState.hh"
#include "synthesis/htree/topology_pruning/TopologyPruning.hh"

namespace icts::htree::analytical_solution {
namespace as = analytical_selection;

auto SelectAnalyticalHTreeSolution(HTreeSynthesisState& state) -> HTreeSelectionBuild
{
  HTreeSelectionBuild selection_build;
  selection_build.engine = htree::HTreeSelectionEngine::kAnalytical;
  if (state.input == nullptr) {
    CTSLOG.error(Loc::current(), "HTree analytical solution requires synthesis input.");
  }
  if (state.config == nullptr) {
    CTSLOG.error(Loc::current(), "HTree analytical solution requires synthesis config.");
  }

  auto& result = state.result;
  const auto& input = *state.input;
  auto& segment_pattern_library = state.segmentPatterns();
  const auto& char_builder = state.charBuilder();

  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "HTree analytical solution requires explicit Design dependency.");
  }
  if (input.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "HTree analytical solution requires explicit Wrapper dependency.");
  }
  const auto analytical_attempt = as::TrySolveAnalyticalHTree(result.output.topology, state.full_level_plans, state.depth_candidates, segment_pattern_library,
                                                              state.search_boundary_constraints, state.fanout_pruning_config,
                                                              state.root_driver_compensation_input, state.sink_load_region_input, char_builder);
  result.diagnostics.analytical_model_set_count = analytical_attempt.model_set_count;
  result.diagnostics.analytical_rejected_fit_count = analytical_attempt.rejected_fit_count;
  result.diagnostics.analytical_structural_cap_operator_count = analytical_attempt.structural_cap_operator_count;
  result.diagnostics.analytical_evaluated_segment_count = analytical_attempt.evaluated_segment_count;
  result.diagnostics.analytical_generated_candidate_count = analytical_attempt.generated_candidate_count;
  result.diagnostics.analytical_validated_candidate_count = analytical_attempt.validated_candidate_count;
  result.diagnostics.analytical_validated_pareto_count = analytical_attempt.validated_pareto_count;
  result.diagnostics.analytical_selected_pareto_power_rank = analytical_attempt.selected_pareto_power_rank;
  result.diagnostics.analytical_validated_delay_min_ns = analytical_attempt.validated_delay_min_ns;
  result.diagnostics.analytical_validated_delay_median_ns = analytical_attempt.validated_delay_median_ns;
  result.diagnostics.analytical_validated_delay_max_ns = analytical_attempt.validated_delay_max_ns;
  result.diagnostics.analytical_validated_power_min_w = analytical_attempt.validated_power_min_w;
  result.diagnostics.analytical_validated_power_median_w = analytical_attempt.validated_power_median_w;
  result.diagnostics.analytical_validated_power_max_w = analytical_attempt.validated_power_max_w;
  result.diagnostics.analytical_solver_backend = analytical_attempt.backend_name;
  result.diagnostics.analytical_solver_status = analytical_attempt.solver_status;
  result.diagnostics.analytical_solver_variable_count = analytical_attempt.solver_variable_count;
  result.diagnostics.analytical_solver_binary_variable_count = analytical_attempt.solver_binary_variable_count;
  result.diagnostics.analytical_solver_continuous_variable_count = analytical_attempt.solver_continuous_variable_count;
  result.diagnostics.analytical_solver_constraint_count = analytical_attempt.solver_constraint_count;
  result.diagnostics.analytical_solver_wall_time_ms = analytical_attempt.solver_wall_time_ms;
  result.diagnostics.analytical_solver_objective_value = analytical_attempt.solver_objective_value;
  result.diagnostics.analytical_solver_optimality_gap = analytical_attempt.solver_optimality_gap;
  result.diagnostics.analytical_solver_min_delay_anchor_ns = analytical_attempt.solver_min_delay_anchor_ns;
  result.diagnostics.analytical_solver_min_power_anchor_w = analytical_attempt.solver_min_power_anchor_w;
  result.diagnostics.analytical_solver_total_delay_ns = analytical_attempt.solver_total_delay_ns;
  result.diagnostics.analytical_solver_total_power_w = analytical_attempt.solver_total_power_w;

  if (analytical_attempt.selected && analytical_attempt.selected_evaluation.best_char.has_value()) {
    result.diagnostics.analytical_mode_selected = true;
    auto selected_evaluation = analytical_attempt.selected_evaluation;
    auto selected_summary = analytical_attempt.selected_summary;
    result.diagnostics.depth_candidate_count = state.depth_candidates.size();

    htree::DepthSearchBuild analytical_exploration;
    as::ApplyAnalyticalRootDriverStats(analytical_exploration, analytical_attempt, state.root_driver_compensation_input);
    const htree::HTreeSelectedSolution selected_solution{
        .engine = htree::HTreeSelectionEngine::kAnalytical,
        .evaluation = selected_evaluation,
        .summary = selected_summary,
        .sink_load_region_legality = analytical_attempt.selected_sink_load_region_legality,
        .compensation_stats = analytical_exploration.summary.root_driver_compensation_stats,
        .compensation_detail = analytical_attempt.selected_compensation_detail,
        .root_driver_clock_period_source = state.root_driver_clock_period_source,
        .used_boundary_relaxation = false,
        .boundary_relaxation_reason = "",
        .boundary_relaxation_score = std::nullopt,
    };
    selection_build.selected = true;
    selection_build.selected_solution = selected_solution;
    return selection_build;
  }

  result.diagnostics.analytical_failure_reason
      = analytical_attempt.failure_reason.empty() ? "analytical_candidate_unavailable" : analytical_attempt.failure_reason;
  CTSLOG.warn(Loc::current(), "HTree: analytical candidate selection failed: ", result.diagnostics.analytical_failure_reason);
  selection_build.failure_reason = result.diagnostics.analytical_failure_reason;
  return selection_build;
}

}  // namespace icts::htree::analytical_solution
