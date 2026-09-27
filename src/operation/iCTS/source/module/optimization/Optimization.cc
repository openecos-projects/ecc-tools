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
 * @file Optimization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS post-synthesis optimization module entry implementation.
 */

#include "optimization/Optimization.hh"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "config/Config.hh"
#include "data_manager/DataManager.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "io/Wrapper.hh"
#include "optimization/clock_sizing_edit/ClockSizingAcceptedEdit.hh"
#include "optimization/model/ClockSizingOptimizationData.hh"
#include "optimization/policy/OptimizationPolicy.hh"
#include "optimization/preparation/OptimizationPreparation.hh"
#include "optimization/solver/OptimizationSolver.hh"

namespace icts {
namespace oi = clock_sizing_optimization;
namespace {

struct OptimizationStageRuntimeProfile
{
  Monitor monitor;
  double prepare_timing_s = 0.0;
  double prepare_timing_cpu_s = 0.0;
  double clock_work_s = 0.0;
  double clock_work_cpu_s = 0.0;
  double commit_and_publish_s = 0.0;
  double commit_and_publish_cpu_s = 0.0;

  ~OptimizationStageRuntimeProfile()
  {
    // This object is created before stage-local ownership. Its final sample
    // includes timing-guard rollback/release and local object destruction.
    const double total_s = monitor.getElapsedSeconds();
    const double total_cpu_s = monitor.getCPUSeconds();
    const double other_s = total_s - prepare_timing_s - clock_work_s - commit_and_publish_s;
    const double other_cpu_s = total_cpu_s - prepare_timing_cpu_s - clock_work_cpu_s - commit_and_publish_cpu_s;
    EmitLogTable(Loc::current(), "CTS Optimization Stage Runtime", {"Stage", "Wall (s)", "CPU (s)"},
                 {{"Shared Timing Preparation", ToLogTableCell(prepare_timing_s), ToLogTableCell(prepare_timing_cpu_s)},
                  {"All Clock Work", ToLogTableCell(clock_work_s), ToLogTableCell(clock_work_cpu_s)},
                  {"Commit and Publication", ToLogTableCell(commit_and_publish_s), ToLogTableCell(commit_and_publish_cpu_s)},
                  {"Setup, Reporting and Cleanup", ToLogTableCell(other_s), ToLogTableCell(other_cpu_s)},
                  {"Stage Total", ToLogTableCell(total_s), ToLogTableCell(total_cpu_s)}});
  }
};

class OptimizationClockWorkMonitor
{
 public:
  explicit OptimizationClockWorkMonitor(OptimizationStageRuntimeProfile& profile) : _profile(profile) {}
  ~OptimizationClockWorkMonitor()
  {
    _profile.clock_work_s = _monitor.getElapsedSeconds();
    _profile.clock_work_cpu_s = _monitor.getCPUSeconds();
  }

  OptimizationClockWorkMonitor(const OptimizationClockWorkMonitor&) = delete;
  OptimizationClockWorkMonitor(OptimizationClockWorkMonitor&&) = delete;
  auto operator=(const OptimizationClockWorkMonitor&) -> OptimizationClockWorkMonitor& = delete;
  auto operator=(OptimizationClockWorkMonitor&&) -> OptimizationClockWorkMonitor& = delete;

 private:
  OptimizationStageRuntimeProfile& _profile;
  Monitor _monitor;
};

class OptimizationTimingGuard
{
 public:
  explicit OptimizationTimingGuard(DataManager& data_manager) : _data_manager(data_manager) {}
  ~OptimizationTimingGuard() { _data_manager.discardOptimizationTiming(); }

  OptimizationTimingGuard(const OptimizationTimingGuard&) = delete;
  OptimizationTimingGuard(OptimizationTimingGuard&&) = delete;
  auto operator=(const OptimizationTimingGuard&) -> OptimizationTimingGuard& = delete;
  auto operator=(OptimizationTimingGuard&&) -> OptimizationTimingGuard& = delete;

