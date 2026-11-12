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
#include "GraphEndpointClassifier.hpp"

#include <string>

#include "Utility.hpp"

namespace ista {
void GraphEndpointClassifier::classify()
{
  Database& database = _database;
  for (auto& [pin_name, pin] : database.network().pins()) {
    if (isStartPoint(pin_name, pin)) {
      database.graph().startpoints().push_back(pin_name);
    }
    if (isEndPoint(pin_name, pin)) {
      database.graph().endpoints().push_back(pin_name);
    }
  }
}

bool GraphEndpointClassifier::isStartPoint(const std::string& pin_name, Pin& pin)
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

bool GraphEndpointClassifier::isRegisterClockStartPoint(const std::string& pin_name, Pin& pin)
{
  Database& database = _database;
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_is_clock_gating() && pin_name == instance.get_clock_pin_name();
}

bool GraphEndpointClassifier::isClockPin(const std::string& pin_name, Pin& pin)
{
  Database& database = _database;
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  return pin_name == database.network().instances()[pin.get_instance_name()].get_clock_pin_name();
}

bool GraphEndpointClassifier::isClockSource(const std::string& pin_name)
{
  Database& database = _database;
  for (auto& [clock_name, timing_clock] : database.sdc().get_clock_map()) {
    if (STAUTIL.exist(timing_clock.get_source_list(), pin_name)) {
      return true;
    }
  }
  return false;
}

bool GraphEndpointClassifier::hasIncomingArc(const std::string& pin_name)
{
  Database& database = _database;
  return database.graph().incomingArcs().count(pin_name) > 0 && !database.graph().incomingArcs()[pin_name].empty();
}

bool GraphEndpointClassifier::isStartPort(Pin& pin)
{
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kInput || pin.get_direction() == PinDirection::kInout);
}

bool GraphEndpointClassifier::isEndPoint(const std::string& pin_name, Pin& pin)
{
  if (isClockPin(pin_name, pin) || isClockSource(pin_name)) {
    return false;
  }
  return !hasOutgoingArc(pin_name) || isEndPort(pin) || isTimingCheckEndPoint(pin_name, pin);
}

bool GraphEndpointClassifier::isTimingCheckEndPoint(const std::string& pin_name, Pin& pin)
{
  Database& database = _database;
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  for (TimingCheckArc& timing_check_arc : instance.get_check_arc_list()) {
    if (timing_check_arc.get_data_port() == pin_name) {
      return true;
    }
  }
  return false;
}

bool GraphEndpointClassifier::hasOutgoingArc(const std::string& pin_name)
{
  Database& database = _database;
  return database.graph().outgoingArcs().count(pin_name) > 0 && !database.graph().outgoingArcs()[pin_name].empty();
}

bool GraphEndpointClassifier::isEndPort(Pin& pin)
{
  return pin.get_is_port() && (pin.get_direction() == PinDirection::kOutput || pin.get_direction() == PinDirection::kInout);
}
}  // namespace ista
