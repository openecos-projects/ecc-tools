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
#include "GraphBuilder.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "TimingCaseAnalysis.hpp"
#include "Utility.hpp"

namespace ista {

// public

void GraphBuilder::initInst()
{
  if (_gb_instance == nullptr) {
    _gb_instance = new GraphBuilder();
  }
}

GraphBuilder& GraphBuilder::getInst()
{
  if (_gb_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_gb_instance;
}

void GraphBuilder::destroyInst()
{
  if (_gb_instance != nullptr) {
    delete _gb_instance;
    _gb_instance = nullptr;
  }
}

// function

void GraphBuilder::build()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  buildTimingPointList();
  normalizePinDirectionByTimingCell();
  buildCellArcs();
  buildInoutPinDirectionByGraph();
  buildNetDriverLoadList();
  buildNetArcs();
  buildGraphVertexList();
  TimingCaseAnalysis::apply(STADM.getDatabase());
  buildStartEndPointList();
  breakLoopArcList();
  buildTimingOrder();
  printLoopInfo();
  initializeArcTiming();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

GraphBuilder* GraphBuilder::_gb_instance = nullptr;

void GraphBuilder::buildTimingPointList()
{
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    database.get_timing_point_map()[pin_pair.first] = TimingPoint();
  }
}

void GraphBuilder::normalizePinDirectionByTimingCell()
{
  Database& database = STADM.getDatabase();
  for (auto& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port()) {
      continue;
    }
    TimingCellPort* port = getTimingCellPort(pin);
    PinDirection timing_cell_direction
        = port != nullptr && port->get_is_input() && port->get_is_output() ? PinDirection::kInout : inferInoutPinDirectionByTimingCell(pin);
    if (timing_cell_direction != PinDirection::kNone) {
      // Liberty defines the signal roles, including a genuine bidirectional port.
      pin.set_direction(timing_cell_direction);
    }
  }
}

void GraphBuilder::buildCellArcs()
{
  Database& database = STADM.getDatabase();
  for (auto& [instance_name, instance] : database.get_instance_map()) {
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

bool GraphBuilder::buildLibraryCellArcs(Instance& instance)
{
  Database& database = STADM.getDatabase();
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return false;
  }

  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_cell_arc_list().empty()) {
    return false;
  }

  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (!timing_cell_arc.get_is_timing_graph_arc()) {
      continue;
    }
    addCellArc(instance, timing_cell_arc);
  }
  return true;
}

std::string GraphBuilder::getInstancePinName(Instance& instance, std::string& port_name)
{
  return instance.get_instance_name() + ":" + port_name;
}

void GraphBuilder::addCellArc(Instance& instance, TimingCellArc& timing_cell_arc)
{
  Database& database = STADM.getDatabase();
  std::string source_pin = getInstancePinName(instance, timing_cell_arc.get_source_port());
  std::string sink_pin = getInstancePinName(instance, timing_cell_arc.get_sink_port());
  if (database.get_pin_map().count(source_pin) == 0 || database.get_pin_map().count(sink_pin) == 0) {
    return;
  }
  bool is_disable_arc = timing_cell_arc.get_is_disable_arc();
  addArc(source_pin, sink_pin, ArcType::kCell, instance.get_instance_name(), timing_cell_arc.get_source_port(), timing_cell_arc.get_sink_port(),
         timing_cell_arc.get_is_clock_arc(), is_disable_arc, &timing_cell_arc);
}

void GraphBuilder::addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name,
                          const std::string& library_source_port, const std::string& library_sink_port, bool is_clock_arc, bool is_disable_arc,
                          TimingCellArc* timing_cell_arc)
{
  Database& database = STADM.getDatabase();
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
  arc.set_timing_cell_arc(timing_cell_arc);

  database.get_arc_list().push_back(arc);
  const std::size_t arc_idx = database.get_arc_list().size() - 1;
  database.get_outgoing_arc_list_map()[source_pin].push_back(arc_idx);
  database.get_incoming_arc_list_map()[sink_pin].push_back(arc_idx);
}

std::vector<std::string> GraphBuilder::collectInputPins(Instance& instance)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> input_pin_list;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (isInputLike(database.get_pin_map()[pin_name].get_direction())) {
      input_pin_list.push_back(pin_name);
    }
  }
  return input_pin_list;
}

