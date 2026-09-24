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
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ipw {

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
    PWLOG.error(Loc::current(), "The instance not initialized!");
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
  PWLOG.info(Loc::current(), "Starting...");
  buildSignalPointList();
  normalizePinDirectionByTimingCell();
  buildCellArcs();
  buildInoutPinDirectionByGraph();
  buildNetDriverLoadList();
  buildNetArcs();
  buildSourcePinList();
  breakLoopArcList();
  buildSignalOrder();
  applyCaseAnalysis();
  printLoopInfo();
  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void GraphBuilder::applyCaseAnalysis()
{
  Database& database = PWDM.getDatabase();
  TimingConstraint& constraint = database.get_timing_constraint();
  if (constraint.get_case_analysis_map().empty()) {
    constraint.get_effective_case_analysis_map().clear();
    for (Arc& arc : database.get_arc_list()) {
      arc.set_is_case_analysis_disable(false);
    }
    return;
  }
  constraint.get_effective_case_analysis_map() = constraint.get_case_analysis_map();
  propagateCaseValue();
  disableCaseInsensitiveArc();
}

void GraphBuilder::propagateCaseValue()
{
  Database& database = PWDM.getDatabase();
  TimingConstraint& constraint = database.get_timing_constraint();
  bool changed = true;
  for (std::size_t iteration = 0; changed && iteration <= database.get_pin_map().size(); ++iteration) {
    changed = false;
    for (auto& [net_name, net] : database.get_net_map()) {
      const std::optional<TimingCaseValue> value = getNetDriverCaseValue(constraint, net);
      if (!value.has_value()) {
        continue;
      }
      for (const std::string& load : net.get_load_pin_list()) {
        changed = updateEffectiveCaseValue(constraint, load, *value) || changed;
      }
    }

    for (auto& [instance_name, instance] : database.get_instance_map()) {
      const auto cell = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
      if (cell == database.get_timing_library().get_cell_map().end()) {
        continue;
      }
      const std::map<std::string, bool> value_map = getInstanceConstantInputMap(instance);
      for (auto& [port_name, port] : cell->second.get_port_map()) {
        if (!port.get_is_output() || port.get_function_expression().get_is_empty()) {
          continue;
        }
        const std::string output_pin = instance_name + ":" + port_name;
        const std::optional<bool> value = port.get_function_expression().evaluate_constant(value_map);
        if (value.has_value()) {
          changed = updateEffectiveCaseValue(constraint, output_pin, *value ? TimingCaseValue::kOne : TimingCaseValue::kZero) || changed;
        } else if (hasStaticExpressionInput(instance, port.get_function_expression())) {
          changed = updateEffectiveCaseValue(constraint, output_pin, TimingCaseValue::kStatic) || changed;
        }
      }
    }
  }
}

std::optional<TimingCaseValue> GraphBuilder::getNetDriverCaseValue(TimingConstraint& constraint, Net& net)
{
  for (const std::string& driver_pin : net.get_driver_pin_list()) {
    const auto value = constraint.get_effective_case_analysis_map().find(driver_pin);
    if (value != constraint.get_effective_case_analysis_map().end()) {
      return value->second;
    }
  }
  return std::nullopt;
}

std::map<std::string, bool> GraphBuilder::getInstanceConstantInputMap(Instance& instance)
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, bool> value_map;
  for (const std::string& pin_name : instance.get_pin_name_list()) {
    const auto pin = database.get_pin_map().find(pin_name);
    const auto value = database.get_timing_constraint().get_effective_case_analysis_map().find(pin_name);
    if (pin == database.get_pin_map().end() || value == database.get_timing_constraint().get_effective_case_analysis_map().end()
        || !isConstantCaseValue(value->second)) {
      continue;
    }
    value_map[pin->second.get_pin_name()] = value->second == TimingCaseValue::kOne;
  }
  return value_map;
}

bool GraphBuilder::hasStaticExpressionInput(Instance& instance, LogicExpression& expression)
{
  Database& database = PWDM.getDatabase();
  for (LogicExpressionTerm& term : expression.get_term_list()) {
    if (term.get_operation_type() != LogicOperationType::kPort) {
      continue;
    }
    const std::string pin_name = instance.get_instance_name() + ":" + term.get_port_name();
    const auto value = database.get_timing_constraint().get_effective_case_analysis_map().find(pin_name);
    if (value == database.get_timing_constraint().get_effective_case_analysis_map().end() || !isStaticCaseValue(value->second)) {
      return false;
    }
  }
  return !expression.get_is_empty();
}

