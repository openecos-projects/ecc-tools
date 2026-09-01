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
 * @file ClockTraceResolver.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-15
 * @brief SDC-rooted CTS clock trace resolver implementation.
 */

#include "ClockTraceResolver.hh"

#include <algorithm>
#include <map>
#include <optional>
#include <ostream>
#include <ranges>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbNet.h"
#include "Logger.hh"
#include "SDCClockReader.hh"
#include "clock_trace/SDCClockTraceAlgorithm.hh"

namespace icts {
namespace {

auto findIdbNet(idb::IdbDesign* idb_design, const std::string& net_name) -> idb::IdbNet*
{
  auto* net_list = idb_design == nullptr ? nullptr : idb_design->get_net_list();
  return net_list == nullptr ? nullptr : net_list->find_net(net_name);
}

auto resolveSingleSeedNet(idb::IdbDesign* idb_design, const SdcClockDecl& clock) -> idb::IdbNet*
{
  std::vector<idb::IdbNet*> seed_nets;
  for (const auto& target : clock.targets) {
    auto resolved_nets = clock_trace::ResolveRefNets(idb_design, target);
    seed_nets.insert(seed_nets.end(), resolved_nets.begin(), resolved_nets.end());
  }
  std::ranges::sort(seed_nets);
  const auto unique_seed_nets = std::ranges::unique(seed_nets);
  seed_nets.erase(unique_seed_nets.begin(), unique_seed_nets.end());
  return seed_nets.size() == 1U ? seed_nets.front() : nullptr;
}

auto makeDirectClockTarget(const std::string& clock_name, const std::string& source_net_name, const std::vector<ClockTraceRecord>& accepted_records)
    -> ClockTraceClockTarget
{
  ClockTraceClockTarget target{
      .clock_name = clock_name,
      .clock_net_name = source_net_name,
      .preclustered_sink_reuse = false,
      .preclustered_sink_anchors = {},
      .terminal_net_names = {},
      .propagation_steps = {},
  };
  std::set<std::string> terminal_nets;
  std::map<std::tuple<std::string, std::string, std::string, std::string, std::string>, ClockTracePropagationStep> steps;
  for (const auto& record : accepted_records) {
    terminal_nets.insert(record.net_name);
    for (const auto& step : record.propagation_steps) {
      // Every net covered by an owned transition remains a materialization
      // boundary.  The reader excludes the transition's explicit input pin
      // from terminal membership, while preserving any other load on that net
      // (including an input-only BUF/INV boundary).  Publishing only accepted
      // leaf nets would silently drop those same-clock boundary loads.
      terminal_nets.insert(step.input_net_name);
      terminal_nets.insert(step.output_net_name);
      steps.emplace(std::make_tuple(step.inst_name, step.input_pin_name, step.output_pin_name, step.input_net_name, step.output_net_name), step);
    }
  }
  target.terminal_net_names.assign(terminal_nets.begin(), terminal_nets.end());
  for (const auto& [_, step] : steps) {
    target.propagation_steps.push_back(step);
  }
  return target;
}

auto tryBuildPreclusteredClockTarget(const SdcLibertyCellLookup& liberty_cell_lookup, idb::IdbDesign* idb_design, const SdcClockDecl& clock,
                                     const std::vector<ClockTraceRecord>& accepted_records) -> std::optional<ClockTraceClockTarget>
{
  if (clock.kind != SdcClockDecl::Kind::kPrimary || accepted_records.size() < 2U) {
    return std::nullopt;
  }
  auto* source_net = resolveSingleSeedNet(idb_design, clock);
  if (source_net == nullptr) {
    return std::nullopt;
  }

  std::vector<ClockTracePreclusteredSinkAnchor> anchors;
  anchors.reserve(accepted_records.size());
  std::set<std::string> anchor_input_pins;
  for (const auto& record : accepted_records) {
    auto* leaf_net = findIdbNet(idb_design, record.net_name);
    auto anchor = clock_trace::BuildPreclusteredSinkAnchor(liberty_cell_lookup, leaf_net);
    if (!anchor.has_value() || !anchor_input_pins.insert(anchor->driver_inst_name + "/" + anchor->input_pin_name).second) {
      return std::nullopt;
    }
    anchors.push_back(std::move(*anchor));
  }

  return ClockTraceClockTarget{
      .clock_name = clock.clock_name,
      .clock_net_name = source_net->get_net_name(),
      .preclustered_sink_reuse = true,
      .preclustered_sink_anchors = std::move(anchors),
      .terminal_net_names = {},
      .propagation_steps = {},
  };
}

}  // namespace

auto ClockTraceResolver::resolve(const SdcClockData& clock_data, idb::IdbDesign* idb_design, const SdcLibertyCellLookup& liberty_cell_lookup,
                                 std::size_t max_fanout) -> ClockTraceBuild
{
  // Fanout is a synthesis-legality constraint, not SDC ownership evidence.
  // Keep the public adapter signature stable while deliberately excluding it
  // from trace classification.
  static_cast<void>(max_fanout);
  ClockTraceBuild build;
  if (idb_design == nullptr || idb_design->get_net_list() == nullptr) {
    CTSLOG.warn(Loc::current(), "ClockTraceResolver: iDB design or net list is null.");
    return build;
  }

  const auto case_constraints = clock_trace::BuildCaseConstraintSet(clock_data);
  const auto generated_boundary_owner_by_net = clock_trace::BuildGeneratedBoundaryOwners(idb_design, clock_data);
  const auto clock_view_by_name = clock_trace::BuildClockDeclViews(idb_design, clock_data);
  std::unordered_map<std::string, const SdcClockDecl*> clock_decl_by_name;
  clock_decl_by_name.reserve(clock_data.clocks.size());
  for (const auto& clock : clock_data.clocks) {
    if (!clock.clock_name.empty()) {
      clock_decl_by_name[clock.clock_name] = &clock;
    }
  }

  std::vector<ClockTraceRecord> candidate_records;
  for (const auto& clock : clock_data.clocks) {
    auto records = clock_trace::TraceClock(liberty_cell_lookup, idb_design, clock, case_constraints, generated_boundary_owner_by_net);
    candidate_records.insert(candidate_records.end(), records.begin(), records.end());
  }

  std::map<std::string, std::set<std::string>> accepted_clock_names_by_net;
  for (const auto& record : candidate_records) {
    if (record.status == "accepted" && !record.net_name.empty()) {
      accepted_clock_names_by_net[record.net_name].insert(record.clock_name);
    }
  }

  std::vector<ClockTraceRecord> resolved_records;
  resolved_records.reserve(candidate_records.size());
  for (auto record : candidate_records) {
    if (record.status == "accepted" && accepted_clock_names_by_net[record.net_name].size() > 1U) {
      record.status = "ambiguous";
      record.reason = "target_net_reachable_from_multiple_sdc_clocks";
    }
    clock_trace::AnnotateRecordOwnership(record, clock_view_by_name);
    resolved_records.push_back(std::move(record));
  }
  std::ranges::sort(resolved_records, {}, [](const ClockTraceRecord& record) -> auto {
    return std::tie(record.clock_name, record.net_name, record.status, record.reason, record.trace_path);
  });

  const bool has_ambiguous_ownership
      = std::ranges::any_of(resolved_records, [](const ClockTraceRecord& record) -> bool { return record.status == "ambiguous"; });
  if (has_ambiguous_ownership) {
    // Ownership is an input-wide contract: publishing even the unaffected
    // targets would admit a partial SDC interpretation into canonical state.
    build.status = ClockTraceBuildStatusCode::kAmbiguousOwnership;
    build.message = "clock_trace_ambiguous_ownership";
    build.summary.records = std::move(resolved_records);
    build.summary.unowned_clock_like_records = clock_trace::CollectUnownedClockLikeRecords(liberty_cell_lookup, idb_design, build.summary.records);
    return build;
  }

  std::map<std::string, std::vector<ClockTraceRecord>> accepted_records_by_clock;
  for (const auto& record : resolved_records) {
    if (record.status == "accepted" && !record.clock_name.empty() && !record.net_name.empty()) {
      accepted_records_by_clock[record.clock_name].push_back(record);
    }
  }

  for (const auto& [clock_name, accepted_records] : accepted_records_by_clock) {
    const auto decl_iter = clock_decl_by_name.find(clock_name);
    if (decl_iter != clock_decl_by_name.end()) {
      auto preclustered_target = tryBuildPreclusteredClockTarget(liberty_cell_lookup, idb_design, *decl_iter->second, accepted_records);
      if (preclustered_target.has_value()) {
        build.output.clock_targets.push_back(std::move(*preclustered_target));
        continue;
      }
      auto* source_net = resolveSingleSeedNet(idb_design, *decl_iter->second);
      if (source_net != nullptr) {
        build.output.clock_targets.push_back(makeDirectClockTarget(clock_name, source_net->get_net_name(), accepted_records));
        continue;
      }
    }

    // A clock with more than one resolved seed has no unique ownership root.  Keep
    // its records for diagnostics, but do not manufacture independent clocks from
    // accepted leaves: that would silently turn an ambiguous input into graph
    // structure that was never declared by SDC.
  }

  build.summary.records = std::move(resolved_records);
  build.summary.unowned_clock_like_records = clock_trace::CollectUnownedClockLikeRecords(liberty_cell_lookup, idb_design, build.summary.records);
  return build;
}

}  // namespace icts
