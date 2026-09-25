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
 * @file FastSTALogicPropagation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Seed and propagate logic arrival and slew with cached Liberty responses.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <iterator>
#include <limits>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FastSTAClockState.hh"
#include "FastSTAConstraints.hh"
#include "FastSTADmpCeff.hh"
#include "FastSTAEvents.hh"
#include "FastSTALibertyModel.hh"
#include "FastSTALogicAnalysis.hh"
#include "FastSTATimingLookup.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts::fast_sta {

namespace {

auto findLogicArcIndexes(const FastStaContext& context, const FastStaTimingArc& arc) -> ArcIndexSelection
{
  const auto cell = context.liberty_cell_by_master.find(arc.cell_master);
  if (cell == context.liberty_cell_by_master.end()) {
    return {};
  }
  return SelectArcIndexes(cell->second, [&](const auto& candidate) -> bool {
    return candidate.from_port == arc.from_port && candidate.to_port == arc.to_port && !candidate.edge_triggered
           && FastStaCondition::evaluate(candidate.when, FastStaConstraints::caseLookup(context, context.nodes.at(arc.to_node_id).inst_name))
                  != FastStaLogicValue::kZero;
  });
}

auto findLaunchArcIndexes(const FastStaContext& context, const FastStaTimingLaunch& launch) -> ArcIndexSelection
{
  const auto cell = context.liberty_cell_by_master.find(launch.cell_master);
  if (cell == context.liberty_cell_by_master.end()) {
    return {};
  }
  return SelectArcIndexes(cell->second, [&](const auto& candidate) -> bool {
    return candidate.from_port == launch.clock_port && candidate.to_port == launch.output_port && candidate.edge_triggered
           && candidate.trigger_transition == launch.clock_transition
           && FastStaCondition::evaluate(candidate.when, FastStaConstraints::caseLookup(context, context.nodes.at(launch.output_node_id).inst_name))
                  != FastStaLogicValue::kZero;
  });
}

}  // namespace

auto LogicNetLoad(const FastStaContext& context, const FastStaNet& net) -> double
{
  auto pin_cap_pf = 0.0;
  for (const auto node_id : net.load_node_ids) {
    if (node_id < context.nodes.size()) {
      pin_cap_pf += std::max(0.0, context.nodes.at(node_id).input_cap_pf);
    }
  }
  return std::max({0.0, net.load_cap_pf, net.wire_cap_pf + pin_cap_pf});
}