bool GraphBuilder::isStaticCaseValue(TimingCaseValue value)
{
  return isConstantCaseValue(value) || value == TimingCaseValue::kStatic;
}

bool GraphBuilder::isConstantCaseValue(TimingCaseValue value)
{
  return value == TimingCaseValue::kZero || value == TimingCaseValue::kOne;
}

bool GraphBuilder::updateEffectiveCaseValue(TimingConstraint& constraint, const std::string& pin_name, TimingCaseValue value)
{
  if (constraint.get_case_analysis_map().contains(pin_name)) {
    return false;
  }
  auto& effective_value_map = constraint.get_effective_case_analysis_map();
  const auto current = effective_value_map.find(pin_name);
  if (current != effective_value_map.end() && current->second == value) {
    return false;
  }
  if (current != effective_value_map.end() && current->second == TimingCaseValue::kZero) {
    return false;
  }
  effective_value_map[pin_name] = value;
  return true;
}

void GraphBuilder::disableCaseInsensitiveArc()
{
  Database& database = PWDM.getDatabase();
  for (Arc& arc : database.get_arc_list()) {
    arc.set_is_case_analysis_disable(false);
    const auto source_value = database.get_timing_constraint().get_effective_case_analysis_map().find(arc.get_source_pin());
    const auto sink_value = database.get_timing_constraint().get_effective_case_analysis_map().find(arc.get_sink_pin());
    if ((source_value != database.get_timing_constraint().get_effective_case_analysis_map().end() && isStaticCaseValue(source_value->second))
        || (sink_value != database.get_timing_constraint().get_effective_case_analysis_map().end() && isStaticCaseValue(sink_value->second))) {
      arc.set_is_case_analysis_disable(true);
      continue;
    }
    if (arc.get_type() != ArcType::kCell) {
      continue;
    }
    const auto instance = database.get_instance_map().find(arc.get_owner_name());
    if (instance == database.get_instance_map().end()) {
      continue;
    }
    const auto cell = database.get_timing_library().get_cell_map().find(instance->second.get_cell_name());
    if (cell == database.get_timing_library().get_cell_map().end()) {
      continue;
    }
    const auto output = cell->second.get_port_map().find(arc.get_library_sink_port());
    if (output == cell->second.get_port_map().end() || output->second.get_function_expression().get_is_empty()) {
      continue;
    }
    if (!output->second.get_function_expression().is_sensitive_to(arc.get_library_source_port(), getInstanceConstantInputMap(instance->second))) {
      arc.set_is_case_analysis_disable(true);
    }
  }
}

// private

GraphBuilder* GraphBuilder::_gb_instance = nullptr;

void GraphBuilder::buildSignalPointList()
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    database.get_timing_point_map()[pin_pair.first] = TimingPoint();
  }
}

void GraphBuilder::normalizePinDirectionByTimingCell()
{
  Database& database = PWDM.getDatabase();
  for (auto& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port()) {
      continue;
    }
    PinDirection timing_cell_direction = inferInoutPinDirectionByTimingCell(pin);
    if (timing_cell_direction != PinDirection::kNone) {
      // Liberty is the timing authority when its port direction is unambiguous.
      pin.set_direction(timing_cell_direction);
    }
  }
}

void GraphBuilder::buildCellArcs()
{
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
  Arc arc;
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
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
  std::map<std::string, PinDirection> inout_pin_direction_map = makeInoutPinDirectionMap();
  for (std::pair<const std::string, PinDirection>& pin_direction_pair : inout_pin_direction_map) {
    database.get_pin_map()[pin_direction_pair.first].set_direction(pin_direction_pair.second);
  }
  rebuildCellArcListByPinDirection();
}

std::map<std::string, PinDirection> GraphBuilder::makeInoutPinDirectionMap()
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, PinDirection> inout_pin_direction_map;
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    if (isFloatingInoutPin(pin)) {
      continue;
    }
    PinDirection pin_direction = inferInoutPinDirection(pin_pair.first, pin, inout_pin_direction_map);
    if (pin_direction == PinDirection::kNone || pin_direction == PinDirection::kInout) {
      PWLOG.error(Loc::current(), "Failed to infer inout pin direction: pin=", pin_pair.first, " net=", pin.get_net_name());
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
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
  for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
    if (database.get_arc_list()[arc_idx].get_type() == ArcType::kCell) {
      return true;
    }
  }
  return false;
}

