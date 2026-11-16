// SPDX-License-Identifier: MulanPSL-2.0
#include "GetFan.hpp"

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "query/ObjectQuery.hpp"

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
  std::set<std::string> roots = getQueryPins(getOptionObjects(_is_fanin ? "-to" : "-from"));
  bool boundary_only = getOptionOrArg(_is_fanin ? "-startpoints_only" : "-endpoints_only")->is_set_val();
  bool only_cells = getOptionOrArg("-only_cells")->is_set_val();
  std::set<std::string> result = findFan(roots, boundary_only, only_cells);
  Tcl_Obj* collection = Tcl_NewListObj(0, nullptr);
  for (const auto& name : result) {
    const auto type = only_cells                                                    ? SdcObjectType::kCell
                      : STADM.getDatabase().network().pins().at(name).get_is_port() ? SdcObjectType::kPort
                                                                                    : SdcObjectType::kPin;
    Tcl_ListObjAppendElement(interpreter(), collection, SdcObject::create(name, type));
  }
  Tcl_SetObjResult(interpreter(), collection);
  return 1;
}

std::set<std::string> TclGetFan::getQueryPins(const std::vector<Tcl_Obj*>& objects)
{
  Database& database = STADM.getDatabase();
  std::set<std::string> roots;
  for (Tcl_Obj* object : objects) {
    const auto type = queryObjectType(object);
    const bool typed = SdcObject::getType(object) != SdcObjectType::kNone;
    const std::string name = Tcl_GetString(object);
    std::vector<std::pair<QueryObjectType, std::string>> matches;
    for (const bool exact : {true, false}) {
      ObjectQueryOptions options;
      options.exact = exact;
      for (auto candidate_type : {QueryObjectType::kPort, QueryObjectType::kPin, QueryObjectType::kNet}) {
        if ((typed || type != QueryObjectType::kAny) && type != candidate_type)
          continue;
        for (const auto& match : typed ? findQueryObjects(interpreter(), database, object, candidate_type, options)
                                       : findObjects(interpreter(), database, {name}, candidate_type, options))
          matches.emplace_back(candidate_type, match);
      }
      // A resolved object is a literal identity. Raw input names also retain
      // an existing literal match before trying pattern expansion.
      if (typed || !matches.empty())
        break;
    }
    if (matches.empty())
      throw std::invalid_argument(std::string(get_cmd_name()) + " pin, port or net not found: " + name);
    for (const auto& [kind, match] : matches) {
      if (kind != QueryObjectType::kNet && database.network().pins().contains(match)) {
        roots.insert(match);
      } else if (kind == QueryObjectType::kPin && database.network().isHierarchyPin(match)) {
        const auto& boundary = database.network().checkedHierarchyPin(match);
        const auto& pins = database.network().nets().at(boundary.net).get_pin_name_list();
        for (const auto& driver : pins)
          for (const auto& load : pins) {
            if (database.network().crossesHierarchyPin(boundary, driver, load))
              roots.insert(_is_fanin ? driver : load);
          }
      } else if (kind == QueryObjectType::kNet) {
        for (const auto& pin_name : database.network().nets().at(database.network().physicalNet(match)).get_pin_name_list()) {
          auto pin = database.network().pins().find(pin_name);
          if (pin != database.network().pins().end() && (_is_fanin || isLoad(pin->second)))
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
  auto instance = database.network().instances().find(pin.get_instance_name());
  if (!pin.get_is_port() && instance != database.network().instances().end()) {
    auto cell = database.liberty().get_cell_map().find(instance->second.get_cell_name());
    if (cell != database.liberty().get_cell_map().end()) {
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
    Pin& pin = database.network().pins().at(pin_name);
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
  auto instance = database.network().instances().find(pin.get_instance_name());
  if (pin.get_is_port() || instance == database.network().instances().end()) {
    return false;
  }
  if (_is_fanin) {
    if (!instance->second.get_is_sequential()) {
      return false;
    }
    if (pin.get_full_name() == instance->second.get_clock_pin_name()) {
      return true;
    }
    auto cell = database.liberty().get_cell_map().find(instance->second.get_cell_name());
    if (cell != database.liberty().get_cell_map().end()) {
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
    auto net = database.network().nets().find(pin.get_net_name());
    if (net != database.network().nets().end()) {
      for (const std::string& other_name : net->second.get_pin_name_list()) {
        auto other = database.network().pins().find(other_name);
        if (other != database.network().pins().end() && other_name != pin.get_full_name() && (fanin ? isDriver(other->second) : isLoad(other->second))
            && isSearchPin(other->second)) {
          adjacent.push_back(other_name);
        }
      }
    }
  }
  auto instance = database.network().instances().find(pin.get_instance_name());
  if (pin.get_is_port() || instance == database.network().instances().end()) {
    return adjacent;
  }
  auto cell = database.liberty().get_cell_map().find(instance->second.get_cell_name());
  if (cell == database.liberty().get_cell_map().end()) {
    return adjacent;
  }
  for (TimingCellArc& arc : cell->second.get_cell_arc_list()) {
    // Fanin includes clock-to-Q up to the register clock pin. Fanout never
    // crosses a sequential element; reset and check arcs are not data paths.
    if (!arc.get_is_timing_graph_arc() || arc.get_is_disable_arc() || arc.get_is_clear_preset_arc() || (!_is_fanin && arc.get_is_clock_arc())) {
      continue;
    }
    if (pin.get_pin_name() != (fanin ? arc.get_sink_port() : arc.get_source_port())) {
      continue;
    }
    std::string other_name = instance->first + ":" + (fanin ? arc.get_source_port() : arc.get_sink_port());
    auto other = database.network().pins().find(other_name);
    if (other != database.network().pins().end() && isSearchPin(other->second)) {
      adjacent.push_back(other_name);
    }
  }
  return adjacent;
}

bool TclGetFan::isSearchPin(Pin& pin)
{
  std::map<std::string, TimingCaseValue>& values = STADM.getDatabase().sdc().get_case_analysis_map();
  auto value = values.find(pin.get_full_name());
  return value == values.end()
         || (value->second != TimingCaseValue::kZero && value->second != TimingCaseValue::kOne && value->second != TimingCaseValue::kStatic);
}

bool TclGetFan::isDriver(Pin& pin)
{
  PinDirection direction = getDirection(pin);
  return direction == PinDirection::kInout || direction == (pin.get_is_port() ? PinDirection::kInput : PinDirection::kOutput);
}

}  // namespace ista::sdc
