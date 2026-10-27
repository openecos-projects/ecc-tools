#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ReadLiberty.hpp"
// SPDX-License-Identifier: MulanPSL-2.0
#include "ClockPropagator.hpp"
#include "DataManager.hpp"
#include "DataPropagator.hpp"
#include "DelayCalculator.hpp"
#include "GraphBuilder.hpp"
#include "GraphConnectivityBuilder.hpp"
#include "Lib.hh"
#include "LibParserCpp.hh"
#include "Logger.hpp"
#include "PathAnalyzer.hpp"
#include "PathChecker.hpp"
#include "RequiredPropagator.hpp"
#include "STAInterface.hpp"
#include "SdcCommand.hpp"

using namespace ista;

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void command(const std::string& text)
{
  require(SdcCommand::getInst().evalString(text) == TCL_OK, "SDC failed: " + text);
}

void addPin(const std::string& name, PinDirection direction, bool is_port)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.network().pins()[name];
  pin.set_direction(direction);
  pin.set_is_port(is_port);
  pin.set_pin_name(is_port ? name : name.substr(name.find(':') + 1));
  pin.set_full_name(name);
  if (!is_port) {
    pin.set_instance_name("buffer");
    database.network().instances()["buffer"].get_pin_name_list().push_back(name);
  }
}

void addNet(const std::string& name, const std::vector<std::string>& pins)
{
  Database& database = STADM.getDatabase();
  Net& net = database.network().nets()[name];
  net.set_net_name(name);
  net.set_pin_name_list(pins);
  for (const std::string& pin : pins) {
    database.network().pins()[pin].set_net_name(name);
  }
}

void addCellArc(const std::string& from, const std::string& to, const std::string& condition, double value)
{
  Database& database = STADM.getDatabase();
  TimingCellArc cell_arc;
  cell_arc.set_source_port(from);
  cell_arc.set_sink_port(to);
  TimingArc timing_arc;
  timing_arc.set_sense(TimingArcSense::kPositive);
  timing_arc.set_when_expression(ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).readLogicExpression(condition));
  TimingTable table;
  table.set_value_list({value});
  for (TransType trans : {TransType::kRise, TransType::kFall}) {
    timing_arc.get_delay_table_map()[trans] = table;
    timing_arc.get_slew_table_map()[trans] = table;
  }
  timing_arc.set_arc_idx(0);
  cell_arc.get_timing_arc_list().push_back(timing_arc);
  // Same cell, same endpoints, but a disabled variant must not win lookup.
  timing_arc.set_when_expression(ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).readLogicExpression("!receive"));
  table.set_value_list({99.0});
  for (TransType trans : {TransType::kRise, TransType::kFall}) {
    timing_arc.get_delay_table_map()[trans] = table;
    timing_arc.get_slew_table_map()[trans] = table;
  }
  timing_arc.set_arc_idx(1);
  cell_arc.get_timing_arc_list().push_back(timing_arc);
  database.liberty().get_cell_map()["boundary"].get_cell_arc_list().push_back(cell_arc);
}