bool GraphBuilder::hasIncomingCellArc(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
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
    PWLOG.error(Loc::current(), "The net has multiple driver pins: net=", pin.get_net_name(), " drivers=", getPinNameListString(driver_pin_list));
  }
  return PinDirection::kNone;
}

Net* GraphBuilder::getPinNet(Pin& pin)
{
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
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
  Database& database = PWDM.getDatabase();
  database.get_arc_list().clear();
  database.get_outgoing_arc_list_map().clear();
  database.get_incoming_arc_list_map().clear();
  buildCellArcs();
}

void GraphBuilder::buildNetDriverLoadList()
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, Net>& net_pair : database.get_net_map()) {
    makeNetDriverLoad(net_pair.first, net_pair.second);
  }
}

void GraphBuilder::makeNetDriverLoad(const std::string& net_name, Net& net)
{
  Database& database = PWDM.getDatabase();
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
    if (isDriverPin(pin)) {
      net.get_driver_pin_list().push_back(pin_name);
    } else {
      net.get_load_pin_list().push_back(pin_name);
    }
  }

  if (net.get_driver_pin_list().size() > 1) {
    PWLOG.error(Loc::current(), "The net has multiple driver pins: net=", net_name, " drivers=", getPinNameListString(net.get_driver_pin_list()));
  }
}

bool GraphBuilder::isDriverPin(Pin& pin)
{
  return isDriverDirection(pin, pin.get_direction());
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
  Database& database = PWDM.getDatabase();
  for (auto& [net_name, net] : database.get_net_map()) {
    if (net.get_driver_pin_list().empty()) {
      continue;
    }
    std::string& driver_pin = net.get_driver_pin_list().back();
    for (std::string& load_pin : net.get_load_pin_list()) {
      if (load_pin == driver_pin) {
        continue;
      }
      addArc(driver_pin, load_pin, ArcType::kNet, net_name);
    }
  }
}

void GraphBuilder::addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name)
{
  Database& database = PWDM.getDatabase();
  Arc arc;
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
  Database& database = PWDM.getDatabase();
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

void GraphBuilder::buildSourcePinList()
{
  Database& database = PWDM.getDatabase();
  database.get_source_pin_list().clear();
  for (auto& [pin_name, pin] : database.get_pin_map()) {
    if (isSourcePin(pin_name, pin)) {
      appendUnique(database.get_source_pin_list(), pin_name);
    }
  }
}

bool GraphBuilder::isSourcePin(const std::string& pin_name, Pin& pin)
{
  if (isSequentialClockSource(pin_name, pin)) {
    return true;
  }
  if (isClockPin(pin_name, pin)) {
    return false;
  }
  if (isClockSource(pin_name)) {
    return isInputPort(pin);
  }
  return !hasIncomingArc(pin_name) || isInputPort(pin);
}

bool GraphBuilder::isSequentialClockSource(const std::string& pin_name, Pin& pin)
{
  Database& database = PWDM.getDatabase();
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_is_clock_gating() && pin_name == instance.get_clock_pin_name();
}

bool GraphBuilder::isClockPin(const std::string& pin_name, Pin& pin)
{
  Database& database = PWDM.getDatabase();
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  return pin_name == database.get_instance_map()[pin.get_instance_name()].get_clock_pin_name();
}

bool GraphBuilder::isClockSource(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  for (auto& [clock_name, timing_clock] : database.get_timing_constraint().get_clock_map()) {
    if (PWUTIL.exist(timing_clock.get_source_list(), pin_name)) {
      return true;
    }
  }
  return false;
}

bool GraphBuilder::hasIncomingArc(const std::string& pin_name)
{
  Database& database = PWDM.getDatabase();
  return database.get_incoming_arc_list_map().count(pin_name) > 0 && !database.get_incoming_arc_list_map()[pin_name].empty();
}

bool GraphBuilder::isInputPort(Pin& pin)
{
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout);
}

void GraphBuilder::appendUnique(std::vector<std::string>& list, const std::string& value)
{
  if (!PWUTIL.exist(list, value)) {
    list.push_back(value);
  }
}

void GraphBuilder::breakLoopArcList()
{
  Database& database = PWDM.getDatabase();
  std::size_t disabled_loop_num = 0;
  std::map<std::string, GBColorType> color_map;
  for (std::pair<const std::string, TimingPoint>& timing_pair : database.get_timing_point_map()) {
    std::string pin_name = timing_pair.first;
    (void) traverseDataPath(pin_name, color_map, disabled_loop_num);
  }
  if (disabled_loop_num > 0) {
    PWLOG.info(Loc::current(), "Break iPW loop arcs: disabled_arcs=", disabled_loop_num);
  }
}