 private:
  DataManager& _data_manager;
};

auto captureClockTimingSummary(const Clock& clock, const FastSTA& fast_sta, FastStaContextId context_id, const oi::ClockSizingSummary& optimization,
                               double target_skew_ns) -> std::optional<ClockTimingSummary>
{
  const auto sink_arrivals = fast_sta.collectClockSinkArrivals(context_id);
  if (!optimization.valid || sink_arrivals.empty()) {
    return std::nullopt;
  }

  double min_arrival_ns = sink_arrivals.front().arrival_ns;
  double max_arrival_ns = sink_arrivals.front().arrival_ns;
  double total_arrival_ns = 0.0;
  for (const auto& sink : sink_arrivals) {
    min_arrival_ns = std::min(min_arrival_ns, sink.arrival_ns);
    max_arrival_ns = std::max(max_arrival_ns, sink.arrival_ns);
    total_arrival_ns += sink.arrival_ns;
  }

  return ClockTimingSummary{
      .clock = clock.get_clock_name(),
      .sink_count = sink_arrivals.size(),
      .target_skew_ns = target_skew_ns,
      .initial_skew_ns = optimization.before.skew.skew_ns,
      .optimized_skew_ns = optimization.after.skew.skew_ns,
      .min_insertion_latency_ns = min_arrival_ns,
      .max_insertion_latency_ns = max_arrival_ns,
      .mean_insertion_latency_ns = total_arrival_ns / static_cast<double>(sink_arrivals.size()),
      .target_met = optimization.target_met,
  };
}

auto timingStatusName(FastStaTimingStatus status) -> const char*
{
  switch (status) {
    case FastStaTimingStatus::kNotRun:
      return "not_run";
    case FastStaTimingStatus::kComplete:
      return "complete";
    case FastStaTimingStatus::kInvalidInput:
      return "invalid_input";
    case FastStaTimingStatus::kUnsupported:
      return "unsupported";
  }
  return "unknown";
}

auto logFastStaTimingSummary(const FastSTA& fast_sta, FastStaContextId context_id, std::string_view checkpoint, std::string_view owner_clock) -> void
{
  const auto summary = fast_sta.queryTimingSummary(context_id);
  if (!summary.has_value()) {
    CTSLOG.warn(Loc::current(), "FastSTA timing summary unavailable for context ", context_id, ".");
    return;
  }
  CTSLOG.info(Loc::current(), "FastSTA timing summary: checkpoint=", checkpoint, ", scope=whole_network, skew_clock=", owner_clock,
              ", status=", timingStatusName(summary->status), ", logic_nodes=", summary->node_count, ", logic_nets=", summary->net_count,
              ", endpoints=", summary->endpoint_count, ", launches=", summary->launch_count, ", relations=", summary->relation_count,
              ", setup_relations=", summary->setup_relation_count, ", hold_relations=", summary->hold_relation_count,
              ", logic_rc_nodes=", summary->logic_rc_node_count, ", logic_rc_edges=", summary->logic_rc_edge_count,
              ", receiver_pin_caps=", summary->logic_pin_cap_count, ", setup_wns_ns=", summary->setup_wns_ns, ", setup_tns_ns=", summary->setup_tns_ns,
              ", hold_wns_ns=", summary->hold_wns_ns, ", hold_tns_ns=", summary->hold_tns_ns, ", setup_violations=", summary->setup_violation_count,
              ", hold_violations=", summary->hold_violation_count, ", slew_violations=", summary->slew_violation_count,
              ", cap_violations=", summary->cap_violation_count, ", fanout_violations=", summary->fanout_violation_count,
              ", conditional_fallback_arcs=", summary->conditional_fallback_arc_count,
              ", conditional_extrema_bundles=", summary->conditional_extrema_bundle_count, ", max_slew_ns=", summary->max_slew_ns,
              ", max_cap_pf=", summary->max_cap_pf, ", max_fanout=", summary->max_fanout, ", skew_ns=", summary->max_skew_ns,
              ", feasible_projection_width_ns[min/median/p95/max]=", summary->window_width_min_ns, "/", summary->window_width_median_ns, "/",
              summary->window_width_p95_ns, "/", summary->window_width_max_ns, ", projection_conflicts=", summary->window_conflict_count,
              ", clock_propagation_runtime_s=", summary->clock_propagation_runtime_s, ", logic_propagation_runtime_s=", summary->logic_propagation_runtime_s,
              ", relation_extraction_runtime_s=", summary->relation_extraction_runtime_s, ", timing_runtime_s=", summary->runtime_s,
              summary->fallback_reason.empty() ? "" : ", fallback=" + summary->fallback_reason);
}

struct MasterTransitionAggregate
{
  std::size_t count = 0U;
  double area_delta_um2 = 0.0;
};

auto formatTimingDiagnostic(double value) -> std::string
{
  std::ostringstream text;
  text << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return text.str();
}

auto logClockSizingSummary(const Clock& clock, double target_skew_ns, std::size_t buffer_master_candidate_count, const oi::ClockSizingSummary& summary) -> void
{
  const auto& profile = summary.profile;
  EmitLogTable(Loc::current(), "CTS Optimization Setup", {"Property", "Value"},
               {{"Clock", clock.get_clock_name()},
                {"Net", clock.get_clock_net_name()},
                {"Target Skew (ns)", ToLogTableCell(target_skew_ns)},
                {"Solve Mode", summary.solve_mode.empty() ? "unknown" : summary.solve_mode},
                {"Buffer Master Candidates", ToLogTableCell(buffer_master_candidate_count)},
                {"Graph Nodes", ToLogTableCell(profile.node_count)},
                {"Owned Clock Nodes", ToLogTableCell(profile.owned_clock_node_count)},
                {"Graph Nets", ToLogTableCell(profile.net_count)},
                {"Graph Sinks", ToLogTableCell(profile.sink_count)},
                {"Buffer Inputs", ToLogTableCell(profile.buffer_input_count)},
                {"Buffer Outputs", ToLogTableCell(profile.buffer_output_count)},
                {"Optimizable Buffers", ToLogTableCell(profile.optimizable_buffer_count)},
                {"Generated Candidates", ToLogTableCell(profile.generated_candidate_count)}});

  EmitLogTable(Loc::current(), "CTS Optimization Evolution", {"Metric", "Before", "After / Result"},
               {{"Skew (ns)", ToLogTableCell(summary.before.skew.skew_ns), ToLogTableCell(summary.after.skew.skew_ns)},
                {"Area (um^2)", ToLogTableCell(summary.before.power.area_um2), ToLogTableCell(summary.after.power.area_um2)},
                {"Power (W)", ToLogTableCell(summary.before.power.total_power_w), ToLogTableCell(summary.after.power.total_power_w)},
                {"Capacitance Regressions", ToLogTableCell(summary.before.cap.violation_count), ToLogTableCell(summary.after.cap.violation_count)},
                {"Slew Regressions", ToLogTableCell(summary.before.slew.violation_count), ToLogTableCell(summary.after.slew.violation_count)},
                {"Valid", "-", ToLogTableCell(summary.valid)},
                {"Target Met", "-", ToLogTableCell(summary.target_met)},
                {"Changed", "-", ToLogTableCell(summary.changed)},
                {"Iterations", "-", ToLogTableCell(summary.iteration_count)},
                {"Trials", "-", ToLogTableCell(summary.trial_count)},
                {"Batch Trials", "-", ToLogTableCell(summary.batch_trial_count)},
                {"Accepted Edits", "-", ToLogTableCell(summary.accepted_edit_count)},
                {"Accepted Batches", "-", ToLogTableCell(summary.accepted_batch_count)},
                {"Rejected Candidates", "-", ToLogTableCell(summary.rejected_candidate_count)},
                {"Capacitance Rejected", "-", ToLogTableCell(summary.cap_rejected_count)},
                {"Slew Rejected", "-", ToLogTableCell(summary.slew_rejected_count)},
                {"Stop Reason", "-", summary.stop_reason.empty() ? "n/a" : summary.stop_reason}});

  if (summary.rejected_state.has_value()) {
    const auto& rejected = *summary.rejected_state;
    LogTableRows rejection_rows{{"Capacitance Regressions", ToLogTableCell(rejected.cap.violation_count)},
                                {"Slew Regressions", ToLogTableCell(rejected.slew.violation_count)},
                                {"Unavailable Slew States", ToLogTableCell(rejected.slew.unavailable_count)}};
    if (rejected.slew.worst_violation.has_value()) {
      const auto& worst = *rejected.slew.worst_violation;
      rejection_rows.insert(rejection_rows.end(), {{"Worst Slew Node", worst.node_name},
                                                   {"Baseline Slew (ns)", formatTimingDiagnostic(rejected.slew.worst_baseline_slew_ns)},
                                                   {"Rejected Slew (ns)", formatTimingDiagnostic(worst.slew_ns)},
                                                   {"Configured Slew Limit (ns)", formatTimingDiagnostic(worst.max_slew_ns)},
                                                   {"Allowed Slew (ns)", formatTimingDiagnostic(rejected.slew.worst_allowed_slew_ns)},
                                                   {"Slew Excess (ns)", formatTimingDiagnostic(worst.slew_ns - rejected.slew.worst_allowed_slew_ns)}});
    }
    if (summary.rejected_slew_stage.has_value()) {
      const auto& stage = *summary.rejected_slew_stage;
      rejection_rows.insert(rejection_rows.end(), {{"Worst Slew Net", stage.net_name},
                                                   {"Worst Slew Analysis", stage.analysis == FastStaAnalysisKind::kEarly ? "early" : "late"},
                                                   {"Worst Slew Transition", stage.transition == FastStaTransition::kRise ? "rise" : "fall"},
                                                   {"Worst Slew Launch", stage.launch_pin_name},
                                                   {"Worst Slew Input", stage.input_pin_name},
                                                   {"Worst Slew Cell Master", stage.cell_master}});
    }
    EmitLogTable(Loc::current(), "CTS Optimization Rejected State", {"Property", "Value"}, rejection_rows);
    if (summary.rejected_slew_stage.has_value()) {
      const auto& stage = *summary.rejected_slew_stage;
      const auto& restored = summary.restored_slew_stage;
      EmitLogTable(
          Loc::current(), "CTS Optimization Rejected Slew Stage", {"Property", "Restored", "Rejected"},
          {{"Input Arrival (ns)", restored.has_value() ? formatTimingDiagnostic(restored->input_arrival_ns) : "n/a",
            formatTimingDiagnostic(stage.input_arrival_ns)},
           {"Input Slew (ns)", restored.has_value() ? formatTimingDiagnostic(restored->input_slew_ns) : "n/a", formatTimingDiagnostic(stage.input_slew_ns)},
           {"Arrival (ns)", restored.has_value() ? formatTimingDiagnostic(restored->arrival_ns) : "n/a", formatTimingDiagnostic(stage.arrival_ns)},
           {"Slew (ns)", restored.has_value() ? formatTimingDiagnostic(restored->slew_ns) : "n/a", formatTimingDiagnostic(stage.slew_ns)},
           {"Stage Delay (ns)", restored.has_value() ? formatTimingDiagnostic(restored->stage_delay_ns) : "n/a", formatTimingDiagnostic(stage.stage_delay_ns)},
           {"Variant Index", restored.has_value() ? ToLogTableCell(restored->selected_variant_index) : "n/a", ToLogTableCell(stage.selected_variant_index)},
           {"Input Pin", restored.has_value() ? restored->input_pin_name : "n/a", stage.input_pin_name},
           {"Launch Pin", restored.has_value() ? restored->launch_pin_name : "n/a", stage.launch_pin_name}});
    }
    if (!summary.rejected_edits.empty()) {
      LogTableRows edit_rows;
      for (const auto& [inst_name, edit] : summary.rejected_edits) {
        edit_rows.push_back({inst_name, edit.from_master, edit.to_master});
      }
      EmitLogTable(Loc::current(), "CTS Optimization Rejected Master Changes", {"Inst", "From", "To"}, edit_rows);
    }
  }

  std::map<std::pair<std::string, std::string>, MasterTransitionAggregate> transitions;
  for (const auto& edit : summary.accepted_edits) {
    auto& aggregate = transitions[{edit.from_master, edit.to_master}];
    ++aggregate.count;
    aggregate.area_delta_um2 += edit.area_delta_um2;
  }
  LogTableRows transition_rows;
  for (const auto& [masters, aggregate] : transitions) {
    transition_rows.push_back(
        {clock.get_clock_name(), masters.first, masters.second, ToLogTableCell(aggregate.count), ToLogTableCell(aggregate.area_delta_um2)});
  }
  if (transition_rows.empty()) {
    transition_rows.push_back({clock.get_clock_name(), "none", "none", "0", "0"});
  }
  EmitLogTable(Loc::current(), "CTS Optimization Master Transitions", {"Clock", "From", "To", "Count", "Area Delta (um^2)"}, transition_rows);

  const double clock_detail_s = profile.build_route_tree_cache_s + profile.build_clock_sizing_context_s + profile.inject_route_trees_s
                                + profile.separate_timing_relations_s + profile.collect_optimizable_buffers_s + profile.collect_cap_baseline_s
                                + profile.collect_slew_baseline_s + profile.solve_clock_s + profile.apply_accepted_edits_s + profile.refresh_timing_contexts_s
                                + profile.finalize_clock_context_s;
  const double clock_detail_cpu_s = profile.build_clock_sizing_context_cpu_s + profile.separate_timing_relations_cpu_s
                                    + profile.collect_optimizable_buffers_cpu_s + profile.collect_cap_baseline_cpu_s + profile.collect_slew_baseline_cpu_s
                                    + profile.solve_clock_cpu_s + profile.apply_accepted_edits_cpu_s + profile.refresh_timing_contexts_cpu_s
                                    + profile.finalize_clock_context_cpu_s;
  const double solver_detail_s = profile.capture_initial_state_s + profile.build_topology_index_s + profile.generate_batch_candidates_s
                                 + profile.batch_trial_eval_s + profile.apply_accepted_batch_s;
  EmitLogTable(Loc::current(), "CTS Optimization Runtime Profile", {"Clock Work", "Wall (s)", "CPU (s)"},
               {{"Route Tree Cache", ToLogTableCell(profile.build_route_tree_cache_s), "n/a"},
                {"Clock Sizing Context", ToLogTableCell(profile.build_clock_sizing_context_s), ToLogTableCell(profile.build_clock_sizing_context_cpu_s)},
                {"Timing Relations", ToLogTableCell(profile.separate_timing_relations_s), ToLogTableCell(profile.separate_timing_relations_cpu_s)},
                {"Route Injection", ToLogTableCell(profile.inject_route_trees_s), "n/a"},
                {"Collect Buffers", ToLogTableCell(profile.collect_optimizable_buffers_s), ToLogTableCell(profile.collect_optimizable_buffers_cpu_s)},
                {"Collect Cap Baseline", ToLogTableCell(profile.collect_cap_baseline_s), ToLogTableCell(profile.collect_cap_baseline_cpu_s)},
                {"Collect Slew Baseline", ToLogTableCell(profile.collect_slew_baseline_s), ToLogTableCell(profile.collect_slew_baseline_cpu_s)},
                {"Solve", ToLogTableCell(profile.solve_clock_s), ToLogTableCell(profile.solve_clock_cpu_s)},
                {"Apply Edits", ToLogTableCell(profile.apply_accepted_edits_s), ToLogTableCell(profile.apply_accepted_edits_cpu_s)},
                {"Accepted Edit Timing Refresh", ToLogTableCell(profile.refresh_timing_contexts_s), ToLogTableCell(profile.refresh_timing_contexts_cpu_s)},
                {"Capture Result and Release Context", ToLogTableCell(profile.finalize_clock_context_s), ToLogTableCell(profile.finalize_clock_context_cpu_s)},
                {"Clock Other", ToLogTableCell(profile.clock_total_s - clock_detail_s), ToLogTableCell(profile.clock_total_cpu_s - clock_detail_cpu_s)},
                {"Clock Total", ToLogTableCell(profile.clock_total_s), ToLogTableCell(profile.clock_total_cpu_s)}});
  EmitLogTable(Loc::current(), "CTS Optimization Solver Runtime", {"Within Solve", "Runtime (s)"},
               {{"Capture Initial State", ToLogTableCell(profile.capture_initial_state_s)},
                {"Topology Index", ToLogTableCell(profile.build_topology_index_s)},
                {"Candidate Generation", ToLogTableCell(profile.generate_batch_candidates_s)},
                {"Batch Trial Evaluation", ToLogTableCell(profile.batch_trial_eval_s)},
                {"Accepted Batch Apply", ToLogTableCell(profile.apply_accepted_batch_s)},
                {"Solver Detail", ToLogTableCell(solver_detail_s)}});
}

}  // namespace

auto Optimization::run() -> OptimizationSummary
{
  Monitor monitor;
  OptimizationStageRuntimeProfile stage_profile;
  CTSLOG.info(Loc::current(), "Starting CTS optimization...");
  auto local_design = CTSDM.cloneDesign();
  auto clock_layout = CTSDM.getClockLayout();
  const auto& config = CTSDM.getConfig();
  auto& design = *local_design;
  auto& wrapper = CTSDM.getWrapper();
  auto& fast_sta = CTSDM.getFastSTA();
  OptimizationSummary optimization_summary;
  const auto& policy = oi::DefaultOptimizationPolicy();
  if (!oi::ValidateOptimizationPolicy(policy)) {
    CTSLOG.warn(Loc::current(), "Optimization: internal optimizer policy is invalid.");
    optimization_summary.success = false;
    optimization_summary.status = "failed";
    optimization_summary.reason = "invalid_optimizer_options";
    return optimization_summary;
  }

  const auto clocks = design.get_clocks();
  optimization_summary.clock_count = clocks.size();
  const auto master_collection = oi::CollectClockSizingBufferMasters(oi::ClockSizingMasterQueryInput{
      .wrapper = &wrapper,
      .buffer_cell_masters = &config.get_buffer_types(),
  });
  const auto& master_infos = master_collection.masters;
  EmitLogTable(Loc::current(), "CTS Optimization Stage Setup", {"Property", "Value"},
               {{"Clocks", ToLogTableCell(clocks.size())},
                {"Configured Buffer Masters", ToLogTableCell(config.get_buffer_types().size())},
                {"Legal Buffer Master Candidates", ToLogTableCell(master_infos.size())},
                {"Target Skew (ns)", ToLogTableCell(oi::ResolveClockTargetSkewNs(config))}});
  Monitor phase_monitor;
  const auto timing_status = CTSDM.beginOptimizationTiming(design, clock_layout);
  stage_profile.prepare_timing_s = phase_monitor.getElapsedSeconds();
  stage_profile.prepare_timing_cpu_s = phase_monitor.getCPUSeconds();
  if (!timing_status.ok()) {
    optimization_summary.success = false;
    optimization_summary.status = "failed";
    optimization_summary.reason = timing_status.message;
    CTSLOG.warn(Loc::current(), "Optimization: timing candidate preparation failed: ", timing_status.message, ".");
    return optimization_summary;
  }
  OptimizationTimingGuard timing_guard(CTSDM);

  if (master_infos.empty()) {
    if (master_collection.configured_candidate_count > 0U && master_collection.unavailable_candidate_count > 0U) {
      CTSLOG.warn(Loc::current(), "Optimization: configured sizing candidates are unavailable because required Liberty data is missing.");
      optimization_summary.success = false;
      optimization_summary.status = "failed";
      optimization_summary.reason = "sizing_candidate_data_unavailable";
      return optimization_summary;
    }
    CTSLOG.warn(Loc::current(), "Optimization: skip because no sizing candidates are configured.");
    optimization_summary.reason = "no_sizing_candidates_configured";
    Monitor commit_monitor;
    const auto commit_status = CTSDM.commitOptimization(std::move(local_design), std::move(clock_layout), optimization_summary);
    stage_profile.commit_and_publish_s = commit_monitor.getElapsedSeconds();
    stage_profile.commit_and_publish_cpu_s = commit_monitor.getCPUSeconds();
    if (!commit_status.ok()) {
      optimization_summary.success = false;
      optimization_summary.status = "failed";
      optimization_summary.reason = commit_status.message;
    }
    CTSLOG.info(Loc::current(), "Completed CTS optimization", monitor.getStatsInfo());
    return optimization_summary;
  }

  std::string no_op_reason = "no_optimizable_clock";
  {
    OptimizationClockWorkMonitor clock_work_monitor(stage_profile);
    for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
      Monitor clock_monitor;
      auto* clock = clocks.at(clock_index);
      if (clock == nullptr) {
        continue;
      }
      const double target_skew_ns = oi::ResolveClockTargetSkewNs(config);
      const auto pending_context = CTSDM.getOptimizationTimingContext(clock->get_clock_name(), clock->get_clock_net_name());
      if (!pending_context.has_value()) {
        optimization_summary.success = false;
        optimization_summary.status = "failed";
        optimization_summary.reason = "fast_sta_candidate_context_unavailable";
        CTSLOG.warn(Loc::current(), "Optimization: FastSTA context build failed for clock \"", clock->get_clock_name(), "\": ", optimization_summary.reason,
                    ".");
        return optimization_summary;
      }
      const auto context_id = *pending_context;
      auto outer_profile = oi::CaptureGraphProfile(fast_sta, context_id);

      // The DataManager candidate already owns validated layout/input RC. Sizing
      // trials must not replace it with an independently rebuilt FLUTE network.
      logFastStaTimingSummary(fast_sta, context_id, "initial_candidate", clock->get_clock_name());
      phase_monitor = Monitor{};
      const auto relation_seeds = fast_sta.collectTimingRelationSeeds(context_id);
      const auto separation = fast_sta.separateTimingRelations(context_id, FastStaSeparationQuery{});
      outer_profile.separate_timing_relations_s = phase_monitor.getElapsedSeconds();
      outer_profile.separate_timing_relations_cpu_s = phase_monitor.getCPUSeconds();
      if (!separation.complete) {
        optimization_summary.success = false;
        optimization_summary.status = "failed";
        optimization_summary.reason = separation.diagnostic.empty() ? "fast_sta_relation_separation_failed" : separation.diagnostic;
        CTSLOG.warn(Loc::current(), "Optimization: FastSTA relation separation failed for clock \"", clock->get_clock_name(),
                    "\": ", optimization_summary.reason, ".");
        return optimization_summary;
      }
      CTSLOG.info(Loc::current(), "FastSTA relation receipt: seeds=", relation_seeds.relation_indexes.size(),
                  ", separated_relations=", separation.evaluated_check_count, ", setup_hold_violations=", separation.violated.size(),
                  ", near_active=", separation.near_active.size(), ", separation_runtime_s=", separation.runtime_s, ", separation=complete.");

      phase_monitor = Monitor{};
      const auto sizing_context = CTSDM.buildClockSizingContext(design, clock_layout, clock_index);
      outer_profile.build_clock_sizing_context_s = phase_monitor.getElapsedSeconds();
      outer_profile.build_clock_sizing_context_cpu_s = phase_monitor.getCPUSeconds();
      if (!sizing_context.ok() || !sizing_context.context_id.has_value()) {
        optimization_summary.success = false;
        optimization_summary.status = "failed";
        optimization_summary.reason = "fast_sta_clock_sizing_context_unavailable";
        CTSLOG.warn(Loc::current(), "Optimization: clock sizing FastSTA context build failed for clock \"", clock->get_clock_name(),
                    "\": ", sizing_context.failure_reason, ".");
        return optimization_summary;
      }
      const auto sizing_context_id = *sizing_context.context_id;
      auto sizing_graph_profile = oi::CaptureGraphProfile(fast_sta, sizing_context_id);
      outer_profile.node_count = sizing_graph_profile.node_count;
      outer_profile.owned_clock_node_count = sizing_graph_profile.owned_clock_node_count;
      outer_profile.net_count = sizing_graph_profile.net_count;
      outer_profile.sink_count = sizing_graph_profile.sink_count;
      outer_profile.buffer_input_count = sizing_graph_profile.buffer_input_count;
      outer_profile.buffer_output_count = sizing_graph_profile.buffer_output_count;

      phase_monitor = Monitor{};
      auto buffers = oi::CollectClockSizingBuffers(design, fast_sta, sizing_context_id, master_infos);
      outer_profile.collect_optimizable_buffers_s = phase_monitor.getElapsedSeconds();
      outer_profile.collect_optimizable_buffers_cpu_s = phase_monitor.getCPUSeconds();
      outer_profile.optimizable_buffer_count = buffers.size();
      if (buffers.empty()) {
        (void) fast_sta.eraseContext(sizing_context_id);
        CTSLOG.warn(Loc::current(), "Optimization: skip clock \"", clock->get_clock_name(), "\" because no resizable buffers are available.");
        no_op_reason = "no_resizable_buffers";
        continue;
      }

      phase_monitor = Monitor{};
      const auto cap_baseline = oi::CollectClockSizingCapLimits(fast_sta, sizing_context_id);
      outer_profile.collect_cap_baseline_s = phase_monitor.getElapsedSeconds();
      outer_profile.collect_cap_baseline_cpu_s = phase_monitor.getCPUSeconds();
      phase_monitor = Monitor{};
      const auto slew_baseline = oi::CollectClockSizingSlewLimits(fast_sta, sizing_context_id);
      outer_profile.collect_slew_baseline_s = phase_monitor.getElapsedSeconds();
      outer_profile.collect_slew_baseline_cpu_s = phase_monitor.getCPUSeconds();
      phase_monitor = Monitor{};
      const bool use_scalable_solver = oi::ShouldUseScalableSolver(oi::ScalableSolverDecisionInput{
          .fast_sta = &fast_sta,
          .context_id = sizing_context_id,
          .buffers = &buffers,
      });
      auto summary = use_scalable_solver ? oi::SolveClockScalable(fast_sta, sizing_context_id, buffers, cap_baseline, slew_baseline, target_skew_ns)
                                         : oi::SolveClock(fast_sta, sizing_context_id, buffers, cap_baseline, slew_baseline, target_skew_ns);
      outer_profile.solve_clock_s = phase_monitor.getElapsedSeconds();
      outer_profile.solve_clock_cpu_s = phase_monitor.getCPUSeconds();
      oi::CopyOuterProfile(summary.profile, outer_profile);
      if (!summary.valid) {
        phase_monitor = Monitor{};
        (void) fast_sta.eraseContext(sizing_context_id);
        summary.profile.finalize_clock_context_s = phase_monitor.getElapsedSeconds();
        summary.profile.finalize_clock_context_cpu_s = phase_monitor.getCPUSeconds();
        summary.profile.clock_total_s = clock_monitor.getElapsedSeconds();
        summary.profile.clock_total_cpu_s = clock_monitor.getCPUSeconds();
        logClockSizingSummary(*clock, target_skew_ns, master_infos.size(), summary);
        CTSLOG.warn(Loc::current(), "Optimization: skip clock \"", clock->get_clock_name(), "\" because fast STA solver failed with reason ",
                    summary.stop_reason, ".");
        optimization_summary.success = false;
        optimization_summary.status = "failed";
        optimization_summary.reason = summary.stop_reason.empty() ? "solver_failed" : summary.stop_reason;
        return optimization_summary;
      }
      phase_monitor = Monitor{};
      if (const auto timing_summary = captureClockTimingSummary(*clock, fast_sta, sizing_context_id, summary, target_skew_ns); timing_summary.has_value()) {
        optimization_summary.clock_timing.push_back(*timing_summary);
      }
      (void) fast_sta.eraseContext(sizing_context_id);
      summary.profile.finalize_clock_context_s = phase_monitor.getElapsedSeconds();
      summary.profile.finalize_clock_context_cpu_s = phase_monitor.getCPUSeconds();
      phase_monitor = Monitor{};
      const bool edits_applied
          = summary.accepted_edits.empty() || oi::ApplyClockSizingAcceptedEdits(design, wrapper, summary.accepted_edits, buffers, clock_layout);
      summary.profile.apply_accepted_edits_s = phase_monitor.getElapsedSeconds();
      summary.profile.apply_accepted_edits_cpu_s = phase_monitor.getCPUSeconds();
      if (!edits_applied) {
        summary.profile.clock_total_s = clock_monitor.getElapsedSeconds();
        summary.profile.clock_total_cpu_s = clock_monitor.getCPUSeconds();
        logClockSizingSummary(*clock, target_skew_ns, master_infos.size(), summary);
        optimization_summary.success = false;
        optimization_summary.status = "failed";
        optimization_summary.reason = "accepted_edit_apply_failed";
        return optimization_summary;
      }
      if (!summary.accepted_edits.empty()) {
        // Sizing IDs belong to the local context. Stable instance identities
        // map accepted masters into each complete transaction and its logic cones.
        phase_monitor = Monitor{};
        std::vector<FastStaInstanceMasterChange> accepted_masters;
        for (const auto& edit : summary.accepted_edits) {
          const auto found = std::ranges::find_if(accepted_masters, [&](const auto& change) -> bool { return change.inst_name == edit.inst_name; });
          if (found == accepted_masters.end()) {
            accepted_masters.push_back({.inst_name = edit.inst_name, .cell_master = edit.to_master});
          } else {
            found->cell_master = edit.to_master;
          }
        }
        const auto refresh = CTSDM.updateOptimizationTiming(accepted_masters);
        summary.profile.refresh_timing_contexts_s = phase_monitor.getElapsedSeconds();
        summary.profile.refresh_timing_contexts_cpu_s = phase_monitor.getCPUSeconds();
        if (!refresh.ok()) {
          summary.profile.clock_total_s = clock_monitor.getElapsedSeconds();
          summary.profile.clock_total_cpu_s = clock_monitor.getCPUSeconds();
          logClockSizingSummary(*clock, target_skew_ns, master_infos.size(), summary);
          optimization_summary.success = false;
          optimization_summary.status = "failed";
          optimization_summary.reason = refresh.message;
          CTSLOG.warn(Loc::current(), "Optimization: all-clock timing refresh failed after accepted edits for clock \"", clock->get_clock_name(),
                      "\": ", refresh.message, ".");
          return optimization_summary;
        }
        CTSLOG.info(Loc::current(), "Optimization: refreshed all ", clocks.size(), " timing views after accepted edits for clock \"", clock->get_clock_name(),
                    "\", context_refresh_runtime_s=", summary.profile.refresh_timing_contexts_s,
                    ", context_refresh_cpu_s=", summary.profile.refresh_timing_contexts_cpu_s, ".");
      }
      summary.profile.clock_total_s = clock_monitor.getElapsedSeconds();
      summary.profile.clock_total_cpu_s = clock_monitor.getCPUSeconds();
      logClockSizingSummary(*clock, target_skew_ns, master_infos.size(), summary);
      if (summary.accepted_edits.empty() && !summary.stop_reason.empty() && (no_op_reason == "no_optimizable_clock" || no_op_reason == "target_met")) {
        no_op_reason = summary.stop_reason;
      }
      optimization_summary.optimized = optimization_summary.optimized || !summary.accepted_edits.empty();
      optimization_summary.optimized_clock_count += summary.accepted_edits.empty() ? 0U : 1U;
      optimization_summary.accepted_edit_count += summary.accepted_edit_count;
    }
  }

