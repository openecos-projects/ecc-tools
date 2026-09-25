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
 * @file DataManager.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief CTS process-wide state ownership and stage coordination.
 */

#include "DataManager.hh"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "adapter/sdc/SDCClockReader.hh"
#include "builder.h"
#include "design/Clock.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "idm.h"
#include "routing/ClockRouteSegmentRC.hh"

namespace icts {
namespace {

auto ensureDirectory(const std::filesystem::path& directory, std::string& error) -> bool
{
  std::error_code error_code;
  std::filesystem::create_directories(directory, error_code);
  if (!error_code) {
    return true;
  }
  error = "failed to create " + directory.string() + ": " + error_code.message();
  return false;
}

auto clockKindName(SdcClockDecl::Kind kind) -> const char*
{
  return kind == SdcClockDecl::Kind::kGenerated ? "generated" : "primary";
}

auto hasTimingConstraints(const SdcClockData& constraints) -> bool
{
  return !constraints.clocks.empty() || !constraints.case_analyses.empty() || !constraints.clock_transitions.empty() || !constraints.propagated_clocks.empty();
}

auto countTraceStatus(const ClockTraceSummary& summary, std::string_view status) -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(summary.records, [status](const ClockTraceRecord& record) -> bool { return record.status == status; }));
}

auto logRuntimeConfiguration(const Config& config, const Wrapper& wrapper, const std::string& config_file) -> void
{
  const auto& routing_layers = config.get_routing_layers();
  const auto routing_layer = routing_layers.empty() ? 0U : routing_layers.front();
  const auto dbu_per_um = wrapper.queryDbUnit();
  std::optional<ClockRouteSegmentRc> wire_rc = std::nullopt;
  const bool wire_rc_available = routing_layer > 0U && wrapper.is_layout_ready() && dbu_per_um.has_value();
  if (wire_rc_available) {
    wire_rc = wrapper.queryConfiguredClockRouteSegmentRc(config);
  }
  EmitLogTable(Loc::current(), "CTS Runtime Paths", {"Path", "Value"},
               {{"Configuration", config_file},
                {"Work Directory", config.get_work_dir()},
                {"Log File", config.get_log_file()},
                {"Statistics Directory", config.get_statistics_dir()},
                {"Visualization Directory", config.get_visualization_dir()}});

  EmitLogTable(Loc::current(), "Runtime Configuration", {"Option", "Value"},
               {{"Skew Bound (ns)", ToLogTableCell(config.get_skew_bound())},
                {"Maximum Fanout", ToLogTableCell(config.get_max_fanout())},
                {"Root Input Slew (ns)", ToLogTableCell(config.get_root_input_slew())},
                {"Maximum Sink Transition (ns)", ToLogTableCell(config.get_max_sink_tran())},
                {"Maximum Buffer Transition Configured", ToLogTableCell(config.has_max_buf_tran())},
                {"Maximum Buffer Transition (ns)", ToLogTableCell(config.get_max_buf_tran())},
                {"Maximum Capacitance Configured", ToLogTableCell(config.has_max_cap())},
                {"Maximum Capacitance (pF)", ToLogTableCell(config.get_max_cap())},
                {"Routing Layer", ToLogTableCell(routing_layer)},
                {"Routing Layer Count", ToLogTableCell(routing_layers.size())},
                {"Wire Width (um)", ToLogTableCell(config.get_wire_width())},
                {"Buffer Master Count", ToLogTableCell(config.get_buffer_types().size())},
                {"Sink Clustering", ToLogTableCell(config.is_enable_sink_clustering())},
                {"Analytical HTree", ToLogTableCell(config.is_enable_analytical_htree())}});

  EmitLogTable(Loc::current(), "Characterization Configuration", {"Option", "Value"},
               {{"Wirelength Unit (um)", ToLogTableCell(config.get_wirelength_unit_um())},
                {"Wirelength Iterations", ToLogTableCell(config.get_wirelength_iterations())},
                {"Slew Steps", ToLogTableCell(config.get_slew_steps())},
                {"Capacitance Steps", ToLogTableCell(config.get_cap_steps())},
                {"Buffer Redundancy (%)", ToLogTableCell(config.get_char_buf_redundancy_pct())},
                {"Force Branch Buffer", ToLogTableCell(config.is_force_branch_buffer())},
                {"HTree Topology Tolerance", ToLogTableCell(config.get_htree_topology_tolerance())}});

  EmitLogTable(Loc::current(), "Runtime Routing / Wire RC", {"Property", "Value"},
               {{"Routing Setup Source", "Runtime Configuration"},
                {"Routing Layer", ToLogTableCell(routing_layer)},
                {"Query Length (um)", "1"},
                {"DBU per um", wire_rc.has_value() ? ToLogTableCell(wire_rc->dbu_per_um) : "n/a"},
                {"Unit Resistance (ohm/um)", wire_rc.has_value() ? ToLogTableCell(wire_rc->resistance_per_um_ohm) : "n/a"},
                {"Unit Capacitance (pF/um)", wire_rc.has_value() ? ToLogTableCell(wire_rc->capacitance_per_um_pf) : "n/a"},
                {"Status", wire_rc_available ? "available" : "unavailable"}});
}