namespace {

auto outputLoad(const FastStaContext& context, FastStaNodeId node_id) -> double
{
  auto load_pf = 0.0;
  if (node_id >= context.nodes.size()) {
    return load_pf;
  }
  for (const auto net_id : context.nodes.at(node_id).output_net_ids) {
    if (net_id < context.nets.size() && context.nets.at(net_id).domain == FastStaNetDomain::kLogic) {
      load_pf += LogicNetLoad(context, context.nets.at(net_id));
    }
  }
  return load_pf;
}

auto logicDriverPi(const FastStaContext& context, FastStaNodeId node_id, std::size_t analysis_index, std::size_t transition_index) -> FastStaPiModel
{
  if (node_id >= context.nodes.size()) {
    return {};
  }
  const auto& output_nets = context.nodes.at(node_id).output_net_ids;
  const auto net_iter = std::ranges::find_if(output_nets, [&](FastStaNetId net_id) -> bool {
    return net_id < context.nets.size() && context.nets.at(net_id).domain == FastStaNetDomain::kLogic && context.nets.at(net_id).parasitic.valid;
  });
  return net_iter == output_nets.end() ? FastStaPiModel{.far_cap_pf = outputLoad(context, node_id)}
                                       : context.nets.at(*net_iter).parasitic.driver_pi_by_timing.at(analysis_index).at(transition_index);
}

auto logicDriverTiming(const FastStaContext& context, const FastStaTimingPoint& point, FastStaTransition transition, const FastStaPiModel& pi)
    -> FastStaDmpDriverResult
{
  const auto has_slew_driver = point.slew_driver_model_index != std::numeric_limits<std::size_t>::max()
                               && point.slew_driver_arc_variant_index != std::numeric_limits<std::size_t>::max();
  const auto driver_model_index = has_slew_driver ? point.slew_driver_model_index : point.driver_model_index;
  const auto driver_arc_variant_index = has_slew_driver ? point.slew_driver_arc_variant_index : point.driver_arc_variant_index;
  const auto driver_input_slew_ns = has_slew_driver ? point.slew_driver_input_slew_ns : point.driver_input_slew_ns;
  const auto driver_is_launch = has_slew_driver ? point.slew_driver_is_launch : point.driver_is_launch;
  const FastStaLibertyArc* model = nullptr;
  const FastStaLibertyCell* cell = nullptr;
  if (driver_is_launch) {
    if (driver_model_index >= context.timing_launches.size()) {
      return {};
    }
    const auto& launch = context.timing_launches.at(driver_model_index);
    const auto cell_iter = context.liberty_cell_by_master.find(launch.cell_master);
    cell = cell_iter == context.liberty_cell_by_master.end() ? nullptr : &cell_iter->second;
    if (cell != nullptr && driver_arc_variant_index < cell->timing_arcs.size()) {
      const auto& candidate = cell->timing_arcs.at(driver_arc_variant_index);
      if (candidate.from_port == launch.clock_port && candidate.to_port == launch.output_port && candidate.edge_triggered
          && candidate.trigger_transition == launch.clock_transition) {
        model = &candidate;
      }
    }
  } else {
    if (driver_model_index >= context.timing_arcs.size()) {
      return {};
    }
    const auto& arc = context.timing_arcs.at(driver_model_index);
    const auto cell_iter = context.liberty_cell_by_master.find(arc.cell_master);
    cell = cell_iter == context.liberty_cell_by_master.end() ? nullptr : &cell_iter->second;
    if (cell != nullptr && driver_arc_variant_index < cell->timing_arcs.size()) {
      const auto& candidate = cell->timing_arcs.at(driver_arc_variant_index);
      if (candidate.from_port == arc.from_port && candidate.to_port == arc.to_port && !candidate.edge_triggered) {
        model = &candidate;
      }
    }
  }
  if (model == nullptr || cell == nullptr) {
    return {};
  }
  return FastStaDmpCeff::calcDriverTiming(*cell, *model, pi, transition, driver_input_slew_ns);
}

}  // namespace

auto ProposedDelta(const std::unordered_map<FastStaNodeId, double>& deltas, FastStaNodeId node_id) -> double
{
  const auto iter = deltas.find(node_id);
  return iter == deltas.end() ? 0.0 : iter->second;
}

namespace {

auto mergeTimingPoint(FastStaTimingPoint& target, const FastStaTimingPoint& candidate, bool early) -> void
{
  if (!candidate.valid) {
    return;
  }
  const auto slew_improves = !target.valid || (early ? candidate.slew_ns < target.slew_ns : candidate.slew_ns > target.slew_ns);
  const auto slew_source = slew_improves ? candidate : target;
  const auto improves = !target.valid || (early ? candidate.arrival_ns < target.arrival_ns : candidate.arrival_ns > target.arrival_ns)
                        || (candidate.arrival_ns == target.arrival_ns && candidate.launch_node_id < target.launch_node_id);
  if (improves) {
    target = candidate;
  }
  target.slew_ns = slew_source.slew_ns;
  target.slew_driver_model_index = slew_source.slew_driver_model_index;
  target.slew_driver_arc_variant_index = slew_source.slew_driver_arc_variant_index;
  target.slew_driver_input_slew_ns = slew_source.slew_driver_input_slew_ns;
  target.slew_driver_is_launch = slew_source.slew_driver_is_launch;
}

auto applySlewState(FastStaTimingPoint& target, const FastStaTimingPoint& source) -> void
{
  if (!target.valid || !source.valid) {
    return;
  }
  target.slew_ns = source.slew_ns;
  target.slew_driver_model_index = source.slew_driver_model_index;
  target.slew_driver_arc_variant_index = source.slew_driver_arc_variant_index;
  target.slew_driver_input_slew_ns = source.slew_driver_input_slew_ns;
  target.slew_driver_is_launch = source.slew_driver_is_launch;
}

}  // namespace