bool GraphBuilder::isInputLike(PinDirection direction)
{
  return direction == PinDirection::kInput || direction == PinDirection::kInout;
}

std::vector<std::string> GraphBuilder::collectOutputPins(Instance& instance)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> output_pin_list;
  for (std::string& pin_name : instance.get_pin_name_list()) {
    if (isOutputLike(database.get_pin_map()[pin_name].get_direction())) {
      output_pin_list.push_back(pin_name);
    }
  }
  return output_pin_list;
}

bool GraphBuilder::isOutputLike(PinDirection direction)
{
  return direction == PinDirection::kOutput || direction == PinDirection::kInout;
}

void GraphBuilder::buildInoutPinDirectionByGraph()
{
  Database& database = STADM.getDatabase();
  std::map<std::string, PinDirection> inout_pin_direction_map = makeInoutPinDirectionMap();
  for (std::pair<const std::string, PinDirection>& pin_direction_pair : inout_pin_direction_map) {
    database.get_pin_map()[pin_direction_pair.first].set_direction(pin_direction_pair.second);
  }
  rebuildCellArcListByPinDirection();
}

std::map<std::string, PinDirection> GraphBuilder::makeInoutPinDirectionMap()
{
  Database& database = STADM.getDatabase();
  std::map<std::string, PinDirection> inout_pin_direction_map;
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    TimingCellPort* port = getTimingCellPort(pin);
    if (pin.get_is_port() || (port != nullptr && port->get_is_input() && port->get_is_output())) {
      // True bidirectional ports retain both graph roles; they are not missing directions.
      continue;
    }
    if (isFloatingInoutPin(pin)) {
      continue;
    }
    PinDirection pin_direction = inferInoutPinDirection(pin_pair.first, pin, inout_pin_direction_map);
    if (pin_direction == PinDirection::kNone || pin_direction == PinDirection::kInout) {
      STALOG.error(Loc::current(), "Failed to infer inout pin direction: pin=", pin_pair.first, " net=", pin.get_net_name());
    }
    inout_pin_direction_map[pin_pair.first] = pin_direction;
  }
  return inout_pin_direction_map;
}

bool GraphBuilder::isFloatingInoutPin(Pin& pin)
{
  return pin.get_net_name().empty() && inferInoutPinDirectionByTimingCell(pin) == PinDirection::kNone;
}

PinDirection GraphBuilder::inferInoutPinDirection(const std::string& pin_name, Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  PinDirection timing_cell_direction = inferInoutPinDirectionByTimingCell(pin);
  if (timing_cell_direction != PinDirection::kNone) {
    return timing_cell_direction;
  }

  PinDirection timing_graph_direction = inferInoutPinDirectionByTimingGraph(pin_name);
  if (timing_graph_direction != PinDirection::kNone) {
    return timing_graph_direction;
  }

  return inferInoutPinDirectionByNet(pin, inout_pin_direction_map);
}

PinDirection GraphBuilder::inferInoutPinDirectionByTimingCell(Pin& pin)
{
  TimingCellPort* timing_cell_port = getTimingCellPort(pin);
  if (timing_cell_port == nullptr) {
    return PinDirection::kNone;
  }
  if (timing_cell_port->get_is_output() && !timing_cell_port->get_is_input()) {
    return PinDirection::kOutput;
  }
  if (timing_cell_port->get_is_input() && !timing_cell_port->get_is_output()) {
    return PinDirection::kInput;
  }
  return PinDirection::kNone;
}

TimingCellPort* GraphBuilder::getTimingCellPort(Pin& pin)
{
  Database& database = STADM.getDatabase();
  if (pin.get_is_port()) {
    return nullptr;
  }
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_port_map().count(pin.get_pin_name()) == 0) {
    return nullptr;
  }
  return &timing_cell.get_port_map()[pin.get_pin_name()];
}

PinDirection GraphBuilder::inferInoutPinDirectionByTimingGraph(const std::string& pin_name)
{
  const bool has_outgoing_cell_arc = hasOutgoingCellArc(pin_name);
  const bool has_incoming_cell_arc = hasIncomingCellArc(pin_name);
  if (has_outgoing_cell_arc && !has_incoming_cell_arc) {
    return PinDirection::kInput;
  }
  if (has_incoming_cell_arc && !has_outgoing_cell_arc) {
    return PinDirection::kOutput;
  }
  return PinDirection::kNone;
}