auto logClockTraceSummary(const SdcClockData& clock_data, const ClockTraceBuild& trace) -> void
{
  const auto accepted_count = countTraceStatus(trace.summary, "accepted");
  const auto trace_stop_count = countTraceStatus(trace.summary, "trace_stop");
  const auto ambiguous_count = countTraceStatus(trace.summary, "ambiguous");
  const auto rejected_count = countTraceStatus(trace.summary, "rejected");
  const auto skipped_count = countTraceStatus(trace.summary, "skipped");
  const auto known_count = accepted_count + trace_stop_count + ambiguous_count + rejected_count + skipped_count;
  const auto other_count = trace.summary.records.size() >= known_count ? trace.summary.records.size() - known_count : 0U;
  EmitLogTable(Loc::current(), "Clock Trace Overview", {"Metric", "Count"},
               {{"Clock Declarations", ToLogTableCell(clock_data.clocks.size())},
                {"Case Analysis Records", ToLogTableCell(clock_data.case_analyses.size())},
                {"Trace Records", ToLogTableCell(trace.summary.records.size())},
                {"Accepted Records", ToLogTableCell(accepted_count)},
                {"Accepted Target Nets", ToLogTableCell(trace.output.clock_targets.size())},
                {"Trace Stop Records", ToLogTableCell(trace_stop_count)},
                {"Ambiguous Records", ToLogTableCell(ambiguous_count)},
                {"Rejected Records", ToLogTableCell(rejected_count)},
                {"Skipped Records", ToLogTableCell(skipped_count)},
                {"Other Records", ToLogTableCell(other_count)},
                {"Unowned Clock-like Nets", ToLogTableCell(trace.summary.unowned_clock_like_records.size())}});

  LogTableRows ownership_rows;
  for (const auto& declaration : clock_data.clocks) {
    const auto record_count = static_cast<std::size_t>(
        std::ranges::count_if(trace.summary.records, [&declaration](const auto& record) -> bool { return record.clock_name == declaration.clock_name; }));
    const auto clock_accepted_count = static_cast<std::size_t>(std::ranges::count_if(trace.summary.records, [&declaration](const auto& record) -> bool {
      return record.clock_name == declaration.clock_name && record.status == "accepted";
    }));
    const auto target_count = static_cast<std::size_t>(
        std::ranges::count_if(trace.output.clock_targets, [&declaration](const auto& target) -> bool { return target.clock_name == declaration.clock_name; }));
    ownership_rows.push_back({declaration.clock_name, clockKindName(declaration.kind),
                              declaration.master_clock_name.empty() ? "n/a" : declaration.master_clock_name, ToLogTableCell(declaration.period_ns),
                              ToLogTableCell(declaration.period_resolved), ToLogTableCell(declaration.targets.size()),
                              ToLogTableCell(declaration.generated_sources.size()), ToLogTableCell(record_count), ToLogTableCell(clock_accepted_count),
                              ToLogTableCell(target_count), ToLogTableCell(declaration.is_virtual)});
  }
  EmitLogTable(Loc::current(), "SDC Clock Ownership Overview",
               {"Clock", "Kind", "Master", "Period (ns)", "Resolved", "Targets", "Sources", "Trace", "Accepted", "Nets", "Virtual"}, ownership_rows);
}

auto logDesignDistribution(const Design& design) -> void
{
  std::map<InstType, std::size_t> inst_type_counts;
  for (const auto* inst : design.get_insts()) {
    if (inst != nullptr) {
      ++inst_type_counts[inst->get_type()];
    }
  }
  EmitLogTable(Loc::current(), "CTS Inst Classification Summary", {"Class", "Count"},
               {{"Total", ToLogTableCell(design.get_insts().size())},
                {"Buffer", ToLogTableCell(inst_type_counts[InstType::kBuffer])},
                {"Inverter", ToLogTableCell(inst_type_counts[InstType::kInverter])},
                {"Flip-flop", ToLogTableCell(inst_type_counts[InstType::kFlipFlop])},
                {"Latch", ToLogTableCell(inst_type_counts[InstType::kLatch])},
                {"Clock Gate", ToLogTableCell(inst_type_counts[InstType::kClockGate])},
                {"Mux", ToLogTableCell(inst_type_counts[InstType::kMux])},
                {"Clock Logic", ToLogTableCell(inst_type_counts[InstType::kClockLogic])},
                {"Boundary Load", ToLogTableCell(inst_type_counts[InstType::kBoundaryLoad])},
                {"Macro Block", ToLogTableCell(inst_type_counts[InstType::kMacroBlock])},
                {"Unknown", ToLogTableCell(inst_type_counts[InstType::kUnknown])}});

  std::size_t total_sinks = 0U;
  LogTableRows distribution_rows;
  for (const auto* clock : design.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    std::map<InstType, std::size_t> sink_type_counts;
    std::size_t io_sinks = 0U;
    for (const auto* load : clock->get_loads()) {
      if (load == nullptr || load->get_inst() == nullptr) {
        ++io_sinks;
      } else {
        ++sink_type_counts[load->get_inst()->get_type()];
      }
    }
    total_sinks += clock->get_loads().size();
    const auto sequential_sinks = sink_type_counts[InstType::kFlipFlop] + sink_type_counts[InstType::kLatch];
    const auto physical_propagation_boundaries = static_cast<std::size_t>(std::ranges::count_if(clock->get_loads(), [clock](const Pin* load) -> bool {
      const auto* inst = load == nullptr ? nullptr : load->get_inst();
      return inst != nullptr && (inst->is_buffer() || inst->is_inverter()) && clock->findPropagationArc(inst) == nullptr;
    }));
    const auto boundary_sinks = sink_type_counts[InstType::kClockGate] + sink_type_counts[InstType::kMux] + sink_type_counts[InstType::kClockLogic]
                                + sink_type_counts[InstType::kBoundaryLoad] + physical_propagation_boundaries;
    const auto propagation_sinks = clock->get_propagation_arcs().size();
    distribution_rows.push_back({clock->get_clock_name(), clock->get_clock_net_name(), ToLogTableCell(clock->get_clock_period_ns()),
                                 clock->get_clock_period_source(), ToLogTableCell(clock->get_nets().size()), ToLogTableCell(clock->get_insts().size()),
                                 ToLogTableCell(clock->get_loads().size()), ToLogTableCell(sequential_sinks),
                                 ToLogTableCell(sink_type_counts[InstType::kMacroBlock]), ToLogTableCell(boundary_sinks), ToLogTableCell(propagation_sinks),
                                 ToLogTableCell(io_sinks), ToLogTableCell(clock->is_preclustered_sink_reuse()),
                                 ToLogTableCell(clock->get_preclustered_anchor_input_net_names().size())});
  }
  distribution_rows.push_back({"TOTAL", "-", "-", "-", ToLogTableCell(design.get_nets().size()), ToLogTableCell(design.get_insts().size()),
                               ToLogTableCell(total_sinks), "-", "-", "-", "-", "-", "-", "-"});
  EmitLogTable(Loc::current(), "Clock Distribution Overview",
               {"Clock", "Net", "Period", "Source", "Nets", "Insts", "Sinks", "Seq", "Macro", "Boundary", "Propagation", "IO", "Reuse", "Anchors"},
               distribution_rows);
}

}  // namespace