auto TimingSources(const LogicAnalysis& analysis, FastStaNodeId node, std::size_t transition, bool early) -> std::span<const FastStaTimingPoint>
{
  if (analysis.tagged) {
    const auto& points = analysis.tagStates(node, early).at(transition);
    return {points.data(), points.size()};
  }
  const auto& point = analysis.states(node, early).at(transition);
  return {&point, 1U};
}

namespace {

auto mergeLogicPoint(LogicAnalysis& analysis, FastStaNodeId node, FastStaTransition transition, FastStaTimingPoint candidate, bool early) -> void
{
  const auto index = TransitionIndex(transition);
  auto& aggregate = analysis.mutableStates(node, early).at(index);
  mergeTimingPoint(aggregate, candidate, early);
  if (analysis.tagged) {
    auto& states = analysis.mutableTagStates(node, early).at(index);
    const auto found = std::ranges::find_if(states, [&](const auto& state) -> bool { return FastStaEvents::sameTag(state, candidate); });
    if (found == states.end()) {
      states.push_back(std::move(candidate));
    } else {
      mergeTimingPoint(*found, candidate, early);
    }
  }
}

}  // namespace

auto SeedLogicTiming(const FastStaContext& context, const std::unordered_map<FastStaNodeId, double>& clock_deltas, LogicAnalysis& result) -> bool
{
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (!result.includes(node_id) || !node.top_level || !node.input || node.domain == FastStaNodeDomain::kClock) {
      continue;
    }
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      for (const auto early : {true, false}) {
        const auto& existing = result.states(node_id, early).at(TransitionIndex(transition));
        if (existing.valid) {
          continue;
        }
        auto point = FastStaTimingPoint{.launch_node_id = node_id, .valid = true};
        FastStaEvents::startPath(context, point, transition);
        mergeLogicPoint(result, node_id, transition, std::move(point), early);
      }
    }
  }
  // A propagated clock can also be data (for example a feedback divider).
  // Preserve its source event and electrical delay instead of inventing a DFF launch.
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (node.domain != FastStaNodeDomain::kClock || node.clock_inactive || node.case_value.has_value()) {
      continue;
    }
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      for (const auto early : {true, false}) {
        auto point = early ? node.early_timing.at(TransitionIndex(transition)) : node.late_timing.at(TransitionIndex(transition));
        if (!point.valid) {
          continue;
        }
        const auto& origin = context.nodes.at(point.launch_clock_node_id);
        const auto& origin_timing = early ? origin.early_timing.at(TransitionIndex(point.launch_clock_transition))
                                          : origin.late_timing.at(TransitionIndex(point.launch_clock_transition));
        point.launch_clock_arrival_ns = origin_timing.arrival_ns + ProposedDelta(clock_deltas, point.launch_clock_node_id);
        point.arrival_ns += FastStaConstraints::phase(context, point.clock_name, point.launch_clock_transition) + ProposedDelta(clock_deltas, node_id);
        FastStaEvents::startPath(context, point, transition);
        mergeLogicPoint(result, node_id, transition, std::move(point), early);
      }
    }
  }
  for (std::size_t launch_index = 0U; launch_index < context.timing_launches.size(); ++launch_index) {
    const auto& launch = context.timing_launches.at(launch_index);
    if (launch.output_node_id < context.nodes.size() && !result.includes(launch.output_node_id)) {
      continue;
    }
    if (launch.output_node_id < context.nodes.size() && context.nodes.at(launch.output_node_id).domain == FastStaNodeDomain::kClock) {
      continue;
    }
    if (launch.clock_node_id < context.nodes.size()
        && (context.nodes.at(launch.clock_node_id).clock_inactive || context.nodes.at(launch.clock_node_id).domain != FastStaNodeDomain::kClock)) {
      continue;
    }
    if (launch.output_node_id >= context.nodes.size() || launch.clock_node_id >= context.nodes.size() || !context.nodes.at(launch.clock_node_id).timing.valid) {
      result.diagnostic = "launch_clock_arrival_unavailable";
      return false;
    }
    const auto arc_selection = findLaunchArcIndexes(context, launch);
    result.conditional_fallback_arc_count += arc_selection.fallback_count;
    result.conditional_extrema_bundle_count += arc_selection.extrema_bundle_count;
    if (arc_selection.indexes.empty()) {
      result.diagnostic = "launch_arc_model_unavailable:" + launch.cell_master + ":" + launch.clock_port + ":" + launch.output_port;
      return false;
    }
    const auto cell_iter = context.liberty_cell_by_master.find(launch.cell_master);
    if (cell_iter == context.liberty_cell_by_master.end()) {
      result.diagnostic = "launch_cell_model_unavailable:" + launch.cell_master;
      return false;
    }
    for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
      bool transition_valid = false;
      for (const auto arc_index : arc_selection.indexes) {
        for (const auto early : {true, false}) {
          const auto& clock_node = context.nodes.at(launch.clock_node_id);
          const auto& clock_timing = early ? clock_node.early_timing.at(TransitionIndex(launch.clock_transition))
                                           : clock_node.late_timing.at(TransitionIndex(launch.clock_transition));
          if (!clock_timing.valid) {
            continue;
          }
          const auto analysis_index = early ? 0U : 1U;
          const auto driver_pi = logicDriverPi(context, launch.output_node_id, analysis_index, TransitionIndex(transition));
          const auto driver
              = FastStaDmpCeff::calcDriverTiming(cell_iter->second, cell_iter->second.timing_arcs.at(arc_index), driver_pi, transition, clock_timing.slew_ns);
          if (!driver.valid) {
            continue;
          }
          transition_valid = true;
          auto timing = FastStaTimingPoint{
              .arrival_ns = clock_timing.arrival_ns + ProposedDelta(clock_deltas, launch.clock_node_id)
                            + FastStaConstraints::phase(context, clock_timing.clock_name, clock_timing.launch_clock_transition) + driver.gate_delay_ns,
              .slew_ns = driver.driver_slew_ns,
              .launch_node_id = launch.output_node_id,
              .launch_clock_node_id = launch.clock_node_id,
              .launch_clock_transition = clock_timing.launch_clock_transition,
              .launch_clock_arrival_ns = clock_timing.arrival_ns + ProposedDelta(clock_deltas, launch.clock_node_id),
              .driver_model_index = launch_index,
              .driver_arc_variant_index = arc_index,
              .driver_input_slew_ns = clock_timing.slew_ns,
              .slew_driver_model_index = launch_index,
              .slew_driver_arc_variant_index = arc_index,
              .slew_driver_input_slew_ns = clock_timing.slew_ns,
              .stage_input_node_id = launch.clock_node_id,
              .stage_input_transition = launch.clock_transition,
              .stage_input_arrival_ns = clock_timing.arrival_ns,
              .stage_input_slew_ns = clock_timing.slew_ns,
              .stage_delay_ns = driver.gate_delay_ns,
              .driver_is_launch = true,
              .slew_driver_is_launch = true,
              .valid = true,
              .clock_name = clock_timing.clock_name,
          };
          FastStaEvents::startPath(context, timing, transition);
          mergeLogicPoint(result, launch.output_node_id, transition, std::move(timing), early);
        }
      }
      if (!transition_valid) {
        result.diagnostic = "launch_arc_table_unavailable:" + launch.cell_master;
        return false;
      }
    }
  }
  return true;
}