bool GraphBuilder::traverseDataPath(std::string& pin_name, std::map<std::string, GBColorType>& color_map, std::size_t& disabled_loop_num)
{
  Database& database = PWDM.getDatabase();
  if (isBlack(color_map, pin_name)) {
    return false;
  }
  if (isGray(color_map, pin_name)) {
    return true;
  }

  color_map[pin_name] = GBColorType::kGray;
  for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
    Arc& arc = database.get_arc_list()[arc_idx];
    if (isDisableArc(arc)) {
      continue;
    }

    std::string& next_pin_name = arc.get_sink_pin();
    if (isBlack(color_map, next_pin_name)) {
      continue;
    }
    if (isGray(color_map, next_pin_name)) {
      if (disableLoopArc(arc)) {
        ++disabled_loop_num;
      }
      continue;
    }
    if (traverseDataPath(next_pin_name, color_map, disabled_loop_num)) {
      if (disableLoopArc(arc)) {
        ++disabled_loop_num;
      }
      continue;
    }
  }
  color_map[pin_name] = GBColorType::kBlack;
  return false;
}

bool GraphBuilder::isBlack(std::map<std::string, GBColorType>& color_map, std::string& pin_name)
{
  return color_map.count(pin_name) > 0 && color_map[pin_name] == GBColorType::kBlack;
}

bool GraphBuilder::isGray(std::map<std::string, GBColorType>& color_map, std::string& pin_name)
{
  return color_map.count(pin_name) > 0 && color_map[pin_name] == GBColorType::kGray;
}

bool GraphBuilder::disableLoopArc(Arc& arc)
{
  if (arc.get_is_loop_disable()) {
    return false;
  }
  arc.set_is_loop_disable(true);
  return true;
}

void GraphBuilder::buildSignalOrder()
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, std::size_t> indegree_map = makeIndegreeMap();
  std::queue<std::string> pin_queue;
  pushRootPinList(indegree_map, pin_queue);

  database.get_signal_order_list().clear();
  while (!pin_queue.empty()) {
    std::string pin_name = pin_queue.front();
    pin_queue.pop();
    database.get_signal_order_list().push_back(pin_name);

    for (std::size_t arc_idx : database.get_outgoing_arc_list_map()[pin_name]) {
      Arc& arc = database.get_arc_list()[arc_idx];
      if (isDisableArc(arc)) {
        continue;
      }
      updateSinkIndegree(arc, indegree_map, pin_queue);
    }
  }
}

std::map<std::string, std::size_t> GraphBuilder::makeIndegreeMap()
{
  Database& database = PWDM.getDatabase();
  std::map<std::string, std::size_t> indegree_map;
  for (std::pair<const std::string, TimingPoint>& timing_pair : database.get_timing_point_map()) {
    std::size_t indegree = 0;
    for (std::size_t arc_idx : database.get_incoming_arc_list_map()[timing_pair.first]) {
      if (!isDisableArc(database.get_arc_list()[arc_idx])) {
        ++indegree;
      }
    }
    indegree_map[timing_pair.first] = indegree;
  }
  return indegree_map;
}

void GraphBuilder::pushRootPinList(std::map<std::string, std::size_t>& indegree_map, std::queue<std::string>& pin_queue)
{
  Database& database = PWDM.getDatabase();
  for (std::pair<const std::string, TimingPoint>& timing_pair : database.get_timing_point_map()) {
    if (indegree_map[timing_pair.first] == 0) {
      pin_queue.push(timing_pair.first);
    }
  }
}

void GraphBuilder::updateSinkIndegree(Arc& arc, std::map<std::string, std::size_t>& indegree_map, std::queue<std::string>& pin_queue)
{
  if (indegree_map[arc.get_sink_pin()] > 0) {
    --indegree_map[arc.get_sink_pin()];
  }
  if (indegree_map[arc.get_sink_pin()] == 0) {
    pin_queue.push(arc.get_sink_pin());
  }
}

void GraphBuilder::printLoopInfo()
{
  Database& database = PWDM.getDatabase();
  std::size_t loop_pin_num = database.get_timing_point_map().size() - database.get_signal_order_list().size();
  if (loop_pin_num > 0) {
    PWLOG.warn(Loc::current(), "Detected ", loop_pin_num, " vertex(es) in combinational loop or unresolved dependency.");
  }
}

}  // namespace ipw