std::unique_ptr<DataManager> DataManager::_instance;

DataManager::DataManager([[maybe_unused]] ConstructionKey construction_key) : _design(std::make_unique<Design>())
{
}

void DataManager::initInst()
{
  if (_instance == nullptr) {
    _instance = std::make_unique<DataManager>(ConstructionKey{});
  }
}

auto DataManager::getInst() -> DataManager&
{
  if (_instance == nullptr) {
    CTSLOG.error(Loc::current(), "The CTS DataManager instance is not initialized.");
  }
  return *_instance;
}

void DataManager::destroyInst()
{
  _instance = nullptr;
}

auto DataManager::okStatus(std::string message) -> DataManagerStatus
{
  return DataManagerStatus{.code = DataManagerStatusCode::kOk, .message = std::move(message), .diagnostics = {}, .graph_issues = {}};
}

auto DataManager::failureStatus(DataManagerStatusCode code, std::string message) -> DataManagerStatus
{
  return DataManagerStatus{.code = code, .message = std::move(message), .diagnostics = {}, .graph_issues = {}};
}

auto DataManager::makeClockGraphFailureStatus(DataManagerStatusCode code, std::string message, const ClockDAG& clock_dag) -> DataManagerStatus
{
  DataManagerStatus status{.code = code, .message = std::move(message), .diagnostics = {}, .graph_issues = clock_dag.get_issues()};
  status.diagnostics.reserve(status.graph_issues.size());
  for (const auto& issue : status.graph_issues) {
    status.diagnostics.push_back(FormatClockGraphIssue(issue));
  }
  if (!status.diagnostics.empty()) {
    status.message += ": " + status.diagnostics.front();
  }
  return status;
}

auto DataManager::input(const DataManagerInput& input_data) -> DataManagerStatus
{
  Monitor monitor;
  CTSLOG.info(Loc::current(), "Starting CTS data input...");
  reset();

  const bool config_loaded = _config.init(input_data.config_file);
  const auto work_dir = std::filesystem::path(input_data.work_dir.empty() ? _config.get_work_dir() : input_data.work_dir);
  _config.set_work_dir(work_dir.string());
  _config.set_log_file((work_dir / "cts.log").string());
  _config.set_visualization_dir((work_dir / "visualization").string());
  _config.set_statistics_dir((work_dir / "statistics").string());

  std::string directory_error;
  if (!ensureDirectory(work_dir, directory_error) || !ensureDirectory(_config.get_visualization_dir(), directory_error)
      || !ensureDirectory(_config.get_statistics_dir(), directory_error)) {
    _state = CTSRunState::kFailed;
    CTSLOG.warn(Loc::current(), "CTS data input failed: ", directory_error);
    auto status = failureStatus(DataManagerStatusCode::kConfigError, directory_error);
    status.diagnostics = _config.get_warnings();
    return status;
  }
  CTSLOG.openLogFileStream(_config.get_log_file());

  if (!config_loaded) {
    _state = CTSRunState::kFailed;
    CTSLOG.warn(Loc::current(), "CTS data input failed for config file ", input_data.config_file, ": ", _config.get_last_error());
    auto status = failureStatus(DataManagerStatusCode::kConfigError, _config.get_last_error());
    status.diagnostics = _config.get_warnings();
    return status;
  }

  auto* idb_builder = dmInst->get_idb_builder();
  if (idb_builder == nullptr) {
    _state = CTSRunState::kFailed;
    CTSLOG.warn(Loc::current(), "CTS data input failed because the iDB builder is unavailable.");
    return failureStatus(DataManagerStatusCode::kExternalDataError, "iDB builder is unavailable.");
  }
  _wrapper.init(idb_builder);
  logRuntimeConfiguration(_config, _wrapper, input_data.config_file);

  auto status = readClockData();
  if (status.ok()) {
    status = initializeTiming();
  }
  const auto& config_warnings = _config.get_warnings();
  status.diagnostics.insert(status.diagnostics.end(), config_warnings.begin(), config_warnings.end());
  if (!status.ok()) {
    discardOptimizationTiming();
    releaseTimingContexts(_timing_contexts);
    _fast_sta.reset();
    _timing_graph.reset();
    _timing_constraints = {};
    (*_design).reset();
    _wrapper.clearCtsBindings();
    _state = CTSRunState::kFailed;
    return status;
  }

  _state = CTSRunState::kInputReady;
  CTSLOG.info(Loc::current(), "CTS data input completed with ", _design->get_clocks().size(), " clock(s)", monitor.getStatsInfo());
  return status;
}

