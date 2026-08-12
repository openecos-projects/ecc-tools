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
#include <filesystem>
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
    const auto boundary_sinks = sink_type_counts[InstType::kClockGate] + sink_type_counts[InstType::kMux] + sink_type_counts[InstType::kClockLogic]
                                + sink_type_counts[InstType::kBoundaryLoad];
    const auto propagation_sinks = sink_type_counts[InstType::kBuffer] + sink_type_counts[InstType::kInverter];
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
  return DataManagerStatus{.code = DataManagerStatusCode::kOk, .message = std::move(message), .diagnostics = {}};
}

auto DataManager::failureStatus(DataManagerStatusCode code, std::string message) -> DataManagerStatus
{
  return DataManagerStatus{.code = code, .message = std::move(message), .diagnostics = {}};
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
  status.diagnostics = _config.get_warnings();
  if (!status.ok()) {
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
  const auto sdc_clock_data = SdcClockReader().readClockData();
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
  return okStatus("CTS input data is ready.");
}

auto DataManager::commitSynthesis(std::unique_ptr<Design> design, ClockLayout clock_layout, const SynthesisTraceSummary& summary) -> DataManagerStatus
{
  if (_state != CTSRunState::kInputReady) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "synthesis commit requires input-ready state.");
  }
  if (design == nullptr || summary.outcome == SynthesisOutcome::kFailed || (summary.outcome == SynthesisOutcome::kFinished && !summary.success)) {
    return failureStatus(DataManagerStatusCode::kCommitError, "synthesis result is not committable.");
  }
  if (summary.outcome == SynthesisOutcome::kFinished && !design->rebuildClockDAG()) {
    return failureStatus(DataManagerStatusCode::kCommitError, "synthesis result is not a valid clock DAG.");
  }
  replaceCommittedDesign(std::move(design));
  _clock_layout = std::move(clock_layout);
  _synthesis_summary = summary;
  _state = CTSRunState::kSynthesisCommitted;
  return okStatus("CTS synthesis result committed.");
}

auto DataManager::commitOptimization(std::unique_ptr<Design> design, ClockLayout clock_layout, const OptimizationSummary& summary) -> DataManagerStatus
{
  if (_state != CTSRunState::kSynthesisCommitted) {
    return failureStatus(DataManagerStatusCode::kInvalidState, "optimization commit requires synthesized state.");
  }
  if (design == nullptr || !summary.success || !design->rebuildClockDAG()) {
    return failureStatus(DataManagerStatusCode::kCommitError, "optimization result is not committable.");
  }
  replaceCommittedDesign(std::move(design));
  _clock_layout = std::move(clock_layout);
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

void DataManager::replaceCommittedDesign(std::unique_ptr<Design> design)
{
  _wrapper.clearCtsBindings();
  _design = std::move(design);
}

void DataManager::reset()
{
  _evaluation_state = EvaluationState{};
  _instantiation_summary = InstantiationSummary{};
  _optimization_summary = OptimizationSummary{};
  _synthesis_summary = SynthesisTraceSummary{};
  _clock_layout.reset();
  _fast_sta.reset();
  _wrapper.reset();
  _design = std::make_unique<Design>();
  _config.reset();
  _state = CTSRunState::kEmpty;
}

}  // namespace icts
