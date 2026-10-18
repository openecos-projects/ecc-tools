// SPDX-License-Identifier: MulanPSL-2.0
#include "GetFan.hpp"

#include "DataManager.hpp"
#include "object/ObjectQuery.hpp"

namespace ista::sdc {

TclGetFan::TclGetFan(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  _is_fanin = std::string(cmd_name) == "get_fanin";
  addOption(new ecc::TclStringOption(_is_fanin ? "-to" : "-from", 0));
  addOption(new ecc::TclSwitchOption(_is_fanin ? "-startpoints_only" : "-endpoints_only"));
  addOption(new ecc::TclSwitchOption("-only_cells"));
  // IDB's internal netlist is already flat; instance names retain hierarchy.
  addOption(new ecc::TclSwitchOption("-flat"));
}

unsigned TclGetFan::exec()
{
  ecc::TclOption* objects = getOptionOrArg(_is_fanin ? "-to" : "-from");
  if (!objects->is_set_val()) {
    setTclError(std::string(get_cmd_name()) + (_is_fanin ? " requires -to" : " requires -from"));
    return 0;
  }
  std::set<std::string> roots = getQueryPins(parseObjectPatterns(objects->getStringVal(), false));
  bool boundary_only = getOptionOrArg(_is_fanin ? "-startpoints_only" : "-endpoints_only")->is_set_val();
  bool only_cells = getOptionOrArg("-only_cells")->is_set_val();
  std::set<std::string> result = findFan(roots, boundary_only, only_cells);
  setResult(std::vector<std::string>(result.begin(), result.end()));
  return 1;
}

std::set<std::string> TclGetFan::getQueryPins(const std::vector<std::string>& objects)
{
  Database& database = STADM.getDatabase();
  std::set<std::string> roots;
  for (const std::string& object : objects) {
    std::vector<std::string> matches;
    if (database.get_pin_map().contains(object) || database.get_net_map().contains(object)) {
      matches.push_back(object);
    } else {
      for (QueryObjectType type : {QueryObjectType::kPort, QueryObjectType::kPin, QueryObjectType::kNet}) {
        std::vector<std::string> typed_matches = findObjects(database, {object}, type);
        matches.insert(matches.end(), typed_matches.begin(), typed_matches.end());
      }
    }
    if (matches.empty()) {
      throw std::invalid_argument(std::string(get_cmd_name()) + " pin, port or net not found: " + object);
    }
    for (const std::string& match : matches) {
      if (database.get_pin_map().contains(match)) {
        roots.insert(match);
        continue;
      }
      for (const std::string& pin_name : database.get_net_map()[match].get_pin_name_list()) {
        auto pin = database.get_pin_map().find(pin_name);
        if (pin != database.get_pin_map().end() && (_is_fanin || isLoad(pin->second))) {
          roots.insert(pin_name);
        }
      }
    }
  }
  return roots;
}

bool TclGetFan::isLoad(Pin& pin)
{
  PinDirection direction = getDirection(pin);
  return direction == PinDirection::kInout || direction == (pin.get_is_port() ? PinDirection::kOutput : PinDirection::kInput);
}

PinDirection TclGetFan::getDirection(Pin& pin)
{
  Database& database = STADM.getDatabase();
  auto instance = database.get_instance_map().find(pin.get_instance_name());
  if (!pin.get_is_port() && instance != database.get_instance_map().end()) {
    auto cell = database.get_timing_library().get_cell_map().find(instance->second.get_cell_name());
    if (cell != database.get_timing_library().get_cell_map().end()) {
      auto port = cell->second.get_port_map().find(pin.get_pin_name());
      if (port != cell->second.get_port_map().end()) {
        if (port->second.get_is_input() && port->second.get_is_output()) {
          return PinDirection::kInout;
        }
        if (port->second.get_is_input() != port->second.get_is_output()) {
          return port->second.get_is_input() ? PinDirection::kInput : PinDirection::kOutput;
        }
      }
    }
  }
  return pin.get_direction();
}

std::set<std::string> TclGetFan::findFan(const std::set<std::string>& roots, bool boundary_only, bool only_cells)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> pending(roots.begin(), roots.end());
  std::set<std::string> visited;
  std::set<std::string> result;
  while (!pending.empty()) {
    std::string pin_name = pending.back();
    pending.pop_back();
    if (!visited.insert(pin_name).second) {
      continue;
    }
    Pin& pin = database.get_pin_map()[pin_name];
    bool boundary = isRegisterBoundary(pin);
    std::vector<std::string> adjacent = boundary ? std::vector<std::string>() : getAdjacentPins(pin, _is_fanin);
    boundary = boundary || adjacent.empty();
    if (!_is_fanin && boundary) {
      // An isolated pin or a disabled/constant sink is not a timing endpoint.
      boundary = !getAdjacentPins(pin, true).empty();
    }
    if (!boundary_only || boundary) {
      if (!only_cells) {
        result.insert(pin_name);
      } else if (!pin.get_is_port()) {
        result.insert(pin.get_instance_name());
      }
    }
    pending.insert(pending.end(), adjacent.begin(), adjacent.end());
  }
  return result;
}