auto DataManager::readClockData() -> DataManagerStatus
{
  const auto sdc_units = _wrapper.querySdcUnits().value_or(SdcUnits{.time_unit_ns = 0.0, .capacitance_unit_pf = 0.0});
  SdcClockData sdc_clock_data;
  const auto sdc_path = dmInst->get_config().get_sdc_path();
  if (!sdc_path.empty()) {
    sdc_clock_data = SdcClockReader(sdc_path, sdc_units).readClockData();
    if (!sdc_clock_data.ok()) {
      auto status = failureStatus(DataManagerStatusCode::kExternalDataError, "sdc_constraints_rejected");
      status.diagnostics = std::move(sdc_clock_data.diagnostics);
      if (!status.diagnostics.empty()) {
        status.message += ": " + status.diagnostics.front();
      }
      return status;
    }
  }
  std::set<std::string> traceable_clock_names;
  std::map<std::string, double> period_by_clock;
  std::map<std::string, bool> period_resolved_by_clock;
  for (const auto& clock_decl : sdc_clock_data.clocks) {
    if (clock_decl.clock_name.empty()) {
      continue;
    }
    if (!clock_decl.is_virtual) {
      traceable_clock_names.insert(clock_decl.clock_name);
    }
    period_by_clock[clock_decl.clock_name] = clock_decl.period_ns;
    period_resolved_by_clock[clock_decl.clock_name] = clock_decl.period_resolved;
  }

  std::vector<ClockTraceClockTarget> clock_targets;
  ClockTraceBuild trace;
  if (!sdc_clock_data.clocks.empty()) {
    trace = _wrapper.traceSdcClocks(SdcClockTraceInput{
        .clock_data = &sdc_clock_data,
        .max_fanout = _config.get_max_fanout(),
    });
    logClockTraceSummary(sdc_clock_data, trace);
    if (!trace.ok()) {
      CTSLOG.warn(Loc::current(), "CTS data input rejected SDC clock ownership: ", trace.message);
      return failureStatus(DataManagerStatusCode::kExternalDataError, trace.message);
    }
    clock_targets = trace.output.clock_targets;
    std::set<std::string> accepted_clock_names;
    for (const auto& target : clock_targets) {
      accepted_clock_names.insert(target.clock_name);
    }
    for (const auto& clock_name : traceable_clock_names) {
      if (!accepted_clock_names.contains(clock_name)) {
        CTSLOG.warn(Loc::current(), "CTS data input found no target net for SDC clock \"", clock_name, "\".");
        return failureStatus(DataManagerStatusCode::kExternalDataError, "clock_trace_no_targets");
      }
    }
  } else {
    logClockTraceSummary(sdc_clock_data, trace);
  }

  if (!clock_targets.empty() && !_wrapper.readTraceClockTargets(*_design, clock_targets)) {
    return failureStatus(DataManagerStatusCode::kExternalDataError, "clock_materialization_failed");
  }

  if (!_design->rebuildClockDAG()) {
    return makeClockGraphFailureStatus(DataManagerStatusCode::kExternalDataError, "clock_input_graph_invalid", _design->get_clock_dag());
  }

  for (auto* clock : _design->get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    const auto period_iter = period_by_clock.find(clock->get_clock_name());
    const auto resolved_iter = period_resolved_by_clock.find(clock->get_clock_name());
    const bool period_resolved = resolved_iter == period_resolved_by_clock.end() || resolved_iter->second;
    if (period_iter != period_by_clock.end() && period_iter->second > 0.0 && period_resolved) {
      clock->set_clock_period_ns(period_iter->second);
      clock->set_clock_period_source("sdc");
    }
  }
  logDesignDistribution(*_design);
  _timing_constraints = std::move(sdc_clock_data);
  return okStatus("CTS input data is ready.");
}

auto DataManager::initializeTiming() -> DataManagerStatus
{
  // An explicitly empty SDC is a valid no-clock session, not an unavailable model.
  if (!hasTimingConstraints(_timing_constraints)) {
    return okStatus("CTS input has no timing constraints.");
  }
  const auto dbu_per_um = _wrapper.queryDbUnit();
  const auto& routing_layers = _config.get_routing_layers();
  const auto root_slew_ns = _config.get_root_input_slew();
  const auto max_sink_tran_ns = _config.get_max_sink_tran();
  if (!dbu_per_um.has_value() || *dbu_per_um <= 0 || routing_layers.empty() || routing_layers.front() == 0U
      || routing_layers.front() > static_cast<unsigned>(std::numeric_limits<int>::max()) || !std::isfinite(root_slew_ns) || root_slew_ns < 0.0
      || !std::isfinite(max_sink_tran_ns) || max_sink_tran_ns < 0.0
      || (_config.has_max_cap() && (!std::isfinite(_config.get_max_cap()) || _config.get_max_cap() <= 0.0))) {
    return failureStatus(DataManagerStatusCode::kExternalDataError, "fast_sta_environment_unavailable");
  }
  _fast_sta.bindEnvironment(FastStaEnvironment{
      .wrapper = &_wrapper,
      .dbu_per_um = *dbu_per_um,
      .routing_layer = static_cast<int>(routing_layers.front()),
      .wire_width_um = _config.get_wire_width() > 0.0 ? std::optional<double>{_config.get_wire_width()} : std::nullopt,
      .root_input_slew_ns = root_slew_ns,
      .max_cap_pf = _config.has_max_cap() ? std::optional<double>{_config.get_max_cap()} : std::nullopt,
      .max_sink_tran_ns = max_sink_tran_ns,
      .worker_count = Wrapper::queryParallelWorkerCount(),
  });
  Monitor graph_monitor;
  _timing_graph = _wrapper.collectTimingGraph();
  const double graph_wall_s = graph_monitor.getElapsedSeconds();
  const double graph_cpu_s = graph_monitor.getCPUSeconds();
  if (!_timing_graph->complete()) {
    return failureStatus(DataManagerStatusCode::kExternalDataError, "fast_sta_graph_unavailable:" + _timing_graph->diagnostic);
  }
  const auto& graph = *_timing_graph;
  CTSLOG.info(Loc::current(), "FastSTA timing graph: nodes=", graph.nodes.size(), ", nets=", graph.nets.size(), ", arcs=", graph.arcs.size(),
              ", launches=", graph.launches.size(), ", checks=", graph.checks.size(), ", graph_construction_runtime_s=", graph.graph_construction_runtime_s,
              ", logic_rc_construction_runtime_s=", graph.logic_rc_construction_runtime_s, ", snapshot_wall_s=", graph_wall_s, ", snapshot_cpu_s=", graph_cpu_s,
              ", phase=pre_synthesis.");
  if (graph.signal_routing_authority.complete() && graph.signal_routing_authority.horizontal.has_value()
      && graph.signal_routing_authority.vertical.has_value()) {
    const auto& horizontal = *graph.signal_routing_authority.horizontal;
    const auto& vertical = *graph.signal_routing_authority.vertical;
    CTSLOG.info(Loc::current(), "FastSTA signal routing RC: layers[h/v]=", horizontal.name, "/", vertical.name,
                ", unit_r_ohm_per_um[h/v]=", horizontal.resistance_ohm_per_um, "/", vertical.resistance_ohm_per_um,
                ", unit_c_pf_per_um[h/v]=", horizontal.capacitance_pf_per_um, "/", vertical.capacitance_pf_per_um, ".");
  }
  const auto status = buildTimingContexts(*_design, _clock_layout, true, _timing_contexts);
  if (!status.ok()) {
    releaseTimingContexts(_timing_contexts);
    return failureStatus(DataManagerStatusCode::kExternalDataError, "fast_sta_initial_timing_failed:" + status.message);
  }
  return okStatus("FastSTA initial complete timing and power are ready.");
}

