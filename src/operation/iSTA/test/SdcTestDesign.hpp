#pragma once

#include <stdexcept>
#include <string>
#include <vector>
// SPDX-License-Identifier: MulanPSL-2.0
#include "DataManager.hpp"
#include "Logger.hpp"
#include "SdcCommand.hpp"

using namespace ista;

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void addPin(const std::string& instance_name, const std::string& port_name, PinDirection direction)
{
  Database& database = STADM.getDatabase();
  std::string name = instance_name.empty() ? port_name : instance_name + ":" + port_name;
  Pin& pin = database.network().pins()[name];
  pin.set_full_name(name);
  pin.set_pin_name(port_name);
  pin.set_instance_name(instance_name);
  pin.set_direction(direction);
  pin.set_is_port(instance_name.empty());
  if (!instance_name.empty()) {
    database.network().instances()[instance_name].get_pin_name_list().push_back(name);
  }
}

void addCellArc(LibertyCell& cell, const std::string& from, const std::string& to, bool is_clock = false, bool is_reset = false)
{
  TimingCellArc arc;
  arc.set_source_port(from);
  arc.set_sink_port(to);
  arc.set_is_clock_arc(is_clock);
  arc.set_is_clear_preset_arc(is_reset);
  cell.get_cell_arc_list().push_back(arc);
}

void addInstance(const std::string& name, const std::string& cell_name, bool sequential)
{
  Database& database = STADM.getDatabase();
  Instance& instance = database.network().instances()[name];
  instance.set_instance_name(name);
  instance.set_cell_name(cell_name);
  instance.set_is_sequential(sequential);
  LibertyCell& cell = database.liberty().get_cell_map()[cell_name];
  for (auto& [port_name, port] : cell.get_port_map()) {
    addPin(name, port_name, port.get_is_input() ? PinDirection::kInput : PinDirection::kOutput);
  }
  if (sequential) {
    instance.set_clock_pin_name(name + ":CK");
    TimingCheckArc check;
    check.set_clock_port(name + ":CK");
    check.set_data_port(name + ":D");
    check.set_check_type(TimingCheckType::kSetup);
    instance.get_check_arc_list().push_back(check);
  }
}

void addNet(const std::string& name, const std::vector<std::string>& pins)
{
  Database& database = STADM.getDatabase();
  Net& net = database.network().nets()[name];
  net.set_net_name(name);
  net.set_pin_name_list(pins);
  // Deliberately leave driver/load lists empty, as during SDC input.
  for (const std::string& pin : pins) {
    database.network().pins()[pin].set_net_name(name);
  }
}

void buildDesign()
{
  Database& database = STADM.getDatabase();
  database.network().setName("sdc_test");
  database.liberty().set_current_unit_name("1mA");
  database.liberty().set_voltage_unit_name("1V");
  for (std::string name : {"FF", "COMB"}) {
    LibertyCell& cell = database.liberty().get_cell_map()[name];
    cell.set_is_sequential(name == "FF");
    std::vector<std::string> inputs = name == "FF" ? std::vector<std::string>{"CK", "D", "RN"} : std::vector<std::string>{"A", "B", "UNUSED"};
    for (const std::string& input : inputs) {
      LibertyPort& port = cell.get_port_map()[input];
      port.set_port_name(input);
      port.set_is_input(true);
      port.set_is_clock(input == "CK");
    }
    std::string output = name == "FF" ? "Q" : "Y";
    cell.get_port_map()[output].set_port_name(output);
    cell.get_port_map()[output].set_is_output(true);
    if (name == "FF") {
      addCellArc(cell, "CK", "Q", true);
      addCellArc(cell, "RN", "Q", false, true);
    } else {
      addCellArc(cell, "A", "Y");
      addCellArc(cell, "B", "Y");
      addCellArc(cell, "UNUSED", "Y");
      cell.get_cell_arc_list().back().set_is_disable_arc(true);
    }
  }
  for (std::string name : {"cdc/source_reg", "cdc/sink_reg", "outside_reg", "after_reg"}) {
    addInstance(name, "FF", true);
  }
  for (std::string name : {"cdc/buf", "cdc/reconv", "loop", "cell with space"}) {
    addInstance(name, "COMB", false);
  }
  for (std::string name : {"clk", "in", "reset"}) {
    addPin("", name, PinDirection::kInput);
  }
  addPin("", "out", PinDirection::kOutput);
  addNet("clock_net", {"clk", "cdc/source_reg:CK", "cdc/sink_reg:CK", "outside_reg:CK", "after_reg:CK"});
  addNet("input_net", {"in", "cdc/source_reg:D"});
  addNet("reset_net", {"reset", "cdc/source_reg:RN"});
  addNet("cdc/async_data[0]", {"cdc/source_reg:Q", "cdc/buf:A", "cdc/buf:B", "cdc/reconv:B"});
  addNet("buf_net", {"cdc/buf:Y", "cdc/reconv:A"});
  addNet("sink_net", {"cdc/reconv:Y", "cdc/sink_reg:D", "outside_reg:D", "out"});
  addNet("after_net", {"cdc/sink_reg:Q", "after_reg:D"});
  addNet("loop_net", {"loop:Y", "loop:A", "loop:B"});
  // Liberty overrides an imprecise physical pin direction without mutating IDB.
  database.network().pins()["cdc/source_reg:Q"].set_direction(PinDirection::kInout);
}

void eval(const std::string& script)
{
  SdcCommand& interpreter = SdcCommand::getInst();
  int status = interpreter.evalString(script);
  if (status != TCL_OK) {
    const char* info = Tcl_GetVar(interpreter.getInterp(), "errorInfo", TCL_GLOBAL_ONLY);
    throw std::runtime_error("SDC failed: " + std::string(info ? info : Tcl_GetStringResult(interpreter.getInterp())));
  }
}