auto SeedLogicRootSlew(const FastStaContext& context, const std::vector<std::size_t>& indegree, LogicAnalysis& result) -> void
{
  std::vector<bool> launch_outputs(context.nodes.size(), false);
  for (const auto& launch : context.timing_launches) {
    if (launch.output_node_id < launch_outputs.size()) {
      launch_outputs.at(launch.output_node_id) = true;
    }
  }
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto& node = context.nodes.at(node_id);
    if (!result.includes(node_id) || node.domain != FastStaNodeDomain::kLogic || indegree.at(node_id) != 0U || launch_outputs.at(node_id)
        || node.case_value.has_value()) {
      continue;
    }
    for (std::size_t transition_index = 0U; transition_index < 2U; ++transition_index) {
      result.mutableStates(node_id, true).at(transition_index) = FastStaTimingPoint{
          .arrival_ns = node.arrival_seed_early_ns,
          .slew_ns = std::max(0.0, node.slew_seed_early_ns),
          .launch_node_id = node_id,
          .valid = true,
      };
      result.mutableStates(node_id, false).at(transition_index) = FastStaTimingPoint{
          .arrival_ns = node.arrival_seed_late_ns,
          .slew_ns = std::max(0.0, node.slew_seed_late_ns),
          .launch_node_id = node_id,
          .valid = true,
      };
    }
  }
}

