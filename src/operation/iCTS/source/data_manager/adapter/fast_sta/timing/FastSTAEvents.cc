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
 * @file FastSTAEvents.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Exact path eligibility and conservative, CRPR-disabled clock-event checks.
 */
#include "FastSTAEvents.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "FastSTAConstraints.hh"

namespace icts {
namespace {

auto delta(const std::unordered_map<FastStaNodeId, double>& deltas, FastStaNodeId node) -> double
{
  const auto found = deltas.find(node);
  return found == deltas.end() ? 0.0 : found->second;
}

// A Euclidean clock-period lattice avoids enumerating the common hyperperiod.
// Scale-relative machine precision handles floating-point cancellation, not a
// physical timing guard band; the final signed slack is never clamped.
auto periodLattice(double launch, double capture) -> double
{
  auto larger = std::max(launch, capture);
  auto smaller = std::min(launch, capture);
  const auto cancellation = 16.0 * std::numeric_limits<double>::epsilon() * larger;
  while (smaller > cancellation) {
    const auto remainder = std::fmod(larger, smaller);
    larger = smaller;
    smaller = std::min(remainder, std::abs(smaller - remainder));
  }
  return larger;
}

}  // namespace

auto FastStaEvents::startPath(const FastStaContext& context, FastStaTimingPoint& point, FastStaTransition transition) -> void
{
  (void) context;
  point.launch_data_transition = transition;
}

auto FastStaEvents::sameTag(const FastStaTimingPoint& lhs, const FastStaTimingPoint& rhs) -> bool
{
  return lhs.clock_name == rhs.clock_name && lhs.launch_clock_transition == rhs.launch_clock_transition;
}

auto FastStaEvents::relation(const FastStaContext& context, const FastStaTimingCheck& check, FastStaTransition transition, const FastStaTimingPoint& data,
                             const FastStaTimingPoint& capture, double requirement_ns, const std::unordered_map<FastStaNodeId, double>& clock_deltas)
    -> std::optional<FastStaTimingRelationFact>
{
  const auto setup = check.kind == FastStaTimingCheckKind::kSetup;
  const auto launch_period = FastStaConstraints::period(context, data.clock_name);
  const auto capture_period = FastStaConstraints::period(context, capture.clock_name);
  const auto launch_phase = FastStaConstraints::phase(context, data.clock_name, data.launch_clock_transition);
  const auto capture_phase = FastStaConstraints::phase(context, capture.clock_name, capture.launch_clock_transition);
  const auto lattice = periodLattice(launch_period, capture_period);
  auto separation = std::fmod(capture_phase - launch_phase, lattice);
  if (separation <= 16.0 * std::numeric_limits<double>::epsilon() * lattice) {
    separation += lattice;
  }
  if (!setup) {
    separation -= lattice;
  }
  // Select the earliest non-negative pair on both actual event trains.  This
  // retains the launch-cycle shift for generated and inverted checks, instead
  // of reporting equivalent slack at a fictitious negative capture event.
  auto capture_cycle = std::max(0.0, std::ceil((launch_phase + separation - capture_phase) / capture_period - 16.0 * std::numeric_limits<double>::epsilon()));
  auto capture_event = capture_phase + capture_cycle * capture_period;
  auto launch_event = capture_event - separation;
  const auto maximum_pairings = std::max(1.0, std::round(launch_period / lattice));
  for (std::size_t pairing = 0U; static_cast<double>(pairing) < maximum_pairings; ++pairing) {
    const auto launch_cycles = (launch_event - launch_phase) / launch_period;
    if (std::abs(launch_cycles - std::round(launch_cycles)) <= 32.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(launch_cycles))) {
      break;
    }
    capture_event += capture_period;
    launch_event = capture_event - separation;
  }
  auto arrival = data.arrival_ns + launch_event - launch_phase;
  auto required = launch_event + separation + capture.arrival_ns + delta(clock_deltas, check.clock_node_id) + (setup ? -requirement_ns : requirement_ns);
  const auto& launch_name = context.nodes.at(data.launch_node_id).name;
  const auto& endpoint_name = context.nodes.at(check.data_node_id).name;
  const auto edge_name = [](FastStaTransition edge) -> const char* { return edge == FastStaTransition::kRise ? "rise" : "fall"; };
  auto identity = data.clock_name + "|" + capture.clock_name + "|" + launch_name + "|" + endpoint_name + "|" + (check.clock_gating ? "gating_" : "")
                  + (setup ? "setup" : "hold") + "|" + edge_name(transition) + "|" + edge_name(data.launch_clock_transition) + "|"
                  + edge_name(capture.launch_clock_transition);
  return FastStaTimingRelationFact{.relation_id = std::move(identity),
                                   .launch_pin_name = launch_name,
                                   .capture_pin_name = endpoint_name,
                                   .launch_node_id = data.launch_node_id,
                                   .capture_node_id = check.data_node_id,
                                   .launch_clock_node_id = data.launch_clock_node_id,
                                   .capture_clock_node_id = check.clock_node_id,
                                   .check = check.kind,
                                   .data_transition = transition,
                                   .launch_clock_transition = data.launch_clock_transition,
                                   .capture_clock_transition = capture.launch_clock_transition,
                                   .slack_ns = setup ? required - arrival : arrival - required,
                                   .uncertainty = {.numerical_ns = 1.0e-12},
                                   .launch_clock_name = data.clock_name,
                                   .capture_clock_name = capture.clock_name,
                                   .arrival_ns = arrival,
                                   .required_ns = required,
                                   .requirement_ns = requirement_ns,
                                   .clock_gating = check.clock_gating};
}

}  // namespace icts
