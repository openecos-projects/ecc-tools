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
#include "GraphConnectivityBuilder.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Logger.hpp"

namespace ista {
void GraphConnectivityBuilder::build()
{
  buildTimingPointList();
  _pin_direction_resolver.normalize();
  // Direction inference needs temporary cell arcs. Rebuild them in the same
  // order once pin roles are resolved, before assigning final vertex bindings.
  buildCellArcs();
  _pin_direction_resolver.resolve();
  rebuildCellArcListByPinDirection();
  buildNetDriverLoadList();
  buildNetArcs();
  buildGraphVertexList();
}

void GraphConnectivityBuilder::buildTimingPointList()
{
  Database& database = _database;
  for (std::pair<const std::string, Pin>& pin_pair : database.network().pins()) {
    database.graph().points()[pin_pair.first] = TimingPoint();
  }
}

void GraphConnectivityBuilder::buildCellArcs()
{
  Database& database = _database;
  for (auto& [instance_name, instance] : database.network().instances()) {
    if (buildLibraryCellArcs(instance)) {
      continue;
    }
    std::vector<std::string> input_pin_list = collectInputPins(instance);
    std::vector<std::string> output_pin_list = collectOutputPins(instance);
    if (input_pin_list.empty() || output_pin_list.empty()) {
      continue;
    }
    for (std::string& input_pin : input_pin_list) {
      for (std::string& output_pin : output_pin_list) {
        if (input_pin == output_pin) {
          continue;
        }
        addArc(input_pin, output_pin, ArcType::kCell, instance_name);
      }
    }
  }
}

bool GraphConnectivityBuilder::buildLibraryCellArcs(Instance& instance)
{
  Database& database = _database;
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return false;
  }

  LibertyCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_cell_arc_list().empty()) {
    return false;
  }

  auto& cell_arcs = timing_cell.get_cell_arc_list();
  for (std::size_t index = 0; index < cell_arcs.size(); ++index) {
    TimingCellArc& timing_cell_arc = cell_arcs[index];
    if (!timing_cell_arc.get_is_timing_graph_arc()) {
      continue;
    }
    addCellArc(instance, timing_cell_arc, index);
  }
  return true;
}

std::string GraphConnectivityBuilder::getInstancePinName(Instance& instance, std::string& port_name)
{
  return instance.get_instance_name() + ":" + port_name;
}

void GraphConnectivityBuilder::addCellArc(Instance& instance, TimingCellArc& timing_cell_arc, std::size_t liberty_arc_index)
{
  Database& database = _database;
  std::string source_pin = getInstancePinName(instance, timing_cell_arc.get_source_port());
  std::string sink_pin = getInstancePinName(instance, timing_cell_arc.get_sink_port());
  if (database.network().pins().count(source_pin) == 0 || database.network().pins().count(sink_pin) == 0) {
    return;
  }
  bool is_disable_arc = timing_cell_arc.get_is_disable_arc();
  addArc(source_pin, sink_pin, ArcType::kCell, instance.get_instance_name(), timing_cell_arc.get_source_port(), timing_cell_arc.get_sink_port(),
         timing_cell_arc.get_is_clock_arc(), is_disable_arc, LibertyArcId{instance.get_cell_name(), liberty_arc_index});
}

void GraphConnectivityBuilder::addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name,
                                      const std::string& library_source_port, const std::string& library_sink_port, bool is_clock_arc, bool is_disable_arc,
                                      LibertyArcId liberty_arc_id)
{
  Database& database = _database;
  Arc arc;
  arc.set_arc_name(owner_name + ":" + source_pin + "->" + sink_pin);
  arc.set_source_pin(source_pin);
  arc.set_sink_pin(sink_pin);
  arc.set_owner_name(owner_name);
  arc.set_library_source_port(library_source_port);
  arc.set_library_sink_port(library_sink_port);
  arc.set_type(type);
  arc.set_is_clock_arc(is_clock_arc);
  arc.set_is_disable_arc(is_disable_arc);
  arc.setLibertyArcId(std::move(liberty_arc_id));

  database.graph().arcs().push_back(arc);
  const std::size_t arc_idx = database.graph().arcs().size() - 1;
  database.graph().outgoingArcs()[source_pin].push_back(arc_idx);
  database.graph().incomingArcs()[sink_pin].push_back(arc_idx);
}

