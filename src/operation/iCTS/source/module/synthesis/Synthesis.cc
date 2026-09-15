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
 * @file Synthesis.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS synthesis entry facade implementation.
 */

#include "synthesis/Synthesis.hh"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "config/Config.hh"
#include "data_manager/DataManager.hh"
#include "design/Clock.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Pin.hh"
#include "io/Wrapper.hh"
#include "synthesis/distribution/ClockDistribution.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/realization/ClockTreeRealization.hh"
#include "synthesis/topology/Topology.hh"
#include "synthesis/topology/layout/ClockLayoutBuilder.hh"
#include "synthesis/trace/domain_status/DomainStatusRecorder.hh"

namespace icts {

class Net;

namespace {

struct ClockSynthesisSummary
{
  bool success = false;
  bool skipped = false;
};

struct ClockSynthesisCounters
{
  std::size_t total_sink_domains = 0U;
  std::size_t hard_macro_sinks = 0U;
  std::size_t regular_sinks = 0U;
};

struct SynthesizedObjectCounts
{
  std::size_t insts = 0U;
  std::size_t nets = 0U;
};

auto CountSynthesizedObjects(const Design& design) -> SynthesizedObjectCounts
{
  SynthesizedObjectCounts counts;
  for (const auto* clock : design.get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    std::unordered_set<const Pin*> synthesized_outputs;
    for (const auto& arc : clock->get_propagation_arcs()) {
      if (arc.origin != ClockPropagationOrigin::kSynthesized) {
        continue;
      }
      ++counts.insts;
      if (arc.output_pin != nullptr) {
        synthesized_outputs.insert(arc.output_pin);
      }
    }
    for (const auto* net : clock->get_nets()) {
      if (net != nullptr && synthesized_outputs.contains(net->get_driver())) {
        ++counts.nets;
      }
    }
  }
  return counts;
}

struct ClockSynthesisRunInput
{
  const Config* config = nullptr;
  Design* design = nullptr;
  Wrapper* wrapper = nullptr;
  FastSTA* fast_sta = nullptr;
  Clock* clock = nullptr;
  std::size_t clock_index = 0U;
  ClockLayout* clock_layout = nullptr;
  SynthesisTraceSummary* summary = nullptr;
  ClockSynthesisCounters* counters = nullptr;
  CharacterizationLibrary* characterization_library = nullptr;
};

auto synthesisOutcomeName(SynthesisOutcome outcome) -> const char*
{
  switch (outcome) {
    case SynthesisOutcome::kFinished:
      return "finished";
    case SynthesisOutcome::kFailed:
      return "failed";
    case SynthesisOutcome::kNoOp:
      return "no_op";
  }
  return "unknown";
}

auto RequireSynthesisSummary(SynthesisTraceSummary* summary) -> SynthesisTraceSummary&
{
  if (summary == nullptr) {
    CTSLOG.error(Loc::current(), "Synthesis: trace summary is null.");
  }
  return *summary;
}

class ClockSynthesisRun
{
 public:
  explicit ClockSynthesisRun(const ClockSynthesisRunInput& input)
      : _config(input.config),
        _design(input.design),
        _wrapper(input.wrapper),
        _fast_sta(input.fast_sta),
        _clock(input.clock),
        _clock_index(input.clock_index),
        _clock_layout(input.clock_layout),
        _summary(input.summary),
        _status_recorder(RequireSynthesisSummary(input.summary).domain_status),
        _counters(input.counters),
        _characterization_library(input.characterization_library)
  {
    if (_config == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock config is null.");
    }
    if (_design == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock design is null.");
    }
    if (_wrapper == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock wrapper is null.");
    }
    if (_fast_sta == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock FastSTA is null.");
    }
    if (_clock == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock clock is null.");
    }
    if (_clock_layout == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock layout is null.");
    }
    if (_summary == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock summary is null.");
    }
    if (_counters == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock counters are null.");
    }
    if (_characterization_library == nullptr) {
      CTSLOG.error(Loc::current(), "Synthesis: per-clock characterization library is null.");
    }
  }

  auto run() -> ClockSynthesisSummary;