auto PropagateLogicTiming(const FastStaContext& context, const LogicTraversal& traversal, LogicAnalysis& result, LogicResponseCache* response_cache,
                          const LogicAnalysis* slew_analysis) -> bool
{
  LogicResponseCache local_response_cache;
  if (response_cache == nullptr) {
    response_cache = &local_response_cache;
  }
  if (response_cache->cell_responses.size() != context.timing_arcs.size() || response_cache->net_drivers.size() != context.nets.size()
      || response_cache->net_loads.size() != context.nets.size()) {
    response_cache->cell_responses.resize(context.timing_arcs.size());
    response_cache->net_drivers.resize(context.nets.size());
    response_cache->net_loads.resize(context.nets.size());
    for (FastStaNetId net_id = 0U; net_id < context.nets.size(); ++net_id) {
      const auto& net = context.nets.at(net_id);
      const auto needed = std::ranges::any_of(net.load_node_ids, [&](const auto node_id) -> bool { return result.includes(node_id); });
      const auto load_count = needed ? net.load_node_ids.size() : 0U;
      for (std::size_t analysis_index = 0U; analysis_index < 2U; ++analysis_index) {
        for (std::size_t transition_index = 0U; transition_index < 2U; ++transition_index) {
          response_cache->net_loads.at(net_id).at(analysis_index).at(transition_index).resize(load_count);
        }
      }
    }
  }
  std::vector<std::mutex> net_locks(context.nets.size());
  std::atomic<std::size_t> conditional_fallback_count{0U};
  std::atomic<std::size_t> conditional_extrema_count{0U};
  std::mutex diagnostic_lock;
  const auto fail = [&](std::string diagnostic) -> bool {
    const std::scoped_lock guard(diagnostic_lock);
    if (result.diagnostic.empty()) {
      result.diagnostic = std::move(diagnostic);
    }
    return false;
  };
  const auto propagate_target = [&](FastStaNodeId target_node_id) -> bool {
    if (context.nodes.at(target_node_id).case_value.has_value()) {
      return true;
    }
    if (!result.includes(target_node_id)) {
      return true;
    }
    for (const auto& [node_id, edge] : traversal.incoming.at(target_node_id)) {
      if (edge.net) {
        const auto& net = context.nets.at(edge.model_index);
        for (const auto transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
          const auto index = TransitionIndex(transition);
          for (const auto early : {true, false}) {
            const auto analysis_index = early ? 0U : 1U;
            for (const auto& source : TimingSources(result, node_id, index, early)) {
              if (!source.valid) {
                continue;
              }
              const auto& driver_pi = net.parasitic.driver_pi_by_timing.at(analysis_index).at(index);
              auto& cached_driver = response_cache->net_drivers.at(edge.model_index).at(analysis_index).at(index);
              {
                const std::scoped_lock driver_guard(net_locks.at(edge.model_index));
                if (!cached_driver.has_value()) {
                  cached_driver = logicDriverTiming(context, source, transition, driver_pi);
                }
              }
              const auto& driver = *cached_driver;
              const auto rc_node_id = edge.load_index < net.load_rc_node_ids.size() ? net.load_rc_node_ids.at(edge.load_index) : kInvalidFastStaRcNodeId;
              const auto delay_ns = FindRcTerminalElmore(net.parasitic, rc_node_id, analysis_index, index);
              auto& cached_loads = response_cache->net_loads.at(edge.model_index).at(analysis_index).at(index);
              auto& load_timing = cached_loads.at(edge.load_index);
              if (!load_timing.valid) {
                if (driver.valid) {
                  load_timing = FastStaDmpCeff::calcLoadDelaySlew(driver, delay_ns, FindLoadLibertyCell(context, edge.to_node_id));
                } else if (source.slew_driver_model_index == std::numeric_limits<std::size_t>::max()
                           && source.driver_model_index == std::numeric_limits<std::size_t>::max()) {
                  load_timing = FastStaDmpCeff::calcInputPortDelaySlew(source.slew_ns, delay_ns, transition, FindLoadLibertyCell(context, edge.to_node_id));
                } else {
                  return fail("logic_driver_model_unavailable:" + context.nodes.at(node_id).name);
                }
              }
              if (!load_timing.valid) {
                return fail("logic_load_timing_unavailable:" + context.nodes.at(edge.to_node_id).name);
              }
              auto candidate = source;
              candidate.arrival_ns += load_timing.wire_delay_ns;
              candidate.slew_ns = load_timing.load_slew_ns;
              candidate.stage_input_node_id = node_id;
              candidate.stage_input_transition = transition;
              candidate.stage_input_arrival_ns = source.arrival_ns;
              candidate.stage_input_slew_ns = source.slew_ns;
              candidate.stage_delay_ns = load_timing.wire_delay_ns;
              mergeLogicPoint(result, edge.to_node_id, transition, std::move(candidate), early);
            }
          }
        }
        continue;
      }
      const auto& timing_arc = context.timing_arcs.at(edge.model_index);
      const auto arc_selection = findLogicArcIndexes(context, timing_arc);
      conditional_fallback_count.fetch_add(arc_selection.fallback_count, std::memory_order_relaxed);
      conditional_extrema_count.fetch_add(arc_selection.extrema_bundle_count, std::memory_order_relaxed);
      if (arc_selection.indexes.empty()) {
        continue;
      }
      const auto cell_iter = context.liberty_cell_by_master.find(timing_arc.cell_master);
      if (cell_iter == context.liberty_cell_by_master.end()) {
        return fail("logic_driver_cell_unavailable:" + timing_arc.cell_master);
      }
      for (const auto arc_index : arc_selection.indexes) {
        const auto& model = cell_iter->second.timing_arcs.at(arc_index);
        for (const auto input_transition : {FastStaTransition::kRise, FastStaTransition::kFall}) {
          std::array<FastStaTransition, 2U> outputs{input_transition, input_transition};
          auto output_count = 1U;
          const bool timing_arc_negative_only = timing_arc.negative_unate && !timing_arc.positive_unate;
          if (model.negative_unate || (!model.positive_unate && timing_arc_negative_only)) {
            outputs.front() = OppositeTransition(input_transition);
          } else {
            const bool timing_arc_positive_only = timing_arc.positive_unate && !timing_arc.negative_unate;
            if (model.positive_unate || timing_arc_positive_only) {
              output_count = 1U;
            } else {
              outputs = {FastStaTransition::kRise, FastStaTransition::kFall};
              output_count = 2U;
            }
          }
          for (std::size_t output_index = 0U; output_index < output_count; ++output_index) {
            const auto output_transition = outputs.at(output_index);
            for (const auto early : {true, false}) {
              const auto analysis_index = early ? 0U : 1U;
              for (const auto& source : TimingSources(result, node_id, TransitionIndex(input_transition), early)) {
                if (!source.valid) {
                  continue;
                }
                const auto driver_pi = logicDriverPi(context, edge.to_node_id, analysis_index, TransitionIndex(output_transition));
                auto& cached_responses = response_cache->cell_responses.at(edge.model_index);
                const auto response_iter = std::ranges::find_if(cached_responses, [&](const CellResponse& response) -> bool {
                  return response.arc_variant_index == arc_index && response.input_transition == input_transition
                         && response.output_transition == output_transition && response.analysis_index == analysis_index;
                });
                double gate_delay_ns = 0.0;
                double driver_slew_ns = 0.0;
                bool driver_valid = false;
                FastStaDmpDriverResult computed_driver;
                if (response_iter != cached_responses.end()) {
                  gate_delay_ns = response_iter->gate_delay_ns;
                  driver_slew_ns = response_iter->driver_slew_ns;
                  driver_valid = response_iter->valid;
                } else {
                  computed_driver = FastStaDmpCeff::calcDriverTiming(cell_iter->second, model, driver_pi, output_transition, source.slew_ns);
                  gate_delay_ns = computed_driver.gate_delay_ns;
                  driver_slew_ns = computed_driver.driver_slew_ns;
                  driver_valid = computed_driver.valid;
                  cached_responses.push_back(CellResponse{.arc_variant_index = arc_index,
                                                          .input_transition = input_transition,
                                                          .output_transition = output_transition,
                                                          .analysis_index = analysis_index,
                                                          .gate_delay_ns = gate_delay_ns,
                                                          .driver_slew_ns = driver_slew_ns,
                                                          .valid = driver_valid});
                }
                if (!driver_valid) {
                  continue;
                }
                auto candidate = source;
                candidate.arrival_ns += gate_delay_ns;
                candidate.slew_ns = driver_slew_ns;
                candidate.driver_model_index = edge.model_index;
                candidate.driver_arc_variant_index = arc_index;
                candidate.driver_input_slew_ns = source.slew_ns;
                candidate.slew_driver_model_index = edge.model_index;
                candidate.slew_driver_arc_variant_index = arc_index;
                candidate.slew_driver_input_slew_ns = source.slew_ns;
                candidate.stage_input_node_id = node_id;
                candidate.stage_input_transition = input_transition;
                candidate.stage_input_arrival_ns = source.arrival_ns;
                candidate.stage_input_slew_ns = source.slew_ns;
                candidate.stage_delay_ns = gate_delay_ns;
                candidate.driver_is_launch = false;
                candidate.slew_driver_is_launch = false;
                auto& target = result.mutableStates(edge.to_node_id, early).at(TransitionIndex(output_transition));
                const auto slew_improves = !target.valid || (early ? candidate.slew_ns < target.slew_ns : candidate.slew_ns > target.slew_ns);
                if (slew_improves && computed_driver.valid) {
                  for (const auto net_id : context.nodes.at(edge.to_node_id).output_net_ids) {
                    if (net_id < context.nets.size() && context.nets.at(net_id).domain == FastStaNetDomain::kLogic) {
                      response_cache->net_drivers.at(net_id).at(analysis_index).at(TransitionIndex(output_transition)) = computed_driver;
                    }
                  }
                }
                mergeLogicPoint(result, edge.to_node_id, output_transition, std::move(candidate), early);
              }
            }
          }
        }
      }
    }
    if (slew_analysis != nullptr) {
      for (std::size_t transition_index = 0U; transition_index < 2U; ++transition_index) {
        applySlewState(result.mutableStates(target_node_id, true).at(transition_index), slew_analysis->states(target_node_id, true).at(transition_index));
        applySlewState(result.mutableStates(target_node_id, false).at(transition_index), slew_analysis->states(target_node_id, false).at(transition_index));
        if (result.tagged) {
          for (auto& state : result.mutableTagStates(target_node_id, true).at(transition_index)) {
            applySlewState(state, slew_analysis->states(target_node_id, true).at(transition_index));
          }
          for (auto& state : result.mutableTagStates(target_node_id, false).at(transition_index)) {
            applySlewState(state, slew_analysis->states(target_node_id, false).at(transition_index));
          }
        }
      }
    }
    return true;
  };
  const auto worker_count = static_cast<int>(std::min<std::size_t>(context.worker_count, std::numeric_limits<int>::max()));
  for (const auto& level_nodes : traversal.nodes_by_level) {
    std::vector<FastStaNodeId> affected_level;
    for (const auto id : level_nodes) {
      if (result.includes(id) && !context.nodes.at(id).case_value.has_value()) {
        affected_level.push_back(id);
      }
    }
    if (affected_level.empty()) {
      continue;
    }
    result.visited_logic_node_count += affected_level.size();
    std::atomic<bool> level_complete{true};
#pragma omp parallel for schedule(static) num_threads(worker_count)
    for (auto index = std::ptrdiff_t{0}; index < std::ssize(affected_level); ++index) {
      if (!propagate_target(affected_level.at(static_cast<std::size_t>(index)))) {
        level_complete.store(false, std::memory_order_relaxed);
      }
    }
    if (!level_complete.load(std::memory_order_relaxed)) {
      return false;
    }
  }
  result.conditional_fallback_arc_count += conditional_fallback_count.load(std::memory_order_relaxed);
  result.conditional_extrema_bundle_count += conditional_extrema_count.load(std::memory_order_relaxed);
  return true;
}

}  // namespace icts::fast_sta