bool GraphBuilder::hasOutgoingCellArc(const std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
    if (database.get_arc_list()[arc_idx].get_type() == ArcType::kCell) {
      return true;
    }
  }
  return false;
}

bool GraphBuilder::hasIncomingCellArc(const std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  for (std::size_t arc_idx : database.get_incoming_arc_list_map()[pin_name]) {
    if (database.get_arc_list()[arc_idx].get_type() == ArcType::kCell) {
      return true;
    }
  }
  return false;
}

PinDirection GraphBuilder::inferInoutPinDirectionByNet(Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  Net* net = getPinNet(pin);
  if (net == nullptr) {
    return PinDirection::kNone;
  }
  int32_t driver_pin_num = getDriverPinNum(*net, inout_pin_direction_map);
  int32_t unresolved_inout_pin_num = getUnresolvedInoutPinNum(*net, inout_pin_direction_map);
  if (driver_pin_num == 0 && unresolved_inout_pin_num == 1) {
    return getDriverPinDirection(pin);
  }
  if (driver_pin_num == 1) {
    return getLoadPinDirection(pin);
  }
  if (driver_pin_num > 1) {
    std::vector<std::string> driver_pin_list = getDriverPinList(*net, inout_pin_direction_map);
    STALOG.error(Loc::current(), "The net has multiple driver pins: net=", net->get_net_name(), " drivers=", getPinNameListString(driver_pin_list));
  }
  return PinDirection::kNone;
}

Net* GraphBuilder::getPinNet(Pin& pin)
{
  Database& database = STADM.getDatabase();
  if (pin.get_net_name().empty() || database.get_net_map().count(pin.get_net_name()) == 0) {
    return nullptr;
  }
  return &database.get_net_map()[pin.get_net_name()];
}

std::vector<std::string> GraphBuilder::getDriverPinList(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  std::vector<std::string> driver_pin_list;
  for (std::string& pin_name : net.get_pin_name_list()) {
    if (isResolvedDriverPin(pin_name, inout_pin_direction_map)) {
      driver_pin_list.push_back(pin_name);
    }
  }
  return driver_pin_list;
}

int32_t GraphBuilder::getDriverPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  return static_cast<int32_t>(getDriverPinList(net, inout_pin_direction_map).size());
}

int32_t GraphBuilder::getUnresolvedInoutPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  Database& database = STADM.getDatabase();
  int32_t unresolved_inout_pin_num = 0;
  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
    if (pin.get_direction() == PinDirection::kInout && inout_pin_direction_map.count(pin_name) == 0) {
      unresolved_inout_pin_num++;
    }
  }
  return unresolved_inout_pin_num;
}

bool GraphBuilder::isResolvedDriverPin(const std::string& pin_name, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  PinDirection direction = pin.get_direction();
  if (direction == PinDirection::kInout && inout_pin_direction_map.count(pin_name) > 0) {
    direction = inout_pin_direction_map[pin_name];
  }
  return isDriverDirection(pin, direction);
}

bool GraphBuilder::isDriverDirection(Pin& pin, PinDirection direction)
{
  if (pin.get_is_port()) {
    return direction == PinDirection::kInput;
  }
  return direction == PinDirection::kOutput;
}

PinDirection GraphBuilder::getDriverPinDirection(Pin& pin)
{
  if (pin.get_is_port()) {
    return PinDirection::kInput;
  }
  return PinDirection::kOutput;
}

PinDirection GraphBuilder::getLoadPinDirection(Pin& pin)
{
  if (pin.get_is_port()) {
    return PinDirection::kOutput;
  }
  return PinDirection::kInput;
}

void GraphBuilder::rebuildCellArcListByPinDirection()
{
  Database& database = STADM.getDatabase();
  database.get_arc_list().clear();
  database.get_outgoing_arc_list_map().clear();
  database.get_incoming_arc_list_map().clear();
  buildCellArcs();
}

void GraphBuilder::buildNetDriverLoadList()
{
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, Net>& net_pair : database.get_net_map()) {
    makeNetDriverLoad(net_pair.second);
  }
}