auto DataManager::buildTimingContexts(const Design& design, const ClockLayout& clock_layout, bool require_power, TimingContextSet& contexts)
    -> DataManagerStatus
{
  if (!_timing_graph.has_value()) {
    return design.get_clocks().empty() && !hasTimingConstraints(_timing_constraints)
               ? okStatus("CTS has no timing contexts to build.")
               : failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_input_snapshot_unavailable");
  }
  TimingContextSet candidate;
  auto build_one = [&](const Clock* clock, const FastStaClockRouteGeometry* geometry, const std::string& name) -> DataManagerStatus {
    Monitor build_monitor;
    const auto result = _fast_sta.buildContext(FastStaBuildInput{
        .clock = clock,
        .route_geometry = geometry,
        .timing_graph = &*_timing_graph,
        .constraints = &_timing_constraints,
        .require_power = require_power,
        .committed_design = &design,
        .committed_layout = &clock_layout,
        .propagate_all_clocks = true,
    });
    CTSLOG.info(Loc::current(), "FastSTA complete context build: clock=", name.empty() ? "unclocked" : name, ", require_power=", require_power,
                ", wall_s=", build_monitor.getElapsedSeconds(), ", cpu_s=", build_monitor.getCPUSeconds(), ", success=", result.ok(), ".");
    if (!result.ok()) {
      releaseTimingContexts(candidate);
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_candidate_rejected:" + name + ":" + result.failure_reason);
    }
    if (name.empty()) {
      candidate.unclocked = result.context_id;
    } else {
      candidate.clocks.emplace(std::pair{name, clock == nullptr ? std::string{} : clock->get_clock_net_name()}, *result.context_id);
    }
    return okStatus("CTS timing candidate is valid.");
  };
  const auto& clocks = design.get_clocks();
  for (std::size_t index = 0; index < clocks.size(); ++index) {
    const auto* clock = clocks.at(index);
    if (clock == nullptr || clock->get_clock_name().empty() || candidate.clocks.contains(std::pair{clock->get_clock_name(), clock->get_clock_net_name()})) {
      releaseTimingContexts(candidate);
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_clock_identity_invalid");
    }
    const auto geometry = FastSTA::collectClockRouteGeometry(clock_layout, index);
    if (!geometry.clock_nets.empty() && (geometry.design_dbu_per_um <= 0 || geometry.design_dbu_per_um != _wrapper.queryDbUnit().value_or(0))) {
      releaseTimingContexts(candidate);
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_route_geometry_dbu_mismatch");
    }
    auto status = build_one(clock, geometry.clock_nets.empty() ? nullptr : &geometry, clock->get_clock_name());
    if (!status.ok()) {
      return status;
    }
  }
  for (const auto& declaration : _timing_constraints.clocks) {
    if (!std::ranges::any_of(candidate.clocks, [&](const auto& entry) -> bool { return entry.first.first == declaration.clock_name; })) {
      if (!declaration.is_virtual) {
        releaseTimingContexts(candidate);
        return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_clock_missing:" + declaration.clock_name);
      }
      auto status = build_one(nullptr, nullptr, declaration.clock_name);
      if (!status.ok()) {
        return status;
      }
    }
  }
  if (candidate.clocks.empty()) {
    auto status = build_one(nullptr, nullptr, {});
    if (!status.ok()) {
      return status;
    }
  }
  contexts = std::move(candidate);
  return okStatus("CTS timing candidates are valid.");
}

