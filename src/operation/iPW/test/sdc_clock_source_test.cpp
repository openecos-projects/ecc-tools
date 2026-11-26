// SPDX-License-Identifier: MulanPSL-2.0
#include "DataManager.hpp"
#include "Logger.hpp"
#include "SdcCommand.hpp"

using namespace ipw;

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void eval(const std::string& script)
{
  Tcl_Interp* interp = SdcCommand::getInst().getInterp();
  const int status = Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL);
  require(status == TCL_OK, "SDC failed: " + script + ": " + Tcl_GetStringResult(interp));
}

void buildDesign()
{
  auto& database = PWDM.getDatabase();
  database.set_design_name("clock_sources");
  for (const std::string name : {"clock", "bus[0]", "bus0", "literal*", "literal1"}) {
    auto& pin = database.get_pin_map()[name];
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_direction(PinDirection::kInput);
  }
  for (const std::string instance : {"clock", "u_div"}) {
    database.get_instance_map()[instance].set_cell_name("MACRO");
    auto& pin = database.get_pin_map()[instance + ":Q"];
    pin.set_instance_name(instance);
    pin.set_pin_name("Q");
    pin.set_direction(PinDirection::kOutput);
  }
  database.get_net_map()["clock"].get_driver_pin_list() = {"u_div:Q"};
  auto& cell = database.get_timing_library().get_cell_map()["MACRO"];
  cell.set_library_name("macro_lib");
  cell.get_port_map()["clock"].set_is_input(true);
  cell.get_port_map()["clock"].set_is_clock(true);
  cell.get_port_map()["Q"].set_is_output(true);
}

void testPrimaryClockSources()
{
  auto& clocks = PWDM.getDatabase().get_timing_constraint().get_clock_map();
  eval("create_clock -name clock -period 10 [get_ports clock]");
  require(clocks.at("clock").get_source_list() == std::vector<std::string>{"clock"}, "port identity lost");
  eval("create_clock -name clock -period 12 [get_ports clock]");
  require(clocks.at("clock").get_period() == 12, "clock redefinition failed");
  eval("create_clock -name clock -period 10 clock");
  require(clocks.at("clock").get_source_list() == std::vector<std::string>{"clock"}, "literal port lookup broadened");

  eval("create_clock -name BUS -period 10 [get_ports -exact {bus[0]}]");
  require(clocks.at("BUS").get_source_list() == std::vector<std::string>{"bus[0]"}, "bus name was globbed again");
  eval("create_clock -name LITERAL -period 10 [get_ports -exact {literal*}]");
  require(
      clocks.at("LITERAL").get_source_list() == std::vector<std::string>{"literal*"},
      "literal star was globbed again"
  );
  eval("create_clock -name PATTERN -period 10 {bus*}");
  require(
      clocks.at("PATTERN").get_source_list() == std::vector<std::string>({"bus0", "bus[0]"}),
      "text wildcard lookup changed"
  );

  eval("create_clock -name NET -period 10 [get_nets clock]");
  require(clocks.at("NET").get_source_list() == std::vector<std::string>{"u_div:Q"}, "net resolved as same-named port");
  eval("create_clock -name PIN -period 10 [get_pins clock/Q]");
  require(clocks.at("PIN").get_source_list() == std::vector<std::string>{"clock:Q"}, "pin identity lost");
}

void testGeneratedClockSources()
{
  eval("create_generated_clock -name DIV -master_clock clock -source [get_ports clock] -divide_by 2 [get_pins u_div/Q]"
  );
  auto& clock = PWDM.getDatabase().get_timing_constraint().get_clock_map().at("DIV");
  require(clock.get_source_list() == std::vector<std::string>{"u_div:Q"}, "generated clock target changed");
  require(clock.get_period() == 20 && clock.get_master_clock_name() == "clock", "generated clock relation changed");
}

void testInvalidClockSources()
{
  const auto size = PWDM.getDatabase().get_timing_constraint().get_clock_map().size();
  for (const std::string query :
       {"get_cells clock", "get_lib_pins macro_lib/MACRO/clock", "get_clocks clock", "get_ports -quiet missing"}) {
    const std::string script = "create_clock -name INVALID -period 10 [" + query + "]";
    Tcl_Interp* interp = SdcCommand::getInst().getInterp();
    require(Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL) == TCL_ERROR, "invalid source accepted: " + query);
    require(
        PWDM.getDatabase().get_timing_constraint().get_clock_map().size() == size,
        "invalid clock mutated constraints"
    );
  }
}

}  // namespace

int main()
{
  Logger::initInst();
  DataManager::initInst();
  SdcCommand::initInst();
  int status = 0;
  try {
    buildDesign();
    testPrimaryClockSources();
    testGeneratedClockSources();
    testInvalidClockSources();
  } catch (const std::exception& error) {
    PWLOG.warn(Loc::current(), error.what());
    status = 1;
  }
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
  return status;
}
