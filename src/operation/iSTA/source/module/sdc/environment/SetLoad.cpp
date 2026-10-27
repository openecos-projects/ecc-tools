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
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "environment/EnvironmentCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

namespace {

struct LoadTargets
{
  std::set<std::string> ports;
  std::set<std::string> nets;
};

std::vector<AnalysisType> getAnalysisTypes(SdcTclCmd& command)
{
  const bool min = command.getOptionOrArg("-min")->is_set_val();
  const bool max = command.getOptionOrArg("-max")->is_set_val();
  std::vector<AnalysisType> result;
  if (!max || min) {
    result.push_back(AnalysisType::kMin);
  }
  if (!min || max) {
    result.push_back(AnalysisType::kMax);
  }
  return result;
}

std::vector<TransType> getTransitionTypes(SdcTclCmd& command)
{
  const bool rise = command.getOptionOrArg("-rise")->is_set_val();
  const bool fall = command.getOptionOrArg("-fall")->is_set_val();
  std::vector<TransType> result;
  if (!fall || rise) {
    result.push_back(TransType::kRise);
  }
  if (!rise || fall) {
    result.push_back(TransType::kFall);
  }
  return result;
}

std::vector<TransType> allTransTypes()
{
  return {TransType::kRise, TransType::kFall};
}

LoadTargets findLoadTargets(Tcl_Interp* interp, Database& database, const std::vector<Tcl_Obj*>& objects)
{
  LoadTargets targets;
  for (Tcl_Obj* object : objects) {
    std::string name = Tcl_GetString(object);
    SdcObjectType type = SdcObject::getType(object);
    if (type == SdcObjectType::kPort) {
      auto pin = database.network().pins().find(name);
      if (pin == database.network().pins().end() || !pin->second.get_is_port()) {
        throw std::invalid_argument("set_load port no longer exists: " + name);
      }
      targets.ports.insert(name);
      continue;
    }
    if (type == SdcObjectType::kNet) {
      if (!database.network().hasNet(name)) {
        throw std::invalid_argument("set_load net no longer exists: " + name);
      }
      targets.nets.insert(database.network().physicalNet(name));
      continue;
    }
    if (type != SdcObjectType::kNone)
      throw std::invalid_argument("set_load requires port or net objects: " + name);
    // Bare names resolve to ports first. Typed get_nets results remain nets
    // even when a top-level port has the same spelling.
    std::vector<std::string> ports = findObjects(interp, database, {name}, QueryObjectType::kPort);
    if (!ports.empty()) {
      targets.ports.insert(ports.begin(), ports.end());
      continue;
    }
    std::vector<std::string> nets = findObjects(interp, database, {name}, QueryObjectType::kNet);
    if (nets.empty()) {
      throw std::invalid_argument("set_load object is not a port or net: " + name);
    }
    for (const auto& net : nets)
      targets.nets.insert(database.network().physicalNet(net));
  }
  if (targets.ports.empty() && targets.nets.empty()) {
    throw std::invalid_argument("set_load requires a non-empty port or net collection");
  }
  return targets;
}

double getLibraryPinCapacitance(Database& database, const std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  const auto pin_iter = database.network().pins().find(pin_name);
  if (pin_iter == database.network().pins().end()) {
    return 0.0;
  }
  if (pin_iter->second.get_is_port()) {
    auto& port_constraints = database.sdc().get_port_constraint_map();
    const auto constraint = port_constraints.find(pin_name);
    return constraint != port_constraints.end() && constraint->second.get_has_load() ? constraint->second.get_load(analysis_type, trans_type) : 0.0;
  }
  Pin& pin = pin_iter->second;
  const auto instance_iter = database.network().instances().find(pin.get_instance_name());
  if (instance_iter == database.network().instances().end()) {
    return 0.0;
  }
  const auto cell_iter = database.liberty().get_cell_map().find(instance_iter->second.get_cell_name());
  if (cell_iter == database.liberty().get_cell_map().end()) {
    return 0.0;
  }
  const auto port_iter = cell_iter->second.get_port_map().find(pin.get_pin_name());
  if (port_iter == cell_iter->second.get_port_map().end()) {
    return 0.0;
  }
  LibertyPort& cell_port = port_iter->second;
  if (cell_port.get_trans_capacitance_map().contains(analysis_type) && cell_port.get_trans_capacitance_map().at(analysis_type).contains(trans_type)) {
    return cell_port.get_trans_capacitance_map().at(analysis_type).at(trans_type);
  }
  return cell_port.get_capacitance();
}

}  // namespace