bool TclGetFan::isRegisterBoundary(Pin& pin)
{
  Database& database = STADM.getDatabase();
  auto instance = database.get_instance_map().find(pin.get_instance_name());
  if (pin.get_is_port() || instance == database.get_instance_map().end()) {
    return false;
  }
  if (_is_fanin) {
    if (!instance->second.get_is_sequential()) {
      return false;
    }
    if (pin.get_full_name() == instance->second.get_clock_pin_name()) {
      return true;
    }
    auto cell = database.get_timing_library().get_cell_map().find(instance->second.get_cell_name());
    if (cell != database.get_timing_library().get_cell_map().end()) {
      for (TimingCellArc& arc : cell->second.get_cell_arc_list()) {
        if (arc.get_is_clock_arc() && pin.get_pin_name() == arc.get_source_port()) {
          return true;
        }
      }
    }
  } else {
    for (TimingCheckArc& check : instance->second.get_check_arc_list()) {
      if (pin.get_full_name() == check.get_data_port()) {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::string> TclGetFan::getAdjacentPins(Pin& pin, bool fanin)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> adjacent;
  if (!isSearchPin(pin)) {
    return adjacent;
  }
  if (fanin ? isLoad(pin) : isDriver(pin)) {
    auto net = database.get_net_map().find(pin.get_net_name());
    if (net != database.get_net_map().end()) {
      for (const std::string& other_name : net->second.get_pin_name_list()) {
        auto other = database.get_pin_map().find(other_name);
        if (other != database.get_pin_map().end() && other_name != pin.get_full_name()
            && (fanin ? isDriver(other->second) : isLoad(other->second)) && isSearchPin(other->second)) {
          adjacent.push_back(other_name);
        }
      }
    }
  }
  auto instance = database.get_instance_map().find(pin.get_instance_name());
  if (pin.get_is_port() || instance == database.get_instance_map().end()) {
    return adjacent;
  }
  auto cell = database.get_timing_library().get_cell_map().find(instance->second.get_cell_name());
  if (cell == database.get_timing_library().get_cell_map().end()) {
    return adjacent;
  }
  for (TimingCellArc& arc : cell->second.get_cell_arc_list()) {
    // Fanin includes clock-to-Q up to the register clock pin. Fanout never
    // crosses a sequential element; reset and check arcs are not data paths.
    if (!arc.get_is_timing_graph_arc() || arc.get_is_disable_arc() || arc.get_is_clear_preset_arc()
        || (!_is_fanin && arc.get_is_clock_arc())) {
      continue;
    }
    if (pin.get_pin_name() != (fanin ? arc.get_sink_port() : arc.get_source_port())) {
      continue;
    }
    std::string other_name = instance->first + ":" + (fanin ? arc.get_source_port() : arc.get_sink_port());
    auto other = database.get_pin_map().find(other_name);
    if (other != database.get_pin_map().end() && isSearchPin(other->second)) {
      adjacent.push_back(other_name);
    }
  }
  return adjacent;
}

bool TclGetFan::isSearchPin(Pin& pin)
{
  std::map<std::string, TimingCaseValue>& values = STADM.getDatabase().get_timing_constraint().get_case_analysis_map();
  auto value = values.find(pin.get_full_name());
  return value == values.end() || (value->second != TimingCaseValue::kZero && value->second != TimingCaseValue::kOne
                                  && value->second != TimingCaseValue::kStatic);
}

bool TclGetFan::isDriver(Pin& pin)
{
  PinDirection direction = getDirection(pin);
  return direction == PinDirection::kInout || direction == (pin.get_is_port() ? PinDirection::kInput : PinDirection::kOutput);
}

}  // namespace ista::sdc