std::vector<std::string> GraphConnectivityBuilder::collectInputPins(Instance& instance)
{
  Database& database = _database;
  std::vector<std::string> input_pin_list;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (isInputLike(database.network().pins()[pin_name].get_direction())) {
      input_pin_list.push_back(pin_name);
    }
  }
  return input_pin_list;
}

bool GraphConnectivityBuilder::isInputLike(PinDirection direction)
{
  return direction == PinDirection::kInput || direction == PinDirection::kInout;
}

std::vector<std::string> GraphConnectivityBuilder::collectOutputPins(Instance& instance)
{
  Database& database = _database;
  std::vector<std::string> output_pin_list;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (isOutputLike(database.network().pins()[pin_name].get_direction())) {
      output_pin_list.push_back(pin_name);
    }
  }
  return output_pin_list;
}

bool GraphConnectivityBuilder::isOutputLike(PinDirection direction)
{
  return direction == PinDirection::kOutput || direction == PinDirection::kInout;
}

void GraphConnectivityBuilder::rebuildCellArcListByPinDirection()
{
  Database& database = _database;
  database.graph().arcs().clear();
  database.graph().outgoingArcs().clear();
  database.graph().incomingArcs().clear();
  buildCellArcs();
}

void GraphConnectivityBuilder::buildNetDriverLoadList()
{
  Database& database = _database;
  for (std::pair<const std::string, Net>& net_pair : database.network().nets()) {
    makeNetDriverLoad(net_pair.second);
  }
}

void GraphConnectivityBuilder::makeNetDriverLoad(Net& net)
{
  Database& database = _database;
  net.get_driver_pin().clear();
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.network().pins()[pin_name];
    if (isDriverPin(pin)) {
      net.get_driver_pin_list().push_back(pin_name);
    }
    if (!isDriverPin(pin) || pin.get_direction() == PinDirection::kInout) {
      net.get_load_pin_list().push_back(pin_name);
    }
  }

  if (net.get_driver_pin_list().size() == 1) {
    net.set_driver_pin(net.get_driver_pin_list().front());
  }
  int32_t unconditional_driver_num = 0;
  for (std::string& driver : net.get_driver_pin_list()) {
    Pin& pin = database.network().pins()[driver];
    LibertyPort* port = _pin_direction_resolver.libertyPort(pin);
    if (pin.get_direction() != PinDirection::kInout && (port == nullptr || port->get_three_state_expression().get_is_empty())) {
      ++unconditional_driver_num;
    }
  }
  if (unconditional_driver_num > 1) {
    STALOG.error(Loc::current(), "The net has multiple driver pins: net=", net.get_net_name(),
                 " drivers=", _pin_direction_resolver.getPinNameListString(net.get_driver_pin_list()));
  }
}

bool GraphConnectivityBuilder::isDriverPin(Pin& pin)
{
  return pin.get_direction() == PinDirection::kInout || _pin_direction_resolver.isDriverDirection(pin, pin.get_direction());
}

void GraphConnectivityBuilder::buildNetArcs()
{
  Database& database = _database;
  for (auto& [net_name, net] : database.network().nets()) {
    for (std::string& driver_pin : net.get_driver_pin_list()) {
      for (std::string& load_pin : net.get_load_pin_list()) {
        if (load_pin != driver_pin) {
          addArc(driver_pin, load_pin, ArcType::kNet, net_name);
        }
      }
    }
  }
}

