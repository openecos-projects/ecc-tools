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
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "Utility.hh"
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

class FastStaClockContextGuard
{
 public:
  FastStaClockContextGuard(FastSTA& fast_sta, FastStaClockId clock_id) : _fast_sta(&fast_sta), _clock_id(clock_id) {}
  ~FastStaClockContextGuard() { erase(); }

  FastStaClockContextGuard(const FastStaClockContextGuard&) = delete;
  auto operator=(const FastStaClockContextGuard&) -> FastStaClockContextGuard& = delete;

  FastStaClockContextGuard(FastStaClockContextGuard&& rhs) noexcept : _fast_sta(rhs._fast_sta), _clock_id(rhs._clock_id)
  {
    rhs._fast_sta = nullptr;
    rhs._clock_id = kInvalidFastStaClockId;
  }

  auto operator=(FastStaClockContextGuard&& rhs) noexcept -> FastStaClockContextGuard&
  {
    if (this == &rhs) {
      return *this;
    }
    erase();
    _fast_sta = rhs._fast_sta;
    _clock_id = rhs._clock_id;
    rhs._fast_sta = nullptr;
    rhs._clock_id = kInvalidFastStaClockId;
    return *this;
  }

  auto id() const -> FastStaClockId { return _clock_id; }

 private:
  auto erase() -> void
  {
    if (_fast_sta != nullptr && _clock_id != kInvalidFastStaClockId) {
      (void) _fast_sta->eraseClockContext(_clock_id);
      _clock_id = kInvalidFastStaClockId;
    }
  }

  FastSTA* _fast_sta = nullptr;
  FastStaClockId _clock_id = kInvalidFastStaClockId;
};

auto resolveRoutingLayer(const Config& config) -> std::optional<int>
{
  const auto& routing_layers = config.get_routing_layers();
  if (routing_layers.empty() || routing_layers.front() == 0U) {
    return std::nullopt;
  }
  return static_cast<int>(routing_layers.front());
}

auto resolveWireWidth(const Config& config) -> std::optional<double>
{
  const double wire_width_um = config.get_wire_width();
  return wire_width_um > 0.0 ? std::optional<double>{wire_width_um} : std::nullopt;
}

auto buildFastStaEnvironment(const Config& config, Wrapper& wrapper) -> std::optional<FastStaEnvironment>
{
  const auto dbu_per_um = wrapper.queryDbUnit();
  const auto routing_layer = resolveRoutingLayer(config);
  if (!dbu_per_um.has_value() || !routing_layer.has_value()) {
    return std::nullopt;
  }
  return FastStaEnvironment{
      .wrapper = &wrapper,
      .dbu_per_um = *dbu_per_um,
      .routing_layer = *routing_layer,
      .wire_width_um = resolveWireWidth(config),
      .root_input_slew_ns = std::max(0.0, config.get_root_input_slew()),
      .max_cap_pf = config.has_max_cap() && config.get_max_cap() > 0.0 ? std::optional<double>{config.get_max_cap()} : std::nullopt,
      .max_sink_tran_ns = config.get_max_sink_tran(),
  };
}

auto captureClockTimingSummary(const Clock& clock, const FastSTA& fast_sta, FastStaClockId clock_id, const oi::ClockSizingSummary& optimization,
                               double target_skew_ns) -> std::optional<ClockTimingSummary>
{
  const auto sink_arrivals = fast_sta.collectClockSinkArrivals(clock_id);
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

struct MasterTransitionAggregate
{
  std::size_t count = 0U;
  double area_delta_um2 = 0.0;
};

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
                {"Capacitance Violations", ToLogTableCell(summary.before.cap.violation_count), ToLogTableCell(summary.after.cap.violation_count)},
                {"Slew Violations", ToLogTableCell(summary.before.slew.violation_count), ToLogTableCell(summary.after.slew.violation_count)},
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

  const double stage_total_s = profile.build_route_tree_cache_s + profile.build_fast_sta_context_s + profile.inject_route_trees_s
                               + profile.collect_optimizable_buffers_s + profile.collect_cap_baseline_s + profile.collect_slew_baseline_s
                               + profile.solve_clock_s + profile.apply_accepted_edits_s;
  const double solver_detail_s = profile.capture_initial_state_s + profile.build_topology_index_s + profile.generate_batch_candidates_s
                                 + profile.batch_trial_eval_s + profile.apply_accepted_batch_s;
  EmitLogTable(Loc::current(), "CTS Optimization Runtime Profile", {"Stage", "Runtime (s)"},
               {{"Route Tree Cache", ToLogTableCell(profile.build_route_tree_cache_s)},
                {"FastSTA Context", ToLogTableCell(profile.build_fast_sta_context_s)},
                {"Route Injection", ToLogTableCell(profile.inject_route_trees_s)},
                {"Collect Buffers", ToLogTableCell(profile.collect_optimizable_buffers_s)},
                {"Collect Cap Baseline", ToLogTableCell(profile.collect_cap_baseline_s)},
                {"Collect Slew Baseline", ToLogTableCell(profile.collect_slew_baseline_s)},
                {"Solve", ToLogTableCell(profile.solve_clock_s)},
                {"Apply Edits", ToLogTableCell(profile.apply_accepted_edits_s)},
                {"Capture Initial State", ToLogTableCell(profile.capture_initial_state_s)},
                {"Topology Index", ToLogTableCell(profile.build_topology_index_s)},
                {"Candidate Generation", ToLogTableCell(profile.generate_batch_candidates_s)},
                {"Batch Trial Evaluation", ToLogTableCell(profile.batch_trial_eval_s)},
                {"Accepted Batch Apply", ToLogTableCell(profile.apply_accepted_batch_s)},
                {"Stage Total", ToLogTableCell(stage_total_s)},
                {"Solver Detail", ToLogTableCell(solver_detail_s)}});
}

}  // namespace