 private:
  auto ensureClockSource() -> std::pair<Pin*, Net*>;
  auto prepareSinkDomain(SinkDomainKind sink_domain, const std::vector<Pin*>& sinks, std::size_t valid_sinks) -> bool;
  auto formClockTopology(std::size_t valid_sinks) -> bool;

  const Config* _config = nullptr;
  Design* _design = nullptr;
  Wrapper* _wrapper = nullptr;
  FastSTA* _fast_sta = nullptr;
  Clock* _clock = nullptr;
  std::size_t _clock_index = 0U;
  ClockLayout* _clock_layout = nullptr;
  SynthesisTraceSummary* _summary = nullptr;
  DomainStatusRecorder _status_recorder;
  ClockSynthesisCounters* _counters = nullptr;
  CharacterizationLibrary* _characterization_library = nullptr;
  ClockLayout _per_clock_layout;
  std::vector<ClockDistributionContext> _sink_domain_contexts;
};

auto ClockSynthesisRun::ensureClockSource() -> std::pair<Pin*, Net*>
{
  auto* clock_source = _clock->get_clock_source();
  auto* clock_source_net = _clock->get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    _clock->set_clock_source_net(clock_source_net);
  }
  return {clock_source, clock_source_net};
}

auto ClockSynthesisRun::prepareSinkDomain(SinkDomainKind sink_domain, const std::vector<Pin*>& sinks, std::size_t valid_sinks) -> bool
{
  if (sinks.empty()) {
    return true;
  }
  ++_counters->total_sink_domains;
  auto context = ClockDistribution::prepare(ClockDistributionInput{
      .design = _design,
      .clock = _clock,
      .wrapper = _wrapper,
      .clock_index = _clock_index,
      .sink_domain = sink_domain,
      .sinks = sinks,
      .valid_sinks = valid_sinks,
      .root_buffer_types = _config->get_buffer_types(),
      .status_recorder = &_status_recorder,
  });
  if (!context.has_value()) {
    Topology::resetClockTopology(*_design, *_clock);
    return false;
  }
  _sink_domain_contexts.push_back(std::move(*context));
  return true;
}

auto ClockSynthesisRun::formClockTopology(std::size_t valid_sinks) -> bool
{
  if (!Topology::formClock(ClockTopologyInput{
          .config = _config,
          .design = _design,
          .wrapper = _wrapper,
          .fast_sta = _fast_sta,
          .clock = _clock,
          .clock_index = _clock_index,
          .clock_layout = &_per_clock_layout,
          .summary = _summary,
          .status_recorder = &_status_recorder,
          .characterization_library = _characterization_library,
          .valid_sinks = valid_sinks,
          .sink_domains = &_sink_domain_contexts,
      })) {
    return false;
  }
  ClockLayoutBuilder::merge(*_clock_layout, _per_clock_layout);
  return true;
}

auto ClockSynthesisRun::run() -> ClockSynthesisSummary
{
  _per_clock_layout.ensureClock(_clock->get_clock_name(), _clock->get_clock_net_name(), _clock_index);

  const auto synthesis_frontier = ClockTreeRealization::deriveSynthesisFrontier(*_clock);
  if (synthesis_frontier.hasTracedTopology() && !_design->rebuildClockDAG()) {
    _status_recorder.appendNoDomain(*_clock, DomainStatus::kFailed, _clock->get_loads().size(), _clock->get_loads().size(),
                                    _design->get_clock_dag().get_status());
    return ClockSynthesisSummary{.success = false, .skipped = false};
  }
  Topology::resetClockTopology(*_design, *_clock);

  const auto [clock_source, clock_source_net] = ensureClockSource();
  if (clock_source == nullptr) {
    _status_recorder.appendNoDomain(*_clock, DomainStatus::kSkipped, 0U, 0U, "clock source is null");
    CTSLOG.warn(Loc::current(), "Synthesis: skip clock \"", _clock->get_clock_name(), "\" because clock source is null.");
    return ClockSynthesisSummary{.success = false, .skipped = true};
  }
  if (clock_source_net == nullptr) {
    _status_recorder.appendNoDomain(*_clock, DomainStatus::kFailed, 0U, 0U, "clock source net is null");
    CTSLOG.warn(Loc::current(), "Synthesis: clock \"", _clock->get_clock_name(), "\" failed because the clock source net is null.");
    return ClockSynthesisSummary{.success = false, .skipped = false};
  }

  const auto terminal_partition = ClockDistribution::partitionSinkDomains(*_clock);
  const auto valid_sinks = terminal_partition.valid_sink_count;
  _counters->hard_macro_sinks += terminal_partition.macro_sinks.size();
  _counters->regular_sinks += terminal_partition.regular_sinks.size();
  const auto active_domains = static_cast<unsigned>(!terminal_partition.macro_sinks.empty()) + static_cast<unsigned>(!terminal_partition.regular_sinks.empty());
  EmitLogTable(Loc::current(), "CTS Sink Domain Overview",
               {"Clock", "Net", "Valid Sinks", "Hard Macro", "Regular", "Synthesis Frontier", "Active Domains", "Preclustered Reuse"},
               {{_clock->get_clock_name(), _clock->get_clock_net_name(), ToLogTableCell(valid_sinks), ToLogTableCell(terminal_partition.macro_sinks.size()),
                 ToLogTableCell(terminal_partition.regular_sinks.size()), ToLogTableCell(synthesis_frontier.pins.size()), ToLogTableCell(active_domains),
                 ToLogTableCell(_clock->is_preclustered_sink_reuse())}});
  if (valid_sinks == 0U) {
    _status_recorder.appendNoDomain(*_clock, DomainStatus::kSkipped, 0U, 0U, "no valid sinks");
    CTSLOG.warn(Loc::current(), "Synthesis: skip clock \"", _clock->get_clock_name(), "\" because no valid sinks are available.");
    return ClockSynthesisSummary{.success = false, .skipped = true};
  }
  ClockLayoutBuilder::appendSinkInsts(_per_clock_layout, *_clock, _clock_index, terminal_partition.macro_sinks, SinkDomainKind::kHardMacro);
  ClockLayoutBuilder::appendSinkInsts(_per_clock_layout, *_clock, _clock_index, terminal_partition.regular_sinks, SinkDomainKind::kRegular);

  if (synthesis_frontier.isFullyCoveredTracedTopology()) {
    if (!_design->rebuildClockDAG()) {
      _status_recorder.appendNoDomain(*_clock, DomainStatus::kFailed, valid_sinks, valid_sinks, _design->get_clock_dag().get_status());
      return ClockSynthesisSummary{.success = false, .skipped = false};
    }
    if (!terminal_partition.macro_sinks.empty()) {
      ++_counters->total_sink_domains;
      _status_recorder.append(*_clock, DomainStatus::kFinished, SinkDomainKind::kHardMacro, valid_sinks, terminal_partition.macro_sinks.size(),
                              "traced_input_fully_covered");
    }
    if (!terminal_partition.regular_sinks.empty()) {
      ++_counters->total_sink_domains;
      _status_recorder.append(*_clock, DomainStatus::kFinished, SinkDomainKind::kRegular, valid_sinks, terminal_partition.regular_sinks.size(),
                              "traced_input_fully_covered");
    }
    ClockLayoutBuilder::merge(*_clock_layout, _per_clock_layout);
    return ClockSynthesisSummary{.success = true, .skipped = false};
  }

  const auto frontier_partition = ClockDistribution::partitionSinkDomains(synthesis_frontier.pins);
  if (frontier_partition.valid_sink_count == 0U) {
    _status_recorder.appendNoDomain(*_clock, DomainStatus::kFailed, valid_sinks, 0U, "synthesis frontier has no valid loads");
    return ClockSynthesisSummary{.success = false, .skipped = false};
  }

  _sink_domain_contexts.reserve(2U);
  if (!prepareSinkDomain(SinkDomainKind::kHardMacro, frontier_partition.macro_sinks, valid_sinks)) {
    return ClockSynthesisSummary{.success = false, .skipped = false};
  }
  if (!prepareSinkDomain(SinkDomainKind::kRegular, frontier_partition.regular_sinks, valid_sinks)) {
    return ClockSynthesisSummary{.success = false, .skipped = false};
  }
  return formClockTopology(valid_sinks) ? ClockSynthesisSummary{.success = true, .skipped = false} : ClockSynthesisSummary{.success = false, .skipped = false};
}

}  // namespace