auto DataManager::synchronizeTimingContexts(const Design& design, const ClockLayout& clock_layout, TimingContextSet& contexts) -> DataManagerStatus
{
  if (!_timing_graph.has_value()) {
    return design.get_clocks().empty() && !hasTimingConstraints(_timing_constraints)
               ? okStatus("CTS has no timing contexts to synchronize.")
               : failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_initial_context_unavailable");
  }
  for (const auto* clock : design.get_clocks()) {
    if (clock == nullptr || !_timing_contexts.clocks.contains({clock->get_clock_name(), clock->get_clock_net_name()})) {
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_synthesis_clock_identity_changed");
    }
  }
  TimingContextSet candidate;
  const auto synchronize = [&](FastStaContextId committed, const Clock* owner) -> FastStaBuildResult {
    auto pending = _fast_sta.beginContextTransaction(committed);
    if (!pending.context_id.has_value()) {
      return pending;
    }
    const auto result = _fast_sta.synchronizeClockContext(*pending.context_id, {.clock = owner,
                                                                                .constraints = &_timing_constraints,
                                                                                .require_power = true,
                                                                                .committed_design = &design,
                                                                                .committed_layout = &clock_layout,
                                                                                .propagate_all_clocks = true});
    if (!result.context_id.has_value()) {
      (void) _fast_sta.discardContextTransaction(*pending.context_id);
    }
    return result;
  };
  for (const auto& [identity, id] : _timing_contexts.clocks) {
    const Clock* owner = nullptr;
    for (const auto* clock : design.get_clocks()) {
      if (identity == std::pair{clock->get_clock_name(), clock->get_clock_net_name()}) {
        owner = clock;
        break;
      }
    }
    if (owner == nullptr && !std::ranges::any_of(_timing_constraints.clocks, [&](const auto& declaration) -> bool {
          return declaration.is_virtual && declaration.clock_name == identity.first && identity.second.empty();
        })) {
      releaseTimingContexts(candidate);
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_synthesis_clock_missing:" + identity.first);
    }
    Monitor view_monitor;
    const auto result = synchronize(id, owner);
    CTSLOG.info(Loc::current(), "FastSTA synthesis context synchronization: clock=", identity.first, ", wall_s=", view_monitor.getElapsedSeconds(),
                ", cpu_s=", view_monitor.getCPUSeconds(), ", success=", result.ok(), ".");
    if (!result.context_id.has_value()) {
      releaseTimingContexts(candidate);
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_synthesis_sync_failed:" + identity.first + ":" + result.failure_reason);
    }
    candidate.clocks.emplace(identity, *result.context_id);
  }
  if (_timing_contexts.unclocked.has_value()) {
    const auto result = synchronize(*_timing_contexts.unclocked, nullptr);
    if (!result.context_id.has_value()) {
      releaseTimingContexts(candidate);
      return failureStatus(DataManagerStatusCode::kCommitError, result.failure_reason);
    }
    candidate.unclocked = result.context_id;
  }
  for (const auto& [identity, id] : candidate.clocks) {
    if (!_fast_sta.commitContextTransaction(id)) {
      CTSLOG.error(Loc::current(), "FastSTA: validated synthesis transaction commit failed for ", identity.first, ".");
    }
  }
  if (candidate.unclocked.has_value() && !_fast_sta.commitContextTransaction(*candidate.unclocked)) {
    CTSLOG.error(Loc::current(), "FastSTA: validated unclocked synthesis transaction commit failed.");
  }
  contexts = std::move(candidate);
  return okStatus("CTS clock topology and affected timing synchronized.");
}

auto DataManager::pendingTimingContextsValid(bool require_power) const -> bool
{
  const auto valid_context = [&](FastStaContextId context_id) -> bool {
    const auto status = _fast_sta.queryAnalysisStatus(context_id);
    return status.has_value() && status->timing_valid && (!require_power || status->power_valid);
  };
  for (const auto& [identity, context_id] : _optimization_timing_contexts.clocks) {
    (void) identity;
    if (!valid_context(context_id)) {
      return false;
    }
  }
  return !_optimization_timing_contexts.unclocked.has_value() || valid_context(*_optimization_timing_contexts.unclocked);
}

auto DataManager::timingContextsMatchDesign(const TimingContextSet& contexts, const Design& design, const ClockLayout& layout) const -> bool
{
  for (const auto* clock : design.get_clocks()) {
    if (clock == nullptr || !contexts.clocks.contains({clock->get_clock_name(), clock->get_clock_net_name()})) {
      return false;
    }
  }
  for (const auto& [identity, id] : contexts.clocks) {
    const Clock* owner = nullptr;
    for (const auto* clock : design.get_clocks()) {
      if (identity == std::pair{clock->get_clock_name(), clock->get_clock_net_name()}) {
        owner = clock;
        break;
      }
    }
    if (owner == nullptr && !std::ranges::any_of(_timing_constraints.clocks, [&](const auto& declaration) -> bool {
          return declaration.is_virtual && identity.first == declaration.clock_name && identity.second.empty();
        })) {
      return false;
    }
    if (!_fast_sta.matchesClockInput(
            id,
            {.clock = owner, .constraints = &_timing_constraints, .committed_design = &design, .committed_layout = &layout, .propagate_all_clocks = true})) {
      return false;
    }
  }
  if (contexts.unclocked.has_value()) {
    return design.get_clocks().empty() && contexts.clocks.empty()
           && _fast_sta.matchesClockInput(
               *contexts.unclocked,
               {.constraints = &_timing_constraints, .committed_design = &design, .committed_layout = &layout, .propagate_all_clocks = true});
  }
  return !contexts.clocks.empty() || (!_timing_graph.has_value() && design.get_clocks().empty());
}

auto DataManager::getClockTimingContext(std::string_view clock_name) const -> std::optional<FastStaContextId>
{
  return findTimingContext(_timing_contexts, clock_name);
}

auto DataManager::getClockTimingContext(std::string_view clock_name, std::string_view clock_net_name) const -> std::optional<FastStaContextId>
{
  const auto iter = _timing_contexts.clocks.find(std::pair{std::string(clock_name), std::string(clock_net_name)});
  return iter == _timing_contexts.clocks.end() ? std::nullopt : std::optional<FastStaContextId>{iter->second};
}