auto Optimization::run() -> OptimizationSummary
{
  Monitor monitor;
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
  const auto fast_sta_environment = buildFastStaEnvironment(config, wrapper);
  if (!fast_sta_environment.has_value()) {
    CTSLOG.warn(Loc::current(), "Optimization: FastSTA environment inputs are unavailable.");
    optimization_summary.success = false;
    optimization_summary.status = "failed";
    optimization_summary.reason = "fast_sta_environment_unavailable";
    return optimization_summary;
  }
  fast_sta.bindEnvironment(*fast_sta_environment);
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
    const auto commit_status = CTSDM.commitOptimization(std::move(local_design), std::move(clock_layout), optimization_summary);
    if (!commit_status.ok()) {
      optimization_summary.success = false;
      optimization_summary.status = "failed";
      optimization_summary.reason = commit_status.message;
    }
    CTSLOG.info(Loc::current(), "Completed CTS optimization", monitor.getStatsInfo());
    return optimization_summary;
  }

  auto stage_start = std::chrono::steady_clock::now();
  const auto route_tree_by_net = oi::BuildClockSizingRouteTrees(design, clocks);
  const double route_tree_cache_runtime_s = Utility::getElapsedSeconds(stage_start);

  std::string no_op_reason = "no_optimizable_clock";
  for (std::size_t clock_index = 0U; clock_index < clocks.size(); ++clock_index) {
    auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      continue;
    }
    const double target_skew_ns = oi::ResolveClockTargetSkewNs(config);
    oi::ClockSizingRuntimeProfile outer_profile;
    outer_profile.build_route_tree_cache_s = route_tree_cache_runtime_s;
    stage_start = std::chrono::steady_clock::now();
    const auto route_geometry = oi::BuildClockRouteGeometry(clock_layout, clock_index);
    const auto context_build = fast_sta.buildClockContext(FastStaClockBuildInput{
        .clock = clock,
        .route_geometry = &route_geometry,
    });
    if (!context_build.clock_id.has_value()) {
      optimization_summary.success = false;
      optimization_summary.status = "failed";
      optimization_summary.reason = context_build.failure_reason.empty() ? "fast_sta_context_input_unavailable" : context_build.failure_reason;
      CTSLOG.warn(Loc::current(), "Optimization: FastSTA context build failed for clock \"", clock->get_clock_name(), "\": ", optimization_summary.reason, ".");
      return optimization_summary;
    }
    FastStaClockContextGuard clock_context(fast_sta, context_build.clock_id.value());
    const auto clock_id = clock_context.id();
    outer_profile.build_fast_sta_context_s = Utility::getElapsedSeconds(stage_start);

    auto graph_profile = oi::CaptureGraphProfile(fast_sta, clock_id);
    graph_profile.build_route_tree_cache_s = outer_profile.build_route_tree_cache_s;
    graph_profile.build_fast_sta_context_s = outer_profile.build_fast_sta_context_s;
    outer_profile = graph_profile;

    stage_start = std::chrono::steady_clock::now();
    if (!oi::InjectRouteTrees(design, fast_sta, clock_id, *clock, route_tree_by_net)) {
      outer_profile.inject_route_trees_s = Utility::getElapsedSeconds(stage_start);
      CTSLOG.warn(Loc::current(), "Optimization: skip clock \"", clock->get_clock_name(), "\" because fast STA context build failed.");
      no_op_reason = "fast_sta_context_failed";
      continue;
    }
    outer_profile.inject_route_trees_s = Utility::getElapsedSeconds(stage_start);

    stage_start = std::chrono::steady_clock::now();
    auto buffers = oi::CollectClockSizingBuffers(design, fast_sta, clock_id, master_infos);
    outer_profile.collect_optimizable_buffers_s = Utility::getElapsedSeconds(stage_start);
    outer_profile.optimizable_buffer_count = buffers.size();
    if (buffers.empty()) {
      CTSLOG.warn(Loc::current(), "Optimization: skip clock \"", clock->get_clock_name(), "\" because no resizable buffers are available.");
      no_op_reason = "no_resizable_buffers";
      continue;
    }

    stage_start = std::chrono::steady_clock::now();
    const auto cap_baseline = oi::CollectClockSizingCapLimits(fast_sta, clock_id);
    outer_profile.collect_cap_baseline_s = Utility::getElapsedSeconds(stage_start);
    stage_start = std::chrono::steady_clock::now();
    const auto slew_baseline = oi::CollectClockSizingSlewLimits(fast_sta, clock_id);
    outer_profile.collect_slew_baseline_s = Utility::getElapsedSeconds(stage_start);
    stage_start = std::chrono::steady_clock::now();
    const bool use_scalable_solver = oi::ShouldUseScalableSolver(oi::ScalableSolverDecisionInput{
        .fast_sta = &fast_sta,
        .clock_id = clock_id,
        .buffers = &buffers,
    });
    auto summary = use_scalable_solver ? oi::SolveClockScalable(fast_sta, clock_id, buffers, cap_baseline, slew_baseline, target_skew_ns)
                                       : oi::SolveClock(fast_sta, clock_id, buffers, cap_baseline, slew_baseline, target_skew_ns);
    outer_profile.solve_clock_s = Utility::getElapsedSeconds(stage_start);
    oi::CopyOuterProfile(summary.profile, outer_profile);
    if (!summary.valid) {
      logClockSizingSummary(*clock, target_skew_ns, master_infos.size(), summary);
      CTSLOG.warn(Loc::current(), "Optimization: skip clock \"", clock->get_clock_name(), "\" because fast STA solver failed with reason ", summary.stop_reason,
                  ".");
      no_op_reason = summary.stop_reason.empty() ? "solver_failed" : summary.stop_reason;
      continue;
    }
    if (const auto timing_summary = captureClockTimingSummary(*clock, fast_sta, clock_id, summary, target_skew_ns); timing_summary.has_value()) {
      optimization_summary.clock_timing.push_back(*timing_summary);
    }
    stage_start = std::chrono::steady_clock::now();
    if (!summary.accepted_edits.empty() && !oi::ApplyClockSizingAcceptedEdits(design, wrapper, summary.accepted_edits, buffers, clock_layout)) {
      summary.profile.apply_accepted_edits_s = Utility::getElapsedSeconds(stage_start);
      logClockSizingSummary(*clock, target_skew_ns, master_infos.size(), summary);
      optimization_summary.success = false;
      optimization_summary.status = "failed";
      optimization_summary.reason = "accepted_edit_apply_failed";
      return optimization_summary;
    }
    summary.profile.apply_accepted_edits_s = Utility::getElapsedSeconds(stage_start);
    logClockSizingSummary(*clock, target_skew_ns, master_infos.size(), summary);
    if (summary.accepted_edits.empty() && !summary.stop_reason.empty() && (no_op_reason == "no_optimizable_clock" || no_op_reason == "target_met")) {
      no_op_reason = summary.stop_reason;
    }
    optimization_summary.optimized = optimization_summary.optimized || !summary.accepted_edits.empty();
    optimization_summary.optimized_clock_count += summary.accepted_edits.empty() ? 0U : 1U;
    optimization_summary.accepted_edit_count += summary.accepted_edit_count;
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
  const auto commit_status = CTSDM.commitOptimization(std::move(local_design), std::move(clock_layout), optimization_summary);
  if (!commit_status.ok()) {
    optimization_summary.success = false;
    optimization_summary.status = "failed";
    optimization_summary.reason = commit_status.message;
    CTSLOG.warn(Loc::current(), "CTS optimization commit failed: ", commit_status.message);
  }
  CTSLOG.info(Loc::current(), "Completed CTS optimization", monitor.getStatsInfo());
  return optimization_summary;
}

}  // namespace icts
