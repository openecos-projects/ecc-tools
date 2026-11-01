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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "ClockNetworkBuilder.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "Logger.hpp"
namespace ista {
namespace {
bool isDisableArc(const Database& database, const Arc& arc)
{
  const auto* cell_arc = arc.libertyArcId() ? database.liberty().findCellArc(*arc.libertyArcId()) : nullptr;
  return arc.get_is_disable_arc() || arc.get_is_loop_disable() || (cell_arc && cell_arc->isThreeStateArc());
}
}  // namespace
std::vector<ClockSource> ClockNetworkBuilder::build()
{
  auto measurement = _database.timingExecutionStats().measure(TimingPhase::kClockNetwork);
  std::vector<ClockSource> sources;
  orderSources(sources);
  markClockPoints(sources);
  return sources;
}
void ClockNetworkBuilder::orderSources(std::vector<ClockSource>& sources)
{
  Database& database = _database;
  auto& clock_map = database.sdc().get_clock_map();
  std::set<std::string> visiting;
  std::set<std::string> visited;
  std::function<void(const std::string&)> append_clock = [&](const std::string& name) {
    if (visited.contains(name))
      return;
    if (visiting.contains(name)) {
      STALOG.warn(Loc::current(), "generated clock dependency cycle at '", name, "'");
      return;
    }
    auto clock_iter = clock_map.find(name);
    if (clock_iter == clock_map.end())
      return;
    visiting.insert(name);
    const TimingClock& clock = clock_iter->second;
    if (clock.get_is_generated())
      append_clock(clock.get_master_clock_name());
    visiting.erase(name);
    visited.insert(name);
    sources.emplace_back(name, clock.get_source_list(), clock.get_is_propagated());
  };
  for (const auto& [name, clock] : clock_map)
    append_clock(name);
}

void ClockNetworkBuilder::markClockPoints(std::vector<ClockSource>& sources)
{
  for (auto& clock : sources) {
    markClock(clock);
  }
}

void ClockNetworkBuilder::markClock(ClockSource& clock)
{
  Database& database = _database;
  std::queue<int32_t> pin_queue;
  for (const auto& clock_source : clock.get_source_list()) {
    if (!database.graph().points().contains(clock_source)) {
      STALOG.warn(Loc::current(), "clock '", clock.get_clock_name(), "' has no source");
      continue;
    }
    pin_queue.push(database.network().pins().at(clock_source).get_driver_vertex_idx());
  }

  while (!pin_queue.empty()) {
    int32_t vertex_idx = pin_queue.front();
    GraphVertex& vertex = database.graph().vertices()[vertex_idx];
    std::string& pin_name = vertex.get_pin_name();
    pin_queue.pop();

    Pin& pin = database.network().pins().at(pin_name);
    // Multiple exported clock modes have no internal data fanout to analyze.
    if (pin.get_is_port() && pin.get_direction() == PinDirection::kOutput && vertex.get_outgoing_arc_list().empty()) {
      continue;
    }
    bool another_root = false;
    for (auto& [name, definition] : database.sdc().get_clock_map()) {
      if (name != clock.get_clock_name()
          && std::find(definition.get_source_objects().begin(), definition.get_source_objects().end(), pin_name) != definition.get_source_objects().end()
          && std::find(clock.get_source_list().begin(), clock.get_source_list().end(), pin_name) == clock.get_source_list().end()) {
        another_root = true;
        break;
      }
    }
    if (another_root) {
      continue;
    }
    TimingPoint& timing_point = vertex.get_timing_point();
    if (timing_point.has_clock_state(clock.get_clock_name())) {
      continue;
    }

    timing_point.set_is_clock_point(true);
    timing_point.get_clock_state(clock.get_clock_name());
    if (timing_point.defaultClock().empty()) {
      timing_point.setDefaultClock(clock.get_clock_name());
      timing_point.setLaunchClock(clock.get_clock_name());
    }
    clock.add_clock_pin(pin_name);

    if (stopsAt(pin_name)) {
      continue;
    }
    for (const auto& arc_idx : vertex.get_outgoing_arc_list()) {
      Arc& arc = database.graph().arcs()[arc_idx];
      if (isDisableArc(database, arc)
          || (arc.get_type() == ArcType::kNet
              && !database.sdc().clockAllowsNetArc(database.network(), std::string(clock.get_clock_name()), arc.get_source_pin(), arc.get_sink_pin()))) {
        continue;
      }
      pin_queue.push(arc.get_sink_vertex_idx());
    }
  }
}

bool ClockNetworkBuilder::stopsAt(const std::string& pin_name)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[pin_name];
  if (pin.get_is_port() || !database.network().instances().contains(pin.get_instance_name())) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_is_clock_gating() && pin_name == instance.get_clock_pin_name();
}

}  // namespace ista
