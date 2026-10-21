// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "SdcCommand.hpp"

int main()
{
  using namespace ista;
  DataManager::initInst();
  DelayCalculator::initInst();
  SdcCommand::initInst();
  auto& interpreter = SdcCommand::getInst();
  sdc::registerSdcCommands(interpreter);
  auto& database = STADM.getDatabase();
  auto& port = database.get_pin_map()["io"];
  port.set_is_port(true);
  port.set_direction(PinDirection::kInout);
  port.set_full_name("io");
  database.get_net_map()["io"].set_net_name("io");
  database.get_net_map()["io"].set_driver_pin("io");
  database.get_net_map()["io"].set_load_pin_list({"receiver:I"});
  port.set_net_name("io");
  database.get_instance_map()["receiver"].set_cell_name("PAD");
  database.get_pin_map()["receiver:I"].set_instance_name("receiver");
  database.get_pin_map()["receiver:I"].set_pin_name("I");
  database.get_net_map()["wire"].set_net_name("wire");
  auto& cell = database.get_timing_library().get_cell_map()["PAD"];
  cell.set_cell_name("PAD");
  cell.set_library_name("lib");
  for (const auto& name : {"I", "OE", "Y"}) {
    auto& pin = cell.get_port_map()[name];
    pin.set_is_input(name != std::string("Y"));
    pin.set_is_output(name == std::string("Y"));
    pin.set_declaration_index(name == std::string("OE") ? 0 : 1);
  }
  for (const auto& source : {"I", "OE"}) {
    TimingCellArc arc;
    arc.set_source_port(source);
    arc.set_sink_port("Y");
    cell.get_cell_arc_list().push_back(arc);
  }
  if (interpreter.evalString(
          "set_load 1.0 [get_ports io]; set_load 2.0 [get_nets wire]; set_driving_cell -lib_cell PAD -pin Y [get_ports io]")
      != TCL_OK) {
    throw std::runtime_error("I/O SDC command failed");
  }
  auto& constraints = database.get_timing_constraint();
  auto& io = constraints.get_port_constraint_map().at("io");
  if (constraints.get_net_load_map().contains("io") || !constraints.get_net_load_map().contains("wire")
      || io.get_load(AnalysisType::kMax, TransType::kRise) != 1.0) {
    throw std::runtime_error("port load must not also constrain its same-named net");
  }
  const auto drive = io.get_driving_cell(AnalysisType::kMax, TransType::kRise);
  if (!drive || drive->get_from_pin() != "OE")
    throw std::runtime_error("default driver must use Liberty input declaration order");
  if (interpreter.evalString("set_driving_cell -lib_cell PAD -pin Y -from_pin I [get_ports io]") != TCL_OK
      || io.get_driving_cell(AnalysisType::kMax, TransType::kRise)->get_from_pin() != "I") {
    throw std::runtime_error("explicit driver input must take precedence");
  }
  cell.get_port_map()["I"].set_capacitance(2.0);
  TimingArc variant;
  variant.set_sense(TimingArcSense::kPositive);
  TimingTable table;
  table.set_axis_list({{0, 10}});
  table.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
  table.set_value_list({0, 10});
  variant.get_delay_table_map()[TransType::kRise] = table;
  variant.get_slew_table_map()[TransType::kRise] = table;
  cell.get_cell_arc_list().front().set_timing_arc_list({variant});
  TimingArc previous = variant;
  table.set_value_list({0, 50});
  previous.get_delay_table_map()[TransType::kRise] = table;
  previous.get_slew_table_map()[TransType::kRise] = table;
  cell.get_cell_arc_list().front().set_timing_arc_list({previous, variant});
  DCTimingResult result;
  std::string output = "io";
  if (!STADC.calculateDrivingCell(output, cell.get_cell_arc_list().front(), AnalysisType::kMax, TransType::kRise, 0, 0, result)
      || result.get_delay() != 3.0 || result.get_slew() != 3.0) {
    throw std::runtime_error("input driving cell must count its own external port load exactly once");
  }
  SdcCommand::destroyInst();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}
