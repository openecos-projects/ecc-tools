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
#include "DataManager.hpp"
#include "object/ObjectQuery.hpp"
#include "delay/DelayCommands.hpp"

namespace ipw::sdc {

TclSetDrivingCell::TclSetDrivingCell(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("-lib_cell", 0));
  addOption(new ecc::TclStringOption("-cell", 0));
  addOption(new ecc::TclStringOption("-library", 0));
  addOption(new ecc::TclStringOption("-pin", 0));
  addOption(new ecc::TclStringOption("-from_pin", 0));
  addOption(new ecc::TclDoubleOption("-input_transition_rise", 0));
  addOption(new ecc::TclDoubleOption("-input_transition_fall", 0));
  addOption(new ecc::TclDoubleOption("-multiply_by", 0));
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclSwitchOption("-no_design_rule"));
  addOption(new ecc::TclSwitchOption("-dont_scale"));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetDrivingCell::exec()
{
  if (getOptionOrArg("-multiply_by")->is_set_val()) warn("set_driving_cell -multiply_by is accepted for compatibility and ignored");
  if (getOptionOrArg("-dont_scale")->is_set_val()) warn("set_driving_cell -dont_scale is accepted for compatibility and ignored");
  if (getOptionOrArg("-no_design_rule")->is_set_val()) warn("set_driving_cell -no_design_rule is accepted for compatibility and ignored");

  ecc::TclOption* cell_option = getOptionOrArg("-lib_cell");
  ecc::TclOption* cell_alias_option = getOptionOrArg("-cell");
  if (!cell_option->is_set_val() && cell_alias_option->is_set_val()) {
    cell_option = cell_alias_option;
  }
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!cell_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_driving_cell requires -lib_cell or -cell and an input port collection");
    return 0;
  }

  Database& database = PWDM.getDatabase();
  const std::string cell_name = cell_option->getStringVal();
  std::map<std::string, TimingCell>& cell_map = database.get_timing_library().get_cell_map();
  if (cell_name.empty() || !cell_map.contains(cell_name)) {
    setTclError("set_driving_cell library cell does not exist: " + cell_name);
    return 0;
  }
  TimingCell& timing_cell = cell_map.at(cell_name);

  ecc::TclOption* library_option = getOptionOrArg("-library");
  if (library_option->is_set_val() && timing_cell.get_library_name() != library_option->getStringVal()) {
    setTclError("set_driving_cell cell '" + cell_name + "' is not in library '" + library_option->getStringVal() + "'");
    return 0;
  }

  ecc::TclOption* pin_option = getOptionOrArg("-pin");
  std::string to_pin;
  if (pin_option->is_set_val()) {
    to_pin = pin_option->getStringVal();
    if (!timing_cell.get_port_map().contains(to_pin) || !timing_cell.get_port_map().at(to_pin).get_is_output()) {
      setTclError("set_driving_cell output pin does not exist: " + cell_name + "/" + to_pin);
      return 0;
    }
  } else {
    for (auto& [port_name, port] : timing_cell.get_port_map()) {
      if (port.get_is_output()) {
        if (!to_pin.empty()) {
          setTclError("set_driving_cell requires -pin for a cell with multiple outputs: " + cell_name);
          return 0;
        }
        to_pin = port_name;
      }
    }
    if (to_pin.empty()) {
      setTclError("set_driving_cell cell has no output pin: " + cell_name);
      return 0;
    }
  }

  ecc::TclOption* from_pin_option = getOptionOrArg("-from_pin");
  const std::string requested_from_pin = from_pin_option->is_set_val() ? from_pin_option->getStringVal() : std::string{};
  if (!requested_from_pin.empty()
      && (!timing_cell.get_port_map().contains(requested_from_pin) || !timing_cell.get_port_map().at(requested_from_pin).get_is_input())) {
    setTclError("set_driving_cell input pin does not exist: " + cell_name + "/" + requested_from_pin);
    return 0;
  }
  std::string from_pin;
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_sink_port() != to_pin || (!requested_from_pin.empty() && timing_cell_arc.get_source_port() != requested_from_pin)) {
      continue;
    }
    if (!timing_cell.get_port_map().contains(timing_cell_arc.get_source_port())
        || !timing_cell.get_port_map().at(timing_cell_arc.get_source_port()).get_is_input()) {
      continue;
    }
    from_pin = timing_cell_arc.get_source_port();
    break;
  }
  if (from_pin.empty()) {
    setTclError("set_driving_cell has no timing arc to output pin: " + cell_name + "/" + to_pin);
    return 0;
  }

  ecc::TclOption* rise_transition_option = getOptionOrArg("-input_transition_rise");
  ecc::TclOption* fall_transition_option = getOptionOrArg("-input_transition_fall");
  const double rise_transition = rise_transition_option->is_set_val() ? rise_transition_option->getDoubleVal() : 0.0;
  const double fall_transition = fall_transition_option->is_set_val() ? fall_transition_option->getDoubleVal() : 0.0;
  if (!std::isfinite(rise_transition) || rise_transition < 0.0 || !std::isfinite(fall_transition) || fall_transition < 0.0) {
    setTclError("set_driving_cell input transitions must be finite and non-negative");
    return 0;
  }

  const std::vector<std::string>& object_list = object_option->getStringList();
  const std::vector<std::string> port_list = getPortPinNames(database, object_list);
  if (port_list.empty()) {
    setTclError("set_driving_cell requires at least one input port");
    return 0;
  }
  if (port_list.size() != object_list.size()) {
    setTclError("set_driving_cell contains an invalid port object");
    return 0;
  }
  for (const std::string& port_name : port_list) {
    Pin& pin = database.get_pin_map().at(port_name);
    if (!pin.get_is_port() || (pin.get_direction() != PinDirection::kInput && pin.get_direction() != PinDirection::kInout)) {
      setTclError("set_driving_cell requires input ports; '" + port_name + "' is not an input port");
      return 0;
    }
  }

  TimingDrivingCell driving_cell;
  driving_cell.set_library_name(timing_cell.get_library_name());
  driving_cell.set_cell_name(cell_name);
  driving_cell.set_from_pin(from_pin);
  driving_cell.set_to_pin(to_pin);
  driving_cell.set_input_transition_rise(rise_transition);
  driving_cell.set_input_transition_fall(fall_transition);

  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  for (const std::string& port_name : port_list) {
    TimingPortConstraint& port_constraint = getOrCreatePortConstraint(database, port_name);
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis_type == AnalysisType::kMin && max && !min) || (analysis_type == AnalysisType::kMax && min && !max)) {
        continue;
      }
      if (!fall || rise) {
        port_constraint.set_driving_cell(analysis_type, TransType::kRise, driving_cell);
      }
      if (!rise || fall) {
        port_constraint.set_driving_cell(analysis_type, TransType::kFall, driving_cell);
      }
    }
  }
  return 1;
}

}  // namespace ipw::sdc