void GraphBuilder::makeNetDriverLoad(Net& net)
{
  Database& database = STADM.getDatabase();
  net.get_driver_pin().clear();
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
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
    Pin& pin = database.get_pin_map()[driver];
    TimingCellPort* port = getTimingCellPort(pin);
    if (pin.get_direction() != PinDirection::kInout && (port == nullptr || port->get_three_state_expression().get_is_empty())) {
      ++unconditional_driver_num;
    }
  }
  if (unconditional_driver_num > 1) {
    STALOG.error(Loc::current(), "The net has multiple driver pins: net=", net.get_net_name(), " drivers=", getPinNameListString(net.get_driver_pin_list()));
  }
}

bool GraphBuilder::isDriverPin(Pin& pin)
{
  return pin.get_direction() == PinDirection::kInout || isDriverDirection(pin, pin.get_direction());
}

std::string GraphBuilder::getPinNameListString(std::vector<std::string>& pin_name_list)
{
  std::string pin_name_list_string;
  for (std::string& pin_name : pin_name_list) {
    if (!pin_name_list_string.empty()) {
      pin_name_list_string += ", ";
    }
    pin_name_list_string += pin_name;
  }
  return pin_name_list_string;
}

void GraphBuilder::buildNetArcs()
{
  Database& database = STADM.getDatabase();
  for (auto& [net_name, net] : database.get_net_map()) {
    for (std::string& driver_pin : net.get_driver_pin_list()) {
      for (std::string& load_pin : net.get_load_pin_list()) {
        if (load_pin != driver_pin) {
          addArc(driver_pin, load_pin, ArcType::kNet, net_name);
        }
      }
    }
  }
}

void GraphBuilder::buildGraphVertexList()
{
  Database& database = STADM.getDatabase();
  database.get_graph_vertex_list().clear();
  database.get_driver_timing_point_map().clear();
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    int32_t idx = addGraphVertex(pin_name, isDriverPin(pin), database.get_timing_point_map()[pin_name]);
    pin.set_load_vertex_idx(idx);
    pin.set_driver_vertex_idx(idx);
  }
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    if (pin.get_direction() == PinDirection::kInout) {
      database.get_graph_vertex_list()[pin.get_load_vertex_idx()].set_is_driver(false);
      pin.set_driver_vertex_idx(addGraphVertex(pin_name, true, database.get_driver_timing_point_map()[pin_name]));
    }
  }
  for (std::size_t idx = 0; idx < database.get_arc_list().size(); ++idx) {
    Arc& arc = database.get_arc_list()[idx];
    Pin& source = database.get_pin_map()[arc.get_source_pin()];
    Pin& sink = database.get_pin_map()[arc.get_sink_pin()];
    arc.set_source_vertex_idx(arc.get_type() == ArcType::kNet ? source.get_driver_vertex_idx() : source.get_load_vertex_idx());
    arc.set_sink_vertex_idx(arc.get_type() == ArcType::kNet ? sink.get_load_vertex_idx() : sink.get_driver_vertex_idx());
    database.get_graph_vertex_list()[arc.get_source_vertex_idx()].get_outgoing_arc_list().push_back(idx);
    database.get_graph_vertex_list()[arc.get_sink_vertex_idx()].get_incoming_arc_list().push_back(idx);
  }
}

int32_t GraphBuilder::addGraphVertex(const std::string& pin_name, bool is_driver, TimingPoint& point)
{
  Database& database = STADM.getDatabase();
  GraphVertex vertex;
  vertex.set_pin_name(pin_name);
  vertex.set_is_driver(is_driver);
  vertex.set_timing_point(&point);
  database.get_graph_vertex_list().push_back(vertex);
  return static_cast<int32_t>(database.get_graph_vertex_list().size()) - 1;
}

void GraphBuilder::addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name)
{
  Database& database = STADM.getDatabase();
  Arc arc;
  arc.set_arc_name(owner_name + ":" + source_pin + "->" + sink_pin);
  arc.set_source_pin(source_pin);
  arc.set_sink_pin(sink_pin);
  arc.set_owner_name(owner_name);
  arc.set_type(type);
  arc.set_is_disable_arc(shouldDisableNetArc(source_pin, sink_pin));

  database.get_arc_list().push_back(arc);
  const std::size_t arc_idx = database.get_arc_list().size() - 1;
  database.get_outgoing_arc_list_map()[source_pin].push_back(arc_idx);
  database.get_incoming_arc_list_map()[sink_pin].push_back(arc_idx);
}