auto DataManager::beginOptimizationTiming(const Design& design, const ClockLayout& clock_layout) -> DataManagerStatus
{
  if (_state != CTSRunState::kSynthesisCommitted) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "optimization timing requires synthesized state.");
  }

  if (!_optimization_timing_contexts.clocks.empty() || _optimization_timing_contexts.unclocked.has_value()) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "fast_sta_optimization_transaction_already_active");
  }
  Monitor prepare_monitor;
  if (!timingContextsMatchDesign(_timing_contexts, design, clock_layout)) {
    return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_optimization_input_changed");
  }
  const auto begin_one = [&](FastStaContextId committed_id) -> FastStaBuildResult {
    const auto status = _fast_sta.queryAnalysisStatus(committed_id);
    if (!status.has_value() || !status->timing_valid || (!status->power_valid && !_fast_sta.updatePower(committed_id))) {
      return {.failure_reason = "fast_sta_complete_power_unavailable"};
    }
    return _fast_sta.beginContextTransaction(committed_id);
  };
  for (const auto& [identity, id] : _timing_contexts.clocks) {
    const auto pending = begin_one(id);
    if (!pending.context_id.has_value()) {
      discardOptimizationTiming();
      return failureStatus(DataManagerStatusCode::kCommitError, pending.failure_reason);
    }
    _optimization_timing_contexts.clocks.emplace(identity, *pending.context_id);
  }
  if (_timing_contexts.unclocked.has_value()) {
    const auto pending = begin_one(*_timing_contexts.unclocked);
    if (!pending.context_id.has_value()) {
      discardOptimizationTiming();
      return failureStatus(DataManagerStatusCode::kCommitError, pending.failure_reason);
    }
    _optimization_timing_contexts.unclocked = pending.context_id;
  }
  CTSLOG.info(Loc::current(), "FastSTA optimization timing reuse: wall_s=", prepare_monitor.getElapsedSeconds(), ", cpu_s=", prepare_monitor.getCPUSeconds(),
              ".");
  return okStatus("CTS optimization reuses prepared timing in an isolated transaction.");
}

auto DataManager::updateOptimizationTiming(const std::vector<FastStaInstanceMasterChange>& changes) -> DataManagerStatus
{
  if (_state != CTSRunState::kSynthesisCommitted || !pendingTimingContextsValid(true)) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "fast_sta_pending_complete_timing_unavailable");
  }
  for (const auto& [identity, id] : _optimization_timing_contexts.clocks) {
    if (!_fast_sta.changeInstanceMasters(id, changes)) {
      discardOptimizationTiming();
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_accepted_update_failed:" + identity.first);
    }
  }
  if (_optimization_timing_contexts.unclocked.has_value() && !_fast_sta.changeInstanceMasters(*_optimization_timing_contexts.unclocked, changes)) {
    discardOptimizationTiming();
    return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_accepted_update_failed:unclocked");
  }
  return okStatus("CTS accepted masters updated complete timing and power.");
}

auto DataManager::buildClockSizingContext(const Design& design, const ClockLayout& clock_layout, std::size_t clock_index) -> FastStaBuildResult
{
  const auto clocks = design.get_clocks();
  if (clock_index >= clocks.size() || clocks.at(clock_index) == nullptr) {
    return FastStaBuildResult{.failure_reason = "fast_sta_clock_index_invalid"};
  }
  const auto* layout_clock = clock_layout.findClock(clock_index);
  const bool has_route = layout_clock != nullptr && std::ranges::any_of(layout_clock->nets, [](const auto& net) -> bool {
                           return std::ranges::any_of(net.routed_segments, [](const auto& segment) -> bool { return segment.routed && !segment.degraded; });
                         });
  const auto layout_dbu = clock_layout.get_design_dbu_per_um();
  if (has_route && (layout_dbu <= 0 || layout_dbu != _wrapper.queryDbUnit().value_or(0))) {
    return FastStaBuildResult{.failure_reason = "fast_sta_route_geometry_dbu_mismatch"};
  }
  const auto source_context = getOptimizationTimingContext(clocks.at(clock_index)->get_clock_name(), clocks.at(clock_index)->get_clock_net_name());
  if (!source_context.has_value()) {
    return FastStaBuildResult{.failure_reason = "fast_sta_source_context_unavailable"};
  }
  return _fast_sta.buildClockSizingContext(*source_context, FastStaBuildInput{
                                                                .clock = clocks.at(clock_index),
                                                                .timing_graph = nullptr,
                                                                .constraints = &_timing_constraints,
                                                                .require_power = true,
                                                                .committed_design = &design,
                                                                .committed_layout = &clock_layout,
                                                                .propagate_all_clocks = true,
                                                            });
}

auto DataManager::getOptimizationTimingContext(std::string_view clock_name) const -> std::optional<FastStaContextId>
{
  return findTimingContext(_optimization_timing_contexts, clock_name);
}

auto DataManager::getOptimizationTimingContext(std::string_view clock_name, std::string_view clock_net_name) const -> std::optional<FastStaContextId>
{
  const auto iter = _optimization_timing_contexts.clocks.find(std::pair{std::string(clock_name), std::string(clock_net_name)});
  return iter == _optimization_timing_contexts.clocks.end() ? std::nullopt : std::optional<FastStaContextId>{iter->second};
}

auto DataManager::findTimingContext(const TimingContextSet& contexts, std::string_view clock_name) -> std::optional<FastStaContextId>
{
  std::optional<FastStaContextId> result;
  for (const auto& [identity, id] : contexts.clocks) {
    if (identity.first == clock_name) {
      if (result.has_value()) {
        return std::nullopt;
      }
      result = id;
    }
  }
  return result;
}

void DataManager::releaseTimingContexts(TimingContextSet& contexts)
{
  for (const auto& [name, id] : contexts.clocks) {
    (void) _fast_sta.eraseContext(id);
  }
  if (contexts.unclocked.has_value()) {
    (void) _fast_sta.eraseContext(*contexts.unclocked);
  }
  contexts = {};
}

void DataManager::discardOptimizationTiming()
{
  releaseTimingContexts(_optimization_timing_contexts);
}