double subtractNetLoadPinCapacitance(Database& database, const std::string& net_name, double load_value, AnalysisType analysis_type, TransType trans_type)
{
  const auto net_iter = database.network().nets().find(net_name);
  if (net_iter == database.network().nets().end()) {
    return load_value;
  }
  double adjusted = load_value;
  const auto& loads = net_iter->second.get_load_pin_list();
  std::set<std::string> capacitive_pins(loads.begin(), loads.end());
  // Input ports are driver-only after graph construction, but their explicit
  // load still contributes to the network capacitance. Deduplicate inout pins.
  for (const auto& name : net_iter->second.get_driver_pin_list()) {
    const auto pin = database.network().pins().find(name);
    if (pin != database.network().pins().end() && (pin->second.get_is_port() || pin->second.get_direction() == PinDirection::kInout))
      capacitive_pins.insert(name);
  }
  for (const std::string& pin_name : capacitive_pins) {
    adjusted -= getLibraryPinCapacitance(database, pin_name, analysis_type, trans_type);
  }
  return std::max(0.0, adjusted);
}

TclSetLoad::TclSetLoad(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-pin_load"));
  addOption(new ecc::TclSwitchOption("-wire_load"));
  addOption(new ecc::TclSwitchOption("-subtract_pin_load"));
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclDoubleOption("load", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetLoad::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* load_option = getOptionOrArg("load");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!load_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_load requires a load and an object collection");
    return 0;
  }

  const double load_value = capValue("load");
  if (!std::isfinite(load_value) || load_value < 0.0) {
    setTclError("set_load requires a finite non-negative load");
    return 0;
  }
  Database& database = data_manager.getDatabase();
  LoadTargets targets;
  try {
    targets = findLoadTargets(interpreter(), database, getOptionObjects("objects"));
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  if (!targets.nets.empty()) {
    if (getOptionOrArg("-rise")->is_set_val() || getOptionOrArg("-fall")->is_set_val()) {
      warn("set_load -rise/-fall are ignored for net objects");
    }
    if (getOptionOrArg("-pin_load")->is_set_val()) {
      warn("set_load -pin_load is ignored for net objects");
    }
    if (getOptionOrArg("-wire_load")->is_set_val()) {
      warn("set_load -wire_load is ignored for net objects");
    }
  }
  if (!targets.ports.empty() && getOptionOrArg("-subtract_pin_load")->is_set_val()) {
    warn("set_load -subtract_pin_load is ignored for port objects");
  }

  // With both load-kind options present, use the default port pin load. Wire
  // load is selected only when it is the sole load-kind option.
  const bool wire_load = getOptionOrArg("-wire_load")->is_set_val() && !getOptionOrArg("-pin_load")->is_set_val();
  const bool subtract_pin_load = getOptionOrArg("-subtract_pin_load")->is_set_val();
  for (AnalysisType analysis_type : getAnalysisTypes(*this)) {
    for (TransType trans_type : getTransitionTypes(*this)) {
      for (const std::string& port_name : targets.ports) {
        TimingPortConstraint& port_constraint = getOrCreatePortConstraint(database, port_name);
        port_constraint.set_load(analysis_type, trans_type, load_value, wire_load);
      }
    }
    for (TransType trans_type : allTransTypes()) {
      for (const std::string& net_name : targets.nets) {
        const double effective_load = subtract_pin_load ? subtractNetLoadPinCapacitance(database, net_name, load_value, analysis_type, trans_type) : load_value;
        database.sdc().get_net_load_map()[net_name].set(analysis_type, trans_type, effective_load);
      }
    }
  }
  return 1;
}

}  // namespace ista::sdc