  LogTableRows result_rows;
  for (const auto& timing : optimization_summary.clock_timing) {
    result_rows.push_back({timing.clock, ToLogTableCell(timing.sink_count), ToLogTableCell(timing.initial_skew_ns), ToLogTableCell(timing.optimized_skew_ns),
                           ToLogTableCell(timing.target_skew_ns), ToLogTableCell(timing.target_met), ToLogTableCell(timing.min_insertion_latency_ns),
                           ToLogTableCell(timing.max_insertion_latency_ns), ToLogTableCell(timing.mean_insertion_latency_ns)});
  }
  if (result_rows.empty()) {
    result_rows.push_back({"none", "0", "n/a", "n/a", ToLogTableCell(oi::ResolveClockTargetSkewNs(config)), "false", "n/a", "n/a", "n/a"});
  }
  EmitLogTable(Loc::current(), "CTS Optimization Result",
               {"Clock", "Sinks", "Initial Skew", "Optimized Skew", "Target", "Target Met", "Min Latency", "Max Latency", "Mean Latency"}, result_rows);
  EmitLogTable(Loc::current(), "CTS Optimization Selection Summary", {"Metric", "Value"},
               {{"Accepted Sizing Edits", ToLogTableCell(optimization_summary.accepted_edit_count)},
                {"Optimized Clocks", ToLogTableCell(optimization_summary.optimized_clock_count)}});
  if (optimization_summary.optimized) {
    optimization_summary.status = "optimized";
  } else {
    optimization_summary.status = "no_op";
    optimization_summary.reason = no_op_reason;
  }
  phase_monitor = Monitor{};
  const auto commit_status = CTSDM.commitOptimization(std::move(local_design), std::move(clock_layout), optimization_summary);
  if (!commit_status.ok()) {
    optimization_summary.success = false;
    optimization_summary.status = "failed";
    optimization_summary.reason = commit_status.message;
    CTSLOG.warn(Loc::current(), "CTS optimization commit failed: ", commit_status.message);
  } else {
    // Each clock view contains the same full logic network. Publish its global
    // totals once, with the owning clock named for the scoped skew value.
    for (const auto* clock : CTSDM.getDesign().get_clocks()) {
      if (clock == nullptr) {
        continue;
      }
      const auto final_context = CTSDM.getClockTimingContext(clock->get_clock_name(), clock->get_clock_net_name());
      const auto final_status = final_context.has_value() ? fast_sta.queryAnalysisStatus(*final_context) : std::nullopt;
      if (!final_status.has_value() || !final_status->timing_valid) {
        CTSLOG.error(Loc::current(), "Optimization: committed timing invariant was violated for clock \"", clock->get_clock_name(), "\".");
      } else {
        logFastStaTimingSummary(fast_sta, *final_context, "final_committed", clock->get_clock_name());
      }
    }
  }
  stage_profile.commit_and_publish_s = phase_monitor.getElapsedSeconds();
  stage_profile.commit_and_publish_cpu_s = phase_monitor.getCPUSeconds();
  CTSLOG.info(Loc::current(), "Completed CTS optimization", monitor.getStatsInfo());
  return optimization_summary;
}

}  // namespace icts