auto DataManager::commitSynthesis(std::unique_ptr<Design> design, ClockLayout clock_layout, const SynthesisTraceSummary& summary) -> DataManagerStatus
{
  Monitor commit_monitor;
  if (_state != CTSRunState::kInputReady && _state != CTSRunState::kSynthesisCommitted) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "synthesis commit requires input-ready or synthesis-committed state.");
  }
  if (design == nullptr || summary.outcome == SynthesisOutcome::kFailed || (summary.outcome == SynthesisOutcome::kFinished && !summary.success)) {
    return failureStatus(DataManagerStatusCode::kCommitError, "synthesis result is not committable.");
  }
  if (!design->rebuildClockDAG()) {
    return makeClockGraphFailureStatus(DataManagerStatusCode::kCommitError, "synthesis result is not a valid clock DAG", design->get_clock_dag());
  }
  TimingContextSet contexts;
  Monitor timing_monitor;
  auto timing_status = synchronizeTimingContexts(*design, clock_layout, contexts);
  const double timing_wall_s = timing_monitor.getElapsedSeconds();
  const double timing_cpu_s = timing_monitor.getCPUSeconds();
  CTSLOG.info(Loc::current(), "FastSTA synthesis commit timing: operation=clock_topology_synchronization, wall_s=", timing_wall_s, ", cpu_s=", timing_cpu_s,
              ", success=", timing_status.ok(), ".");
  if (!timing_status.ok()) {
    return timing_status;
  }
  replaceCommittedDesign(std::move(design), std::move(clock_layout), std::move(contexts));
  _synthesis_summary = summary;
  _state = CTSRunState::kSynthesisCommitted;
  CTSLOG.info(Loc::current(), "CTS synthesis commit: wall_s=", commit_monitor.getElapsedSeconds(), ", cpu_s=", commit_monitor.getCPUSeconds(), ".");
  return okStatus("CTS synthesis result committed.");
}

auto DataManager::commitOptimization(std::unique_ptr<Design> design, ClockLayout clock_layout, const OptimizationSummary& summary) -> DataManagerStatus
{
  if (_state != CTSRunState::kSynthesisCommitted) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "optimization commit requires synthesized state.");
  }
  if (design == nullptr || !summary.success) {
    discardOptimizationTiming();
    return failureStatus(DataManagerStatusCode::kCommitError, "optimization result is not committable.");
  }
  if (!design->rebuildClockDAG()) {
    discardOptimizationTiming();
    return makeClockGraphFailureStatus(DataManagerStatusCode::kCommitError, "optimization result is not a valid clock DAG", design->get_clock_dag());
  }
  TimingContextSet contexts;
  if (!_optimization_timing_contexts.clocks.empty() || _optimization_timing_contexts.unclocked.has_value()) {
    // Optimization refreshes this candidate after every accepted edit. Move
    // the validated pending contexts into the committed slot instead of
    // rebuilding the same complete FastSTA graph a third time.
    if (!timingContextsMatchDesign(_optimization_timing_contexts, *design, clock_layout) || !pendingTimingContextsValid(true)) {
      discardOptimizationTiming();
      return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_pending_context_invalid");
    }
    for (const auto& [identity, id] : _optimization_timing_contexts.clocks) {
      if (!_fast_sta.commitContextTransaction(id)) {
        CTSLOG.error(Loc::current(), "FastSTA: validated optimization transaction commit failed for ", identity.first, ".");
      }
    }
    if (_optimization_timing_contexts.unclocked.has_value() && !_fast_sta.commitContextTransaction(*_optimization_timing_contexts.unclocked)) {
      CTSLOG.error(Loc::current(), "FastSTA: validated unclocked transaction commit failed.");
    }
    contexts = std::move(_optimization_timing_contexts);
    _optimization_timing_contexts = {};
  } else if (_timing_graph.has_value() || !design->get_clocks().empty()) {
    return failureStatus(DataManagerStatusCode::kCommitError, "fast_sta_optimization_transaction_missing");
  }
  replaceCommittedDesign(std::move(design), std::move(clock_layout), std::move(contexts));
  _optimization_summary = summary;
  _state = CTSRunState::kOptimizationCommitted;
  return okStatus("CTS optimization result committed.");
}

auto DataManager::commitInstantiation(const InstantiationSummary& summary) -> DataManagerStatus
{
  if (_state != CTSRunState::kOptimizationCommitted && _state != CTSRunState::kSynthesisCommitted) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "instantiation commit requires synthesized or optimized state.");
  }
  if (!summary.success) {
    return failureStatus(DataManagerStatusCode::kCommitError, "instantiation result is not committable.");
  }
  _instantiation_summary = summary;
  _clock_layout.markInstantiationDone(true);
  _state = CTSRunState::kInstantiationCommitted;
  return okStatus("CTS instantiation result committed.");
}

auto DataManager::commitEvaluation(EvaluationState state) -> DataManagerStatus
{
  if (_state != CTSRunState::kInstantiationCommitted) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "evaluation commit requires instantiated state.");
  }
  if (!state.summary.has_evaluation_result) {
    return failureStatus(DataManagerStatusCode::kCommitError, "evaluation result is unavailable.");
  }
  _evaluation_state = std::move(state);
  _state = CTSRunState::kEvaluationCommitted;
  return okStatus("CTS evaluation result committed.");
}

auto DataManager::hasCommittedEvaluation() const -> bool
{
  return _state == CTSRunState::kEvaluationCommitted && _evaluation_state.summary.has_evaluation_result && _evaluation_state.statistics.valid;
}

auto DataManager::getCommittedEvaluationState() const -> const EvaluationState*
{
  return hasCommittedEvaluation() ? &_evaluation_state : nullptr;
}

void DataManager::replaceCommittedDesign(std::unique_ptr<Design> design, ClockLayout clock_layout, TimingContextSet contexts)
{
  releaseTimingContexts(_optimization_timing_contexts);
  releaseTimingContexts(_timing_contexts);
  _wrapper.clearCtsBindings();
  _design = std::move(design);
  _clock_layout = std::move(clock_layout);
  _timing_contexts = std::move(contexts);
}

void DataManager::reset()
{
  _evaluation_state = EvaluationState{};
  _instantiation_summary = InstantiationSummary{};
  _optimization_summary = OptimizationSummary{};
  _synthesis_summary = SynthesisTraceSummary{};
  _clock_layout.reset();
  discardOptimizationTiming();
  releaseTimingContexts(_timing_contexts);
  _fast_sta.reset();
  _timing_graph.reset();
  _timing_constraints = {};
  _wrapper.reset();
  _design = std::make_unique<Design>();
  _config.reset();
  _state = CTSRunState::kEmpty;
}

}  // namespace icts