void checkMode(int32_t output_enable, bool clock_mode, bool constant_data = false, bool supply_control = false)
{
  DataManager::initInst();
  DelayCalculator::initInst();
  Database& database = STADM.getDatabase();
  STADM.getConfig().thread_number = 1;
  database.network().setName("bidir_" + std::to_string(output_enable) + (clock_mode ? "_clock" : "_data"));
  Instance& instance = database.network().instances()["buffer"];
  instance.set_instance_name("buffer");
  instance.set_cell_name("boundary");
  LibertyCell& cell = database.liberty().get_cell_map()["boundary"];
  for (const std::string name : {"contact", "send", "receive", "data", "sense"}) {
    LibertyPort& port = cell.get_port_map()[name];
    port.set_is_input(name != "sense");
    port.set_is_output(name == "sense" || name == "contact");
    if (name == "contact") {
      port.set_function_expression(ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).readLogicExpression("data"));
      port.set_three_state_expression(ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).readLogicExpression("!send"));
      port.set_capacitance(0.03);
    } else if (name == "sense") {
      port.set_function_expression(ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).readLogicExpression("contact & receive"));
    }
    addPin("buffer:" + name, name == "contact" ? PinDirection::kInout : name == "sense" ? PinDirection::kOutput : PinDirection::kInput, false);
  }
  addPin("external", PinDirection::kInout, true);
  addPin("core_tx", PinDirection::kInput, true);
  addPin("core_rx", PinDirection::kOutput, true);
  addPin("enable", PinDirection::kInput, true);
  addPin("receive_enable", PinDirection::kInput, true);
  addNet("pad_net", {"external", "buffer:contact"});
  addNet("out_net", {"core_tx", "buffer:data"});
  addNet("in_net", {"buffer:sense", "core_rx"});
  addNet("enable_net", {"enable", "buffer:send"});
  addNet("receive_net", {"receive_enable", "buffer:receive"});
  addCellArc("data", "contact", "receive", 0.2);
  addCellArc("contact", "sense", "receive", 0.1);
  // Older physical libraries can describe the pad as INPUT; Liberty owns its logical roles.
  database.network().pins().at("buffer:contact").set_direction(PinDirection::kInput);
  command("if {[lsearch -exact [get_fanout -from [get_ports core_tx] -flat] external] < 0} {error {inout fanout lost}}");
  command("if {[lsearch -exact [get_fanin -to [get_ports core_rx] -flat] external] < 0} {error {inout fanin lost}}");
  command("create_clock -name virtual_clock -period 10");
  command("set_input_delay -clock virtual_clock 1 [get_ports {external core_tx}]");
  command("set_output_delay -clock virtual_clock 2 [get_ports {external core_rx}]");
  command("set_input_transition 0.25 [get_ports {external core_tx}]");
  command("set_load 0.07 [get_ports external]");
  command("set_case_analysis 1 [get_ports receive_enable]");
  if (output_enable >= 0) {
    if (supply_control) {
      database.network().pins().at("buffer:send").set_constant_value(output_enable == 0 ? TimingCaseValue::kZero : TimingCaseValue::kOne);
    } else {
      command("set_case_analysis " + std::to_string(output_enable) + " [get_ports enable]");
    }
  }
  if (constant_data) {
    command("set_case_analysis 0 [get_ports core_tx]");
  }
  if (clock_mode) {
    command("create_clock -name package_clock -period 12 [get_ports external]");
    command("set_propagated_clock [get_clocks package_clock]");
    command("create_generated_clock -name forwarded -source [get_ports external] -divide_by 1 [get_pins buffer/sense]");
    command("set_propagated_clock [get_clocks forwarded]");
  }

  // Connectivity owns normalization and direction inference, including the
  // Liberty INPUT -> INOUT correction. Later graph phases must remain separate.
  Database connectivity = database;
  GraphConnectivityBuilder(connectivity).build();
  auto& contact = connectivity.network().pins().at("buffer:contact");
  require(contact.get_direction() == PinDirection::kInout, "connectivity skipped Liberty direction normalization");
  require(contact.get_load_vertex_idx() != contact.get_driver_vertex_idx(), "connectivity lost INOUT vertex separation");
  require(connectivity.graph().vertices().size() == 12, "connectivity did not construct all vertices");
  require(connectivity.graph().endpoints().empty() && connectivity.graph().vertexOrder().empty(),
          "connectivity must leave classification and ordering to later phases");
  for (auto& arc : connectivity.graph().arcs()) {
    require(!arc.get_is_case_analysis_disable() && !arc.get_is_loop_disable(), "connectivity performed a later graph phase");
  }

  GraphBuilder(STADM.getDatabase()).build();
  require(connectivity.graph().arcs().size() == database.graph().arcs().size(), "full graph changed connectivity arc count");
  for (std::size_t index = 0; index < database.graph().arcs().size(); ++index) {
    auto& topology = connectivity.graph().arcs()[index];
    auto& full = database.graph().arcs()[index];
    require(topology.get_arc_name() == full.get_arc_name() && topology.get_source_vertex_idx() == full.get_source_vertex_idx()
                && topology.get_sink_vertex_idx() == full.get_sink_vertex_idx(),
            "full graph changed connectivity order or vertex bindings");
  }
  STADC.initialize(STADM.getDatabase().graph());
  require(database.network().pins().size() == 10, "graph duplicated physical pins");
  require(database.graph().vertices().size() == 12, "INOUT must have two independent vertices");
  require(database.graph().vertexOrder().size() == 12, "graph cannot be levelized");
  Pin& pad = database.network().pins().at("buffer:contact");
  require(pad.get_load_vertex_idx() != pad.get_driver_vertex_idx(), "INOUT states share vertex");
  require(database.graph().simValues().isDriverDisabled("buffer:contact") == (output_enable == 0), "three_state polarity is wrong");
  bool has_input = false;
  bool has_output = false;
  for (Arc& arc : database.graph().arcs()) {
    require(!arc.get_is_loop_disable(), "bidirectional pin introduced a false loop");
    require(arc.get_source_pin() != arc.get_sink_pin(), "bidirectional readback wire edge was inserted");
    if (arc.get_source_pin() == "external" && arc.get_sink_pin() == "buffer:contact") {
      has_input = !arc.get_is_case_analysis_disable();
      require(arc.get_sink_vertex_idx() == pad.get_load_vertex_idx(), "input wire misses receiver");
    }
    if (arc.get_source_pin() == "buffer:contact" && arc.get_sink_pin() == "external") {
      has_output = !arc.get_is_case_analysis_disable();
      require(arc.get_source_vertex_idx() == pad.get_driver_vertex_idx(), "output wire misses driver");
    }
  }
  require(has_input, "input path was lost");
  require(has_output == (output_enable != 0 && !constant_data), "output-mode path was lost or disabled output propagated");
  STADM.getDatabase().graph().clearTiming();
  ClockPropagator(STADM.getDatabase(), STADC).propagate();
  if (clock_mode) {
    require(database.graph().points().at("buffer:sense").get_is_clock_point(), "top clock did not reach core through PAD");
    require(database.graph().driverPoint("buffer:sense").find_clock_state("forwarded") != nullptr, "generated clock was lost");
  } else {
    DataPropagator(STADM.getDatabase(), STADC).propagate();
    {
      PathChecker check(STADM.getDatabase(), STADC);
      RequiredPropagator(STADM.getDatabase(), check).propagate();
      PathAnalyzer(STADM.getDatabase(), check).analyze();
    }
    bool input_path = false;
    bool output_path = false;
    for (PathGroup& group : database.pathGroups()) {
      for (auto& [end_point, path_end] : group.endpointPaths()) {
        for (TimingPath& path : path_end) {
          input_path = input_path || (path.get_start_point() == "external" && path.get_end_point() == "core_rx");
          output_path = output_path || (path.get_start_point() == "core_tx" && path.get_end_point() == "external");
        }
      }
    }
    require(input_path, "input timing path was not reported");
    require(output_path == (output_enable != 0 && !constant_data), "output timing path mode was wrong");
    TimingPoint& receiver = database.graph().points().at("buffer:contact");
    TimingPoint& driver = database.graph().driverPoint("buffer:contact");
    require(&receiver != &driver, "arrival/slew storage aliases INOUT states");
    require(database.graph().points().at("buffer:sense").get_data_slew_map()[AnalysisType::kMax][TransType::kRise] < 1.0,
            "disabled conditional table affected timing");
  }
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void checkDriverLoad()
{
  DataManager::initInst();
  DelayCalculator::initInst();
  Database& database = STADM.getDatabase();
  STADC.clear();
  Instance& instance = database.network().instances()["buffer"];
  instance.set_instance_name("buffer");
  instance.set_cell_name("boundary");
  LibertyPort& contact = database.liberty().get_cell_map()["boundary"].get_port_map()["contact"];
  contact.set_is_input(true);
  contact.set_is_output(true);
  contact.set_capacitance(0.03);
  addPin("external", PinDirection::kInout, true);
  addPin("buffer:contact", PinDirection::kInout, false);
  addNet("pad_net", {"external", "buffer:contact"});
  Net& net = database.network().nets().at("pad_net");
  net.get_driver_pin_list() = {"external", "buffer:contact"};
  net.get_load_pin_list() = {"external", "buffer:contact"};
  command("set_load 0.07 [get_ports external]");
  ParasiticNet& parasitic = database.parasitics().nets()["pad_net"];
  parasitic.set_net_name("pad_net");
  parasitic.get_node_map()["external"].set_capacitance(0.01);
  parasitic.get_node_map()["buffer:contact"].set_capacitance(0.02);
  ParasiticResistor resistor;
  resistor.set_source_node("external");
  resistor.set_sink_node("buffer:contact");
  resistor.set_resistance(100.0);
  parasitic.get_resistor_list().push_back(resistor);
  for (bool reverse : {false, true, false}) {
    Arc arc;
    arc.set_type(ArcType::kNet);
    arc.set_owner_name("pad_net");
    arc.set_source_pin(reverse ? "buffer:contact" : "external");
    arc.set_sink_pin(reverse ? "external" : "buffer:contact");
    auto result = STADC.arcDelay(arc, AnalysisType::kMax, TransType::kRise, TransType::kRise, std::numeric_limits<double>::quiet_NaN());
    // An ideal input-port step reaches the receiver threshold after ln(2) * RC.
    // The instance driver has no cell waveform here, so it uses Elmore delay.
    double expected = reverse ? 0.008 : 0.005 * std::log(2.0);
    require(std::abs(result->get_delay() - expected) < 1E-9, "RC response reused another driver's root");
  }
  STADC.clear();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void checkLiberty(const char* path)
{
  Database database;
  ReadLiberty conversion(database.liberty(), database.network());
  idb::LibertyReader reader(path);
  require(reader.readLib() && reader.linkLib(), "Liberty parsing failed");
  std::unique_ptr<idb::LibLibrary> library = reader.takeLib();
  idb::LibCell* cell = library->findCell("boundary");
  require(cell != nullptr, "Liberty cell missing");
  for (const char* name : {"contact", "contacts[0]", "contacts[1]"}) {
    idb::LibPort* port = cell->get_cell_port_or_port_bus(name);
    require(port != nullptr && port->get_three_state_expr_str() == "!send", "three_state parse/bus inheritance lost");
    LibertyCell wrapped;
    conversion.readPort(wrapped, port);
    require(wrapped.get_port_map().at(name).get_three_state_expression().evaluate_constant({{"send", false}}) == true, "three_state wrap/polarity failed");
  }
  idb::LibPort moved(std::move(*cell->get_cell_port_or_port_bus("contact")));
  require(moved.get_three_state_expr_str() == "!send" && moved.get_func_expr_str() == "data", "LibPort move lost expressions");
  library.reset();
  require(moved.get_func_expr() != nullptr && std::string(moved.get_func_expr()->port_name) == "data",
          "moved port lost its expression tree when the source library was destroyed");
}

void checkClockToQ()
{
  DataManager::initInst();
  DelayCalculator::initInst();
  Database& database = STADM.getDatabase();
  STADM.getConfig().thread_number = 1;
  Instance& instance = database.network().instances()["buffer"];
  instance.set_instance_name("buffer");
  instance.set_cell_name("boundary");
  instance.set_is_sequential(true);
  instance.set_clock_pin_name("buffer:clock");
  instance.set_output_pin_name("buffer:output");
  LibertyCell& cell = database.liberty().get_cell_map()["boundary"];
  cell.set_is_sequential(true);
  cell.get_port_map()["clock"].set_is_input(true);
  cell.get_port_map()["output"].set_is_output(true);
  cell.get_port_map()["output"].set_function_expression(ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).readLogicExpression("state"));
  addPin("buffer:clock", PinDirection::kInput, false);
  addPin("buffer:output", PinDirection::kOutput, false);
  addCellArc("clock", "output", "", 0.2);
  cell.get_cell_arc_list().back().set_is_clock_arc(true);

  GraphBuilder(STADM.getDatabase()).build();
  STADC.initialize(STADM.getDatabase().graph());
  require(database.graph().arcs().size() == 1, "clock-to-Q arc missing");
  require(!database.graph().arcs().front().get_is_disable_arc(), "sequential state function disabled clock-to-Q");
  STADC.clear();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

int main(int argc, char** argv)
{
  Logger::initInst();
  require(argc == 2, "missing Liberty fixture");
  checkLiberty(argv[1]);
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  for (int32_t mode : {-1, 0, 1}) {
    checkMode(mode, false);
  }
  checkMode(0, true);
  checkDriverLoad();
  checkClockToQ();
  checkMode(1, false, true);
  checkMode(0, false, false, true);
  checkMode(1, false, false, true);
  SdcCommand::destroyInst();
  STAInterface::destroyInst();
  Logger::destroyInst();
}
