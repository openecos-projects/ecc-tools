// SPDX-License-Identifier: MulanPSL-2.0
#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

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
  require(status == TCL_OK, "SDC failed: " + std::string(Tcl_GetStringResult(interpreter.getInterp())));
}

void testUnits()
{
  eval("set_units -time ns -resistance kOhm -capacitance pF -voltage V -current mA");
  eval("set_units -time 1000ps -capacitance 1000fF -distance um -power W");
  eval("create_clock -name clk -period 10 [get_ports clk]");
  eval("set_units -time ps");
  eval("set_units -time 2ns -capacitance 0.5pF");
  eval("create_clock -name after_units -period 5");
  require(STADM.getDatabase().sdc().get_clock_map()["clk"].get_period() == 10.0, "set_units changed an existing clock");
  require(STADM.getDatabase().sdc().get_clock_map()["after_units"].get_period() == 5.0, "set_units rescaled a new clock");
  for (std::string script :
       {"set_units -time ns_bad", "set_units -time 0ns", "set_units -time -1ns", "set_units -capacitance nS", "set_units -current", "set_units -typo ns"}) {
    require(SdcCommand::getInst().evalString(script) == TCL_ERROR, "invalid units accepted: " + script);
  }
}

void testQueries()
{
  eval(R"TCL(
    proc check {condition} {
      if {![uplevel 1 [list expr $condition]]} {error "assertion failed: $condition"}
    }
    check {[get_fanin -to {} -flat -startpoints_only -only_cells] eq {}}
    check {[get_fanout -from {} -flat -endpoints_only -only_cells] eq {}}
    check {[get_fanin -to [get_pins cdc/reconv/Y] -flat -startpoints_only] eq {cdc/source_reg:CK}}
    check {[get_fanin -to [get_nets {cdc/async_data[0]}] -flat -startpoints_only -only_cells] eq {cdc/source_reg}}
    check {[get_fanin -to [get_pins cdc/source_reg/D] -flat -startpoints_only] eq {in}}
    check {[get_fanout -from [get_pins cdc/source_reg/Q] -flat -endpoints_only] eq {cdc/sink_reg:D out outside_reg:D}}
    check {[get_fanout -from [get_nets {cdc/async_data[0]}] -flat -endpoints_only -only_cells] eq {cdc/sink_reg outside_reg}}
    check {[get_fanout -from [get_pins cdc/sink_reg/D] -flat -endpoints_only -only_cells] eq {cdc/sink_reg}}
    check {[get_fanin -to [get_pins loop/Y] -flat -startpoints_only] eq {}}
    check {[get_fanout -from [get_pins loop/Y] -flat -endpoints_only] eq {}}
    check {[get_fanout -from [get_pins loop/Y] -flat -only_cells] eq {loop}}
    check {[llength [get_fanin -to [get_pins cdc/reconv/Y] -flat]] == 8}
    check {[get_fanout -from [get_pins outside_reg/Q] -flat -endpoints_only] eq {}}
    check {[get_name [get_cells cdc/source_reg]] eq {cdc/source_reg}}
    check {[get_name [get_pins cdc/source_reg/Q]] eq {Q}}
    check {[get_name [get_cells [list {cell with space}]]] eq {cell with space}}
    foreach cell [get_cells [list {cell with space}]] {
      check {[get_name $cell] eq {cell with space}}
    }
    check {[get_name [get_nets {cdc/async_data[0]}]] eq {cdc/async_data[0]}}
  )TCL");
  for (std::string script : {"get_fanin", "get_fanout", "get_fanin -from in", "get_fanin -to no_such_pin", "get_fanout -from cdc/source_reg",
                             "get_fanout -from in -trace_arcs invalid", "get_name {}", "get_name {cdc/source_reg outside_reg}", "get_name missing"}) {
    require(SdcCommand::getInst().evalString(script) == TCL_ERROR, "invalid query accepted: " + script);
  }
  require(STADM.getDatabase().graph().arcs().empty(), "SDC queries changed the analysis graph");
  require(STADM.getDatabase().network().pins()["cdc/source_reg:Q"].get_direction() == PinDirection::kInout, "SDC queries changed pin direction");
}

void testCdcException()
{
  eval(R"TCL(
    set CDC cdc
    set START [get_fanin -to [get_nets $CDC/*async_data*] -flat -startpoints_only -only_cells]
    set START [concat $START [get_cells $CDC/*source_reg]]
    set END [list]
    set CANDIDATES [get_fanout -from [get_nets $CDC/*async*data*] -flat -endpoints_only -only_cells]
    foreach cell $CANDIDATES {
      if {[string match "${CDC}/*" [get_name $cell]]} {lappend END $cell}
    }
    lappend END {*}[get_fanout -from [get_pins $CDC/source_reg/Q] -flat -endpoints_only -only_cells]
    set_max_delay 3.0 -from $START -to $END -ignore_clock_latency
  )TCL");
  std::vector<TimingException>& exceptions = STADM.getDatabase().sdc().get_path_exception_list();
  require(exceptions.size() == 1, "CDC exception missing");
  require(exceptions[0].get_from_objects() == ExceptionObjects{{ExceptionObjectType::kCell, "cdc/source_reg"}}, "CDC launch cells incorrect");
  require(exceptions[0].get_to_objects() == ExceptionObjects{{ExceptionObjectType::kCell, "cdc/sink_reg"}, {ExceptionObjectType::kCell, "outside_reg"}},
          "CDC capture cells incorrect");
  require(exceptions[0].get_delay() == 3.0 && exceptions[0].get_ignore_clock_latency(), "CDC delay qualifiers lost");
}

void testLoadObjectIdentity()
{
  Database& database = STADM.getDatabase();
  addPin("", "package", PinDirection::kInout);
  addNet("package", {"package"});
  addNet("inner/package", {});
  eval("set port [get_ports package]; set net [get_nets -filter {full_name == package}]; set_load 0.7 $port");
  Sdc& constraint = database.sdc();
  require(constraint.get_net_load_map().empty(), "port load also applied to same-name nets");
  require(constraint.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.7, "port load missing");
  eval("foreach item $net {set_load 0.2 $item}");
  require(constraint.get_net_load_map().size() == 1 && constraint.get_net_load_map().contains("package"), "explicit net collection lost its type");
  require(constraint.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.7, "net load changed a same-name port");
  eval("set_load 0.3 [get_nets inner/package]");
  require(constraint.get_net_load_map().at("inner/package").get(AnalysisType::kMax, TransType::kRise) == 0.3, "hierarchical net load missing");
  eval("set_load 0.8 [lindex $port 0]");
  require(constraint.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.8, "scalar collection element lost its type");
  require(constraint.get_net_load_map().at("package").get(AnalysisType::kMax, TransType::kRise) == 0.2, "port element overwrote net load");
  eval("set_load 0.4 [concat $port $net]");
  require(constraint.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.4
              && constraint.get_net_load_map().at("package").get(AnalysisType::kMax, TransType::kRise) == 0.4,
          "mixed port/net collection was not preserved");
}

int main()
{
  Logger::initInst();
  DataManager::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  int status = 0;
  try {
    buildDesign();
    testUnits();
    testQueries();
    testCdcException();
    testLoadObjectIdentity();
    STALOG.info(Loc::current(), "SDC compatibility tests passed");
  } catch (const std::exception& error) {
    STALOG.warn(Loc::current(), error.what());
    status = 1;
  }
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
  return status;
}