bool GraphBuilder::shouldDisableNetArc(const std::string& source_pin, const std::string& sink_pin)
{
  Database& database = STADM.getDatabase();
  Pin& source_pin_inst = database.get_pin_map()[source_pin];
  Pin& sink_pin_inst = database.get_pin_map()[sink_pin];
  if (source_pin_inst.get_is_port() || sink_pin_inst.get_is_port()) {
    return false;
  }
  if (database.get_instance_map().count(source_pin_inst.get_instance_name()) == 0
      || database.get_instance_map().count(sink_pin_inst.get_instance_name()) == 0) {
    return false;
  }
  Instance& source_instance = database.get_instance_map()[source_pin_inst.get_instance_name()];
  Instance& sink_instance = database.get_instance_map()[sink_pin_inst.get_instance_name()];
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(source_instance.get_cell_name()) == 0) {
    return false;
  }
  TimingCell& source_cell = timing_cell_map[source_instance.get_cell_name()];
  return source_cell.get_is_sequential() && !source_cell.get_is_clock_gating() && !source_cell.get_is_macro() && sink_pin == sink_instance.get_clock_pin_name();
}

bool GraphBuilder::isDisableArc(Arc& arc)
{
  return arc.get_is_disable_arc() || arc.get_is_loop_disable();
}

void GraphBuilder::buildStartEndPointList()
{
  Database& database = STADM.getDatabase();
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    if (isStartPoint(pin_name, pin)) {
      database.get_start_point_list().push_back(pin_name);
    }
    if (isEndPoint(pin_name, pin)) {
      database.get_end_point_list().push_back(pin_name);
    }
  }
}

bool GraphBuilder::isStartPoint(const std::string& pin_name, Pin& pin)
{
  if (isRegisterClockStartPoint(pin_name, pin)) {
    return true;
  }
  if (isClockPin(pin_name, pin)) {
    return false;
  }
  if (isClockSource(pin_name)) {
    return isStartPort(pin);
  }
  return !hasIncomingArc(pin_name) || isStartPort(pin);
}

bool GraphBuilder::isRegisterClockStartPoint(const std::string& pin_name, Pin& pin)
{
  Database& database = STADM.getDatabase();
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_is_clock_gating() && pin_name == instance.get_clock_pin_name();
}

bool GraphBuilder::isClockPin(const std::string& pin_name, Pin& pin)
{
  Database& database = STADM.getDatabase();
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  return pin_name == database.get_instance_map()[pin.get_instance_name()].get_clock_pin_name();
}

bool GraphBuilder::isClockSource(const std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  for (auto& [clock_name, timing_clock] : database.get_timing_constraint().get_clock_map()) {
    if (STAUTIL.exist(timing_clock.get_source_list(), pin_name)) {
      return true;
    }
  }
  return false;
}

bool GraphBuilder::hasIncomingArc(const std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  return database.get_incoming_arc_list_map().count(pin_name) > 0 && !database.get_incoming_arc_list_map()[pin_name].empty();
}

bool GraphBuilder::isStartPort(Pin& pin)
{
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout);
}

bool GraphBuilder::isEndPoint(const std::string& pin_name, Pin& pin)
{
  if (isClockPin(pin_name, pin) || isClockSource(pin_name)) {
    return false;
  }
  return !hasOutgoingArc(pin_name) || isEndPort(pin) || isTimingCheckEndPoint(pin_name, pin);
}

bool GraphBuilder::isTimingCheckEndPoint(const std::string& pin_name, Pin& pin)
{
  Database& database = STADM.getDatabase();
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  for (TimingCheckArc& timing_check_arc : instance.get_check_arc_list()) {
    if (timing_check_arc.get_data_port() == pin_name) {
      return true;
    }
  }
  return false;
}

bool GraphBuilder::hasOutgoingArc(const std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  return database.get_outgoing_arc_list_map().count(pin_name) > 0 && !database.get_outgoing_arc_list_map()[pin_name].empty();
}