void GraphConnectivityBuilder::buildGraphVertexList()
{
  Database& database = _database;
  database.graph().vertices().clear();
  database.graph().driverPoints().clear();
  for (auto& [pin_name, pin] : database.network().pins()) {
    int32_t idx = addGraphVertex(pin_name, isDriverPin(pin), database.graph().points()[pin_name]);
    pin.set_load_vertex_idx(idx);
    pin.set_driver_vertex_idx(idx);
  }
  for (auto& [pin_name, pin] : database.network().pins()) {
    if (pin.get_direction() == PinDirection::kInout) {
      database.graph().vertices()[pin.get_load_vertex_idx()].set_is_driver(false);
      pin.set_driver_vertex_idx(addGraphVertex(pin_name, true, database.graph().driverPoints()[pin_name]));
    }
  }
  for (std::size_t idx = 0; idx < database.graph().arcs().size(); ++idx) {
    Arc& arc = database.graph().arcs()[idx];
    Pin& source = database.network().pins()[arc.get_source_pin()];
    Pin& sink = database.network().pins()[arc.get_sink_pin()];
    arc.set_source_vertex_idx(arc.get_type() == ArcType::kNet ? source.get_driver_vertex_idx() : source.get_load_vertex_idx());
    arc.set_sink_vertex_idx(arc.get_type() == ArcType::kNet ? sink.get_load_vertex_idx() : sink.get_driver_vertex_idx());
    database.graph().vertices()[arc.get_source_vertex_idx()].get_outgoing_arc_list().push_back(idx);
    database.graph().vertices()[arc.get_sink_vertex_idx()].get_incoming_arc_list().push_back(idx);
  }
}

int32_t GraphConnectivityBuilder::addGraphVertex(const std::string& pin_name, bool is_driver, TimingPoint& point)
{
  Database& database = _database;
  GraphVertex vertex;
  vertex.set_pin_name(pin_name);
  vertex.set_is_driver(is_driver);
  vertex.set_timing_point(&point);
  database.graph().vertices().push_back(vertex);
  return static_cast<int32_t>(database.graph().vertices().size()) - 1;
}

void GraphConnectivityBuilder::addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name)
{
  Database& database = _database;
  Arc arc;
  arc.set_arc_name(owner_name + ":" + source_pin + "->" + sink_pin);
  arc.set_source_pin(source_pin);
  arc.set_sink_pin(sink_pin);
  arc.set_owner_name(owner_name);
  arc.set_type(type);
  arc.set_is_disable_arc(shouldDisableNetArc(source_pin, sink_pin));

  database.graph().arcs().push_back(arc);
  const std::size_t arc_idx = database.graph().arcs().size() - 1;
  database.graph().outgoingArcs()[source_pin].push_back(arc_idx);
  database.graph().incomingArcs()[sink_pin].push_back(arc_idx);
}

bool GraphConnectivityBuilder::shouldDisableNetArc(const std::string& source_pin, const std::string& sink_pin)
{
  Database& database = _database;
  Pin& source_pin_inst = database.network().pins()[source_pin];
  Pin& sink_pin_inst = database.network().pins()[sink_pin];
  if (source_pin_inst.get_is_port() || sink_pin_inst.get_is_port()) {
    return false;
  }
  if (database.network().instances().count(source_pin_inst.get_instance_name()) == 0
      || database.network().instances().count(sink_pin_inst.get_instance_name()) == 0) {
    return false;
  }
  Instance& source_instance = database.network().instances()[source_pin_inst.get_instance_name()];
  Instance& sink_instance = database.network().instances()[sink_pin_inst.get_instance_name()];
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (timing_cell_map.count(source_instance.get_cell_name()) == 0) {
    return false;
  }
  LibertyCell& source_cell = timing_cell_map[source_instance.get_cell_name()];
  return source_cell.get_is_sequential() && !source_cell.get_is_clock_gating() && !source_cell.get_is_macro() && sink_pin == sink_instance.get_clock_pin_name();
}

bool GraphConnectivityBuilder::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}
}  // namespace ista
