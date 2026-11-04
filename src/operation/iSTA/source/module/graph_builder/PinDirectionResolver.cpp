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
#include "PinDirectionResolver.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Logger.hpp"

namespace ista {
void PinDirectionResolver::normalize()
{
  Database& database = _database;
  for (auto& pin_pair : database.network().pins()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port()) {
      continue;
    }
    LibertyPort* port = libertyPort(pin);
    PinDirection timing_cell_direction
        = port != nullptr && port->get_is_input() && port->get_is_output() ? PinDirection::kInout : inferInoutPinDirectionByTimingCell(pin);
    if (timing_cell_direction != PinDirection::kNone) {
      // Liberty defines the signal roles, including a genuine bidirectional port.
      pin.set_direction(timing_cell_direction);
    }
  }
}

std::string PinDirectionResolver::getPinNameListString(std::vector<std::string>& pin_name_list)
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

void PinDirectionResolver::resolve()
{
  Database& database = _database;
  std::map<std::string, PinDirection> inout_pin_direction_map = makeInoutPinDirectionMap();
  for (std::pair<const std::string, PinDirection>& pin_direction_pair : inout_pin_direction_map) {
    database.network().pins()[pin_direction_pair.first].set_direction(pin_direction_pair.second);
  }
}

std::map<std::string, PinDirection> PinDirectionResolver::makeInoutPinDirectionMap()
{
  Database& database = _database;
  std::map<std::string, PinDirection> inout_pin_direction_map;
  for (std::pair<const std::string, Pin>& pin_pair : database.network().pins()) {
    Pin& pin = pin_pair.second;
    if (pin.get_direction() != PinDirection::kInout) {
      continue;
    }
    LibertyPort* port = libertyPort(pin);
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

bool PinDirectionResolver::isFloatingInoutPin(Pin& pin)
{
  return pin.get_net_name().empty() && inferInoutPinDirectionByTimingCell(pin) == PinDirection::kNone;
}

PinDirection PinDirectionResolver::inferInoutPinDirection(const std::string& pin_name, Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map)
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

PinDirection PinDirectionResolver::inferInoutPinDirectionByTimingCell(Pin& pin)
{
  LibertyPort* timing_cell_port = libertyPort(pin);
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

LibertyPort* PinDirectionResolver::libertyPort(Pin& pin)
{
  Database& database = _database;
  if (pin.get_is_port()) {
    return nullptr;
  }
  if (database.network().instances().count(pin.get_instance_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  LibertyCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_port_map().count(pin.get_pin_name()) == 0) {
    return nullptr;
  }
  return &timing_cell.get_port_map()[pin.get_pin_name()];
}

PinDirection PinDirectionResolver::inferInoutPinDirectionByTimingGraph(const std::string& pin_name)
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

bool PinDirectionResolver::hasOutgoingCellArc(const std::string& pin_name)
{
  Database& database = _database;
  for (std::size_t arc_idx : database.graph().outgoingArcs()[pin_name]) {
    if (database.graph().arcs()[arc_idx].get_type() == ArcType::kCell) {
      return true;
    }
  }
  return false;
}

bool PinDirectionResolver::hasIncomingCellArc(const std::string& pin_name)
{
  Database& database = _database;
  for (std::size_t arc_idx : database.graph().incomingArcs()[pin_name]) {
    if (database.graph().arcs()[arc_idx].get_type() == ArcType::kCell) {
      return true;
    }
  }
  return false;
}

PinDirection PinDirectionResolver::inferInoutPinDirectionByNet(Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map)
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

Net* PinDirectionResolver::getPinNet(Pin& pin)
{
  Database& database = _database;
  if (pin.get_net_name().empty() || database.network().nets().count(pin.get_net_name()) == 0) {
    return nullptr;
  }
  return &database.network().nets()[pin.get_net_name()];
}

std::vector<std::string> PinDirectionResolver::getDriverPinList(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  std::vector<std::string> driver_pin_list;
  for (std::string& pin_name : net.get_pin_name_list()) {
    if (isResolvedDriverPin(pin_name, inout_pin_direction_map)) {
      driver_pin_list.push_back(pin_name);
    }
  }
  return driver_pin_list;
}

int32_t PinDirectionResolver::getDriverPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  return static_cast<int32_t>(getDriverPinList(net, inout_pin_direction_map).size());
}

int32_t PinDirectionResolver::getUnresolvedInoutPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  Database& database = _database;
  int32_t unresolved_inout_pin_num = 0;
  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.network().pins()[pin_name];
    if (pin.get_direction() == PinDirection::kInout && inout_pin_direction_map.count(pin_name) == 0) {
      unresolved_inout_pin_num++;
    }
  }
  return unresolved_inout_pin_num;
}

bool PinDirectionResolver::isResolvedDriverPin(const std::string& pin_name, std::map<std::string, PinDirection>& inout_pin_direction_map)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[pin_name];
  PinDirection direction = pin.get_direction();
  if (direction == PinDirection::kInout && inout_pin_direction_map.count(pin_name) > 0) {
    direction = inout_pin_direction_map[pin_name];
  }
  return isDriverDirection(pin, direction);
}

bool PinDirectionResolver::isDriverDirection(Pin& pin, PinDirection direction)
{
  if (pin.get_is_port()) {
    return direction == PinDirection::kInput;
  }
  return direction == PinDirection::kOutput;
}

PinDirection PinDirectionResolver::getDriverPinDirection(Pin& pin)
{
  if (pin.get_is_port()) {
    return PinDirection::kInput;
  }
  return PinDirection::kOutput;
}

PinDirection PinDirectionResolver::getLoadPinDirection(Pin& pin)
{
  if (pin.get_is_port()) {
    return PinDirection::kOutput;
  }
  return PinDirection::kInput;
}
}  // namespace ista