bool GraphBuilder::isEndPort(Pin& pin)
{
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout);
}

void GraphBuilder::breakLoopArcList()
{
  Database& database = STADM.getDatabase();
  std::vector<GraphVertex>& vertices = database.get_graph_vertex_list();
  std::vector<GBColorType> colors(vertices.size(), GBColorType::kNone);
  int32_t disabled_loop_num = 0;
  // Use graph vertices, not physical pin names: PAD receive and drive are independent.
  for (int32_t root = 0; root < static_cast<int32_t>(vertices.size()); ++root) {
    if (colors[root] != GBColorType::kNone) {
      continue;
    }
    std::vector<std::pair<int32_t, std::size_t>> stack{{root, 0}};
    colors[root] = GBColorType::kGray;
    while (!stack.empty()) {
      auto& [vertex_idx, next_arc_idx] = stack.back();
      std::vector<std::size_t>& outgoing = vertices[vertex_idx].get_outgoing_arc_list();
      if (next_arc_idx == outgoing.size()) {
        colors[vertex_idx] = GBColorType::kBlack;
        stack.pop_back();
        continue;
      }
      Arc& arc = database.get_arc_list()[outgoing[next_arc_idx++]];
      if (isDisableArc(arc)) {
        continue;
      }
      int32_t sink_idx = arc.get_sink_vertex_idx();
      if (colors[sink_idx] == GBColorType::kGray) {
        arc.set_is_loop_disable(true);
        ++disabled_loop_num;
        STALOG.warn(Loc::current(), "Combinational loop cut: ", arc.get_source_pin(), " -> ", arc.get_sink_pin());
      } else if (colors[sink_idx] == GBColorType::kNone) {
        colors[sink_idx] = GBColorType::kGray;
        stack.emplace_back(sink_idx, 0);
      }
    }
  }
  if (disabled_loop_num > 0) {
    STALOG.info(Loc::current(), "Break iSTA loop arcs: disabled_arcs=", disabled_loop_num);
  }
}

void GraphBuilder::buildTimingOrder()
{
  Database& database = STADM.getDatabase();
  std::vector<GraphVertex>& vertices = database.get_graph_vertex_list();
  std::vector<int32_t> indegree_list(vertices.size(), 0);
  std::queue<int32_t> vertex_queue;
  for (int32_t idx = 0; idx < static_cast<int32_t>(vertices.size()); ++idx) {
    for (std::size_t arc_idx : vertices[idx].get_incoming_arc_list()) {
      if (!isDisableArc(database.get_arc_list()[arc_idx])) {
        ++indegree_list[idx];
      }
    }
    if (indegree_list[idx] == 0) {
      vertices[idx].get_timing_point().set_level(1);
      vertex_queue.push(idx);
    }
  }
  database.get_graph_order_list().clear();
  database.get_timing_order_list().clear();
  while (!vertex_queue.empty()) {
    int32_t vertex_idx = vertex_queue.front();
    vertex_queue.pop();
    GraphVertex& vertex = vertices[vertex_idx];
    database.get_graph_order_list().push_back(vertex_idx);
    database.get_timing_order_list().push_back(vertex.get_pin_name());
    for (std::size_t arc_idx : vertex.get_outgoing_arc_list()) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (isDisableArc(arc)) {
        continue;
      }
      int32_t sink_idx = arc.get_sink_vertex_idx();
      TimingPoint& sink = vertices[sink_idx].get_timing_point();
      sink.set_level(std::max(sink.get_level(), vertex.get_timing_point().get_level() + 1));
      if (--indegree_list[sink_idx] == 0) {
        vertex_queue.push(sink_idx);
      }
    }
  }
}

void GraphBuilder::printLoopInfo()
{
  Database& database = STADM.getDatabase();
  if (database.get_graph_order_list().size() != database.get_graph_vertex_list().size()) {
    STALOG.error(Loc::current(), "Incomplete graph order after loop cleanup.");
  }
}

void GraphBuilder::initializeArcTiming()
{
  Database& database = STADM.getDatabase();
  for (Arc& arc : database.get_arc_list()) {
    DCTask dc_task;
    dc_task.set_proc_type(DCProcType::kInitialize);
    dc_task.set_arc(&arc);
    STADC.calculate(dc_task);
  }
}

}  // namespace ista
