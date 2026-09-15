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
#include "SdcCommandUtils.hpp"

#include "STAHeader.hpp"
#include "SdcCommand.hpp"

namespace ista::sdc {

std::vector<std::string> queryPatterns(const std::string& text, bool regexp)
{
  // OpenSTA SDC commonly passes one raw regexp; PT writers pass a Tcl list.
  if (regexp && !text.empty() && text.front() != '{' && text.find('\\') != std::string::npos) {
    return {text};
  }
  int count = 0;
  const char** items = nullptr;
  if (Tcl_SplitList(SdcCommand::getInst().getInterp(), text.c_str(), &count, &items) != TCL_OK) {
    throw std::invalid_argument("invalid object pattern list");
  }
  std::vector<std::string> result(items, items + count);
  Tcl_Free(reinterpret_cast<char*>(items));
  return result;
}

std::vector<std::string> queryObjects(Database& database, const std::vector<std::string>& patterns, QueryObjectType type, bool regexp)
{
  std::map<std::string, std::string> names;
  if (type == QueryObjectType::kClock || type == QueryObjectType::kAny) {
    for (const auto& [name, clock] : database.get_timing_constraint().get_clock_map()) {
      names[name] = name;
    }
  }
  if (type != QueryObjectType::kClock) {
    for (auto& [name, pin] : database.get_pin_map()) {
      if ((type == QueryObjectType::kPort && !pin.get_is_port()) || (type == QueryObjectType::kPin && pin.get_is_port())) {
        continue;
      }
      names[name] = name;
      if (!pin.get_is_port()) {
        names[pin.get_instance_name() + "/" + pin.get_pin_name()] = name;
      }
    }
  }
  if (type == QueryObjectType::kAny) {
    for (const auto& [name, instance] : database.get_instance_map()) {
      names[name] = name;
    }
  }
  std::set<std::string> found;
  for (const std::string& pattern : patterns) {
    if (!regexp && names.contains(pattern)) {
      found.insert(names.at(pattern));
      continue;
    }
    const std::string expression = "^(?:" + pattern + ")$";
    if (regexp && Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), "", expression.c_str()) < 0) {
      throw std::invalid_argument("invalid regular expression: " + pattern);
    }
    for (const auto& [name, canonical] : names) {
      if (regexp ? Tcl_RegExpMatch(SdcCommand::getInst().getInterp(), name.c_str(), expression.c_str()) == 1
                 : Tcl_StringMatch(name.c_str(), pattern.c_str()) != 0) {
        found.insert(canonical);
      }
    }
  }
  return {found.begin(), found.end()};
}

std::set<std::string> resolveClockObjects(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> clocks;
  for (const std::string& pattern : objects) {
    const std::vector<std::string> matches = queryObjects(database, {pattern}, QueryObjectType::kClock);
    if (matches.empty()) {
      throw std::invalid_argument("clock '" + pattern + "' does not exist");
    }
    clocks.insert(matches.begin(), matches.end());
  }
  if (clocks.empty()) {
    throw std::invalid_argument("a non-empty clock collection is required");
  }
  return clocks;
}

std::set<std::string> resolveExceptionObjects(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> result;
  for (const std::string& object : objects) {
    if (database.get_pin_map().contains(object) || database.get_instance_map().contains(object)
        || database.get_timing_constraint().get_clock_map().contains(object)) {
      result.insert(object);
      continue;
    }
    const std::vector<std::string> matches = queryObjects(database, {object}, QueryObjectType::kAny);
    if (matches.empty()) {
      throw std::invalid_argument("exception object not found: " + object);
    }
    result.insert(matches.begin(), matches.end());
  }
  if (result.empty()) {
    throw std::invalid_argument("empty timing exception collection");
  }
  return result;
}

std::vector<std::string> resolveObjectList(Database& database, const std::vector<std::string>& object_list)
{
  std::vector<std::string> resolved_object_list;
  for (const std::string& object_name : object_list) {
    std::string resolved_object_name = object_name;
    if (!resolved_object_name.empty() && resolved_object_name.front() == '\\') {
      resolved_object_name.erase(resolved_object_name.begin());
    }
    if (resolved_object_name.rfind("[get_ports", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(10);
    }
    if (resolved_object_name.rfind("[get_pins", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(9);
      std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    }
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
      continue;
    }
    if (!resolved_object_name.empty() && resolved_object_name.back() == ']') {
      std::string trimmed_object_name = resolved_object_name;
      trimmed_object_name.pop_back();
      if (database.get_pin_map().count(trimmed_object_name) > 0) {
        resolved_object_list.push_back(trimmed_object_name);
        continue;
      }
    }
    std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
    }
  }
  return resolved_object_list;
}

TimingPortConstraint& getPortConstraint(Database& database, const std::string& port_name)
{
  TimingPortConstraint& port_constraint = database.get_timing_constraint().get_port_constraint_map()[port_name];
  port_constraint.set_port_name(port_name);
  return port_constraint;
}

}  // namespace ista::sdc