auto CommitSynthesisCandidate(DataManager& data_manager, std::unique_ptr<Design> design, ClockLayout clock_layout, SynthesisTraceSummary summary,
                              DataManagerStatus& commit_status) -> SynthesisTraceSummary
{
  summary.commit_status = "committed";
  commit_status = data_manager.commitSynthesis(std::move(design), std::move(clock_layout), summary);
  if (!commit_status.ok()) {
    summary.success = false;
    summary.outcome = SynthesisOutcome::kFailed;
    summary.failure_reason = commit_status.message;
    summary.commit_status = "rejected";
  }
  return summary;
}

auto Synthesis::run() -> SynthesisTraceSummary
{
  Monitor monitor;
  CTSLOG.info(Loc::current(), "Starting CTS synthesis...");
  auto local_design = CTSDM.cloneDesign();
  ClockLayout clock_layout;
  CharacterizationLibrary char_library;
  const auto& config = CTSDM.getConfig();
  auto& design = *local_design;
  auto& wrapper = CTSDM.getWrapper();
  auto& fast_sta = CTSDM.getFastSTA();
  clock_layout.reset();
  SynthesisTraceSummary summary;
  summary.domain_status.clear();
  auto clocks = design.get_clocks();
  const std::size_t total_clocks = clocks.size();
  std::size_t successful_clocks = 0U;
  std::size_t skipped_clocks = 0U;
  std::size_t failed_clocks = 0U;
  ClockSynthesisCounters synthesis_counters;

  for (std::size_t clock_index = 0; clock_index < clocks.size(); ++clock_index) {
    auto* clock = clocks.at(clock_index);
    if (clock == nullptr) {
      ++skipped_clocks;
      summary.domain_status.push_back(SynthesisTraceStatusRecord{
          .clock_name = {},
          .clock_net_name = {},
          .status = "skipped",
          .sink_domain = "none",
          .valid_sink_count = 0U,
          .sink_domain_sink_count = 0U,
          .detail = "clock pointer is null",
      });
      continue;
    }

    const auto sink_partition = ClockDistribution::partitionSinkDomains(*clock);
    if (sink_partition.valid_sink_count > 0U && wrapper.is_design_ready()) {
      const auto dbu_per_um = wrapper.queryDbUnit();
      if (!dbu_per_um.has_value()) {
        CTSLOG.warn(Loc::current(), "Synthesis: clock \"", clock->get_clock_name(), "\" failed because DBU-per-micron is unavailable.");
        ++failed_clocks;
        summary.domain_status.push_back(SynthesisTraceStatusRecord{
            .clock_name = clock->get_clock_name(),
            .clock_net_name = clock->get_clock_net_name(),
            .status = "failed",
            .sink_domain = "none",
            .valid_sink_count = sink_partition.valid_sink_count,
            .sink_domain_sink_count = 0U,
            .detail = "dbu_per_um_unavailable",
        });
        continue;
      }
      clock_layout.set_design_dbu_per_um(*dbu_per_um);
    }

    ClockSynthesisRun clock_synthesis(ClockSynthesisRunInput{
        .config = &config,
        .design = &design,
        .wrapper = &wrapper,
        .fast_sta = &fast_sta,
        .clock = clock,
        .clock_index = clock_index,
        .clock_layout = &clock_layout,
        .summary = &summary,
        .counters = &synthesis_counters,
        .characterization_library = &char_library,
    });
    const auto clock_summary = clock_synthesis.run();
    if (clock_summary.success) {
      ++successful_clocks;
    } else if (clock_summary.skipped) {
      ++skipped_clocks;
    } else {
      ++failed_clocks;
    }
  }

  summary.total_clocks = total_clocks;
  summary.successful_clocks = successful_clocks;
  summary.skipped_clocks = skipped_clocks;
  summary.failed_clocks = failed_clocks;
  summary.success = successful_clocks > 0U && failed_clocks == 0U;
  if (total_clocks == 0U) {
    summary.outcome = SynthesisOutcome::kNoOp;
    summary.no_op_reason = "no_clocks_discovered";
  } else if (successful_clocks == 0U && skipped_clocks > 0U && failed_clocks == 0U) {
    summary.outcome = SynthesisOutcome::kNoOp;
    summary.no_op_reason = "all_clocks_skipped";
  } else {
    summary.outcome = summary.success ? SynthesisOutcome::kFinished : SynthesisOutcome::kFailed;
  }
  summary.total_sink_domains = synthesis_counters.total_sink_domains;
  summary.hard_macro_sinks = synthesis_counters.hard_macro_sinks;
  summary.regular_sinks = synthesis_counters.regular_sinks;
  const auto synthesized_object_counts = CountSynthesizedObjects(design);
  summary.inserted_inst_count = synthesized_object_counts.insts;
  summary.inserted_net_count = synthesized_object_counts.nets;
  clock_layout.markSynthesisComplete(summary.success);

  DataManagerStatus commit_status;
  bool commit_attempted = false;
  if (summary.outcome != SynthesisOutcome::kFailed) {
    commit_attempted = true;
    summary = CommitSynthesisCandidate(CTSDM, std::move(local_design), std::move(clock_layout), std::move(summary), commit_status);
  }

  EmitLogTable(Loc::current(), "CTS Clock Tree Synthesis Overview", {"Metric", "Value"},
               {{"Outcome", synthesisOutcomeName(summary.outcome)},
                {"Success", ToLogTableCell(summary.success)},
                {"Total Clocks", ToLogTableCell(summary.total_clocks)},
                {"Successful Clocks", ToLogTableCell(summary.successful_clocks)},
                {"Skipped Clocks", ToLogTableCell(summary.skipped_clocks)},
                {"Failed Clocks", ToLogTableCell(summary.failed_clocks)},
                {"Sink Domains", ToLogTableCell(summary.total_sink_domains)},
                {"Hard Macro Sinks", ToLogTableCell(summary.hard_macro_sinks)},
                {"Regular Sinks", ToLogTableCell(summary.regular_sinks)},
                {"Selected HTree Levels", ToLogTableCell(summary.selected_htree_level_count)},
                {"Selected HTree Depth", ToLogTableCell(summary.selected_htree_depth)},
                {"Inserted Instances", ToLogTableCell(summary.inserted_inst_count)},
                {"Inserted Nets", ToLogTableCell(summary.inserted_net_count)},
                {"Inserted HTree Buffers", ToLogTableCell(summary.htree_inserted_buffer_count)},
                {"Inserted HTree Nets", ToLogTableCell(summary.htree_inserted_net_count)},
                {"Commit Status", summary.commit_status},
                {"No-op Reason", summary.no_op_reason.empty() ? "n/a" : summary.no_op_reason},
                {"Failure Reason", summary.failure_reason.empty() ? "n/a" : summary.failure_reason}});

  LogTableRows domain_rows;
  for (const auto& status : summary.domain_status) {
    domain_rows.push_back({status.clock_name.empty() ? "n/a" : status.clock_name, status.clock_net_name.empty() ? "n/a" : status.clock_net_name,
                           status.sink_domain, status.status, ToLogTableCell(status.valid_sink_count), ToLogTableCell(status.sink_domain_sink_count),
                           status.detail.empty() ? "n/a" : status.detail});
  }
  EmitLogTable(Loc::current(), "CTS Clock Tree Sink Domains", {"Clock", "Net", "Domain", "Status", "Valid", "Domain Sinks", "Detail"}, domain_rows);

  if (commit_attempted && !commit_status.ok()) {
    LogTableRows issue_rows;
    constexpr std::size_t max_commit_issues = 8U;
    for (std::size_t issue_index = 0; issue_index < std::min(max_commit_issues, commit_status.graph_issues.size()); ++issue_index) {
      const auto& issue = commit_status.graph_issues.at(issue_index);
      issue_rows.push_back({ClockGraphIssueCodeName(issue.code), issue.clock_name.empty() ? "n/a" : issue.clock_name,
                            issue.object_name.empty() ? "n/a" : issue.object_name, issue.message.empty() ? "n/a" : issue.message});
    }
    if (issue_rows.empty()) {
      issue_rows.push_back({"commit_error", "n/a", "n/a", commit_status.message});
    }
    EmitLogTable(Loc::current(), "CTS Synthesis Commit Diagnostics", {"Code", "Clock", "Object", "Detail"}, issue_rows);
    CTSLOG.warn(Loc::current(), "CTS synthesis commit failed: ", commit_status.message);
  }
  CTSLOG.info(Loc::current(), "Completed CTS synthesis", monitor.getStatsInfo());
  return summary;
}

}  // namespace icts
