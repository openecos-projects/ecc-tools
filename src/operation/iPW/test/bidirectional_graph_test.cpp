// SPDX-License-Identifier: MulanPSL-2.0
#include "ClockPropagator.hpp"
#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "SdcCommand.hpp"
#include "PWInterface.hpp"
#include "SignalPropagator.hpp"
#include "PowerPropagator.hpp"
#include "PowerAnalyzer.hpp"

using namespace ipw;

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void command(const std::string& text)
{
  require(
      Tcl_EvalEx(SdcCommand::getInst().getInterp(), text.c_str(), -1, TCL_EVAL_GLOBAL) == TCL_OK,
      "SDC failed: " + text
  );
}

void addPin(const std::string& name, PinDirection direction, bool is_port)
{
  Database& database = PWDM.getDatabase();
  Pin& pin = database.get_pin_map()[name];
  pin.set_direction(direction);
  pin.set_is_port(is_port);
  pin.set_pin_name(is_port ? name : name.substr(name.find(':') + 1));

  if (!is_port) {
    pin.set_instance_name("buffer");
    database.get_instance_map()["buffer"].get_pin_name_list().push_back(name);
  }
}

void addNet(const std::string& name, const std::vector<std::string>& pins)
{
  Database& database = PWDM.getDatabase();
  Net& net = database.get_net_map()[name];
  net.get_pin_name_list() = pins;
  for (const std::string& pin : pins) {
    database.get_pin_map()[pin].set_net_name(name);
  }
}

void addCellArc(const std::string& from, const std::string& to, const std::string& condition, double value)
{
  Database& database = PWDM.getDatabase();
  TimingCellArc cell_arc;
  cell_arc.set_source_port(from);
  cell_arc.set_sink_port(to);
  TimingArc timing_arc;
  timing_arc.set_sense(TimingArcSense::kPositive);
  timing_arc.set_when_expression(PWI.wrapLogicExpression(condition));
  TimingTable table;
  table.set_value_list({value});
  for (TransType trans : {TransType::kRise, TransType::kFall}) {
    timing_arc.get_delay_table_map()[trans] = table;
    timing_arc.get_slew_table_map()[trans] = table;
  }
  cell_arc.get_timing_arc_list().push_back(timing_arc);
  // Same cell, same endpoints, but a disabled variant must not win lookup.
  timing_arc.set_when_expression(PWI.wrapLogicExpression("!receive"));
  table.set_value_list({99.0});
  for (TransType trans : {TransType::kRise, TransType::kFall}) {
    timing_arc.get_delay_table_map()[trans] = table;
    timing_arc.get_slew_table_map()[trans] = table;
  }
  cell_arc.get_timing_arc_list().push_back(timing_arc);
  database.get_timing_library().get_cell_map()["boundary"].get_cell_arc_list().push_back(cell_arc);
}

void checkMode(int32_t output_enable, bool clock_mode, bool constant_data = false, bool with_vcd = false)
{
  DataManager::initInst();
  DelayCalculator::initInst();
  Database& database = PWDM.getDatabase();
  PWDM.getConfig().thread_number = 1;
  database.set_design_name("bidir_" + std::to_string(output_enable) + (clock_mode ? "_clock" : "_data"));
  Instance& instance = database.get_instance_map()["buffer"];
  instance.set_instance_name("buffer");
  instance.set_cell_name("boundary");
  TimingCell& cell = database.get_timing_library().get_cell_map()["boundary"];
  cell.set_nom_voltage(1.0);
  for (const std::string name : {"contact", "send", "receive", "data", "sense"}) {
    TimingCellPort& port = cell.get_port_map()[name];
    port.set_is_input(name != "sense");
    port.set_is_output(name == "sense" || name == "contact");
    if (name == "contact") {
      port.set_function_expression(PWI.wrapLogicExpression("data"));
      port.set_three_state_expression(PWI.wrapLogicExpression("!send"));
      port.set_capacitance(0.03);
    } else if (name == "sense") {
      port.set_function_expression(PWI.wrapLogicExpression("contact & receive"));
    }
    addPin(
        "buffer:" + name,
        name == "contact" ? PinDirection::kInout
        : name == "sense" ? PinDirection::kOutput
                          : PinDirection::kInput,
        false
    );
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
  database.get_pin_map().at("buffer:contact").set_direction(PinDirection::kInput);
  command("if {[lsearch -exact [get_fanout -from [get_ports core_tx] -flat] external] < 0} {error {inout fanout lost}}"
  );
  command("if {[lsearch -exact [get_fanin -to [get_ports core_rx] -flat] external] < 0} {error {inout fanin lost}}");
  command("create_clock -name virtual_clock -period 10");
  command("set_input_delay -clock virtual_clock 1 [get_ports {external core_tx}]");
  command("set_output_delay -clock virtual_clock 2 [get_ports {external core_rx}]");
  command("set_input_transition 0.25 [get_ports {external core_tx}]");
  command("set_load 0.07 [get_ports external]");
  command("if {[get_name [get_cells buffer]] ne {buffer}} {error {get_name cell mismatch}}");
  command("if {[get_name [get_pins buffer/data]] ne {data}} {error {get_name pin mismatch}}");
  command("if {[get_object_name [get_pins buffer/data]] ne {buffer/data}} {error {get_object_name pin mismatch}}");
  command("if {[get_object_name {}] ne {}} {error {empty collection expanded}}");
  command("if {![catch {get_name {}}]} {error {get_name accepted empty collection}}");
  command("if {![catch {get_name {external core_tx}}]} {error {get_name accepted multiple objects}}");
  command("if {[get_fanin -to [get_ports core_rx] -flat -only_cells] ne {buffer}} {error {fanin query failed}}");
  command("if {[get_fanout -from [get_ports core_tx] -flat -only_cells] ne {buffer}} {error {fanout query failed}}");
  command("set_case_analysis 1 [get_ports receive_enable]");
  if (constant_data) {
    command("set_case_analysis 0 [get_ports core_tx]");
  }
  if (output_enable >= 0) {
    command("set_case_analysis " + std::to_string(output_enable) + " [get_ports enable]");
  }
  if (clock_mode) {
    command("create_clock -name package_clock -period 12 [get_ports external]");
    command("set_propagated_clock [get_clocks package_clock]");
    command("create_generated_clock -name forwarded -source [get_ports external] -divide_by 1 [get_pins buffer/sense]");
    command("set_propagated_clock [get_clocks forwarded]");
  }
  PWI.wrapPowerIndex();
  GraphBuilder::initInst();
  PWGB.build();
  GraphBuilder::destroyInst();
  require(database.get_pin_map().size() == 10, "graph duplicated physical pins");
  require(database.get_graph_vertex_list().size() == 12, "INOUT must have two independent vertices");
  require(database.get_graph_order_list().size() == 12, "graph cannot be levelized");
  Pin& pad = database.get_pin_map().at("buffer:contact");
  require(pad.get_load_vertex_idx() != pad.get_driver_vertex_idx(), "INOUT states share vertex");
  require(pad.get_is_driver_disabled() == (output_enable == 0), "three_state polarity is wrong");
  bool has_input = false;
  bool has_output = false;
  for (Arc& arc : database.get_arc_list()) {
    require(!arc.get_is_loop_disable(), "bidirectional pin introduced a false loop");
    require(arc.get_source_pin() != arc.get_sink_pin(), "bidirectional readback wire edge was inserted");
    if (arc.get_source_pin() == "external" && arc.get_sink_pin() == "buffer:contact") {
      has_input = !arc.get_is_disable_arc();
      require(arc.get_sink_vertex_idx() == pad.get_load_vertex_idx(), "input wire misses receiver");
    }
    if (arc.get_source_pin() == "buffer:contact" && arc.get_sink_pin() == "external") {
      has_output = !arc.get_is_disable_arc();
      require(arc.get_source_vertex_idx() == pad.get_driver_vertex_idx(), "output wire misses driver");
    }
  }
  require(has_input, "input path was lost");
  require(
      has_output == (output_enable != 0 && !constant_data),
      "output-mode path was lost or disabled output propagated"
  );
  ClockPropagator::initInst();
  PWCP.propagate();
  ClockPropagator::destroyInst();
  if (clock_mode) {
    require(
        database.get_timing_point_map().at("buffer:sense").get_is_clock_point(),
        "top clock did not reach core through PAD"
    );

  } else {
    SignalPropagator::initInst();
    PWSP.propagate();
    SignalPropagator::destroyInst();
    if (with_vcd) {
      PowerActivity activity;
      activity.set_transition_density(0.2);
      activity.set_static_probability(0.3);
      activity.set_origin(PowerActivityOrigin::kVcd);
      activity.set_is_valid(true);
      database.setVcdActivity(static_cast<uint32_t>(database.get_pin_map().at("external").get_index()), activity);
    }
    PowerPropagator::initInst();
    PWPP.propagate();
    PowerPropagator::destroyInst();
    PowerAnalyzer::initInst();
    PWPA.analyze();
    PowerAnalyzer::destroyInst();
    require(
        database.get_timing_point_map().at("buffer:sense").get_data_slew_map()[AnalysisType::kMax][TransType::kRise]
            < 1.0,
        "disabled conditional table affected power slew"
    );
    uint32_t pad_idx = static_cast<uint32_t>(pad.get_index());
    require(
        database.get_workspace().driver_activity_map.contains(pad_idx) == (output_enable != 0),
        "disabled output acquired external activity"
    );
    uint32_t external_idx = static_cast<uint32_t>(database.get_pin_map().at("external").get_index());
    PowerActivity& external_activity = database.get_workspace().driver_activity_map.at(external_idx);
    require(external_activity.get_transition_density() > 0.0, "internal constant clobbered the external driver");
    if (constant_data && output_enable != 0) {
      require(
          database.get_workspace().driver_activity_map.at(pad_idx).get_transition_density() == 0.0,
          "external activity was attributed to a constant internal driver"
      );
    }
    if (with_vcd) {
      require(
          external_activity.get_origin() == PowerActivityOrigin::kVcd
              && std::abs(external_activity.get_transition_density() - 0.2) < 1E-12,
          "VCD annotation was lost at the external driver"
      );
    }
    require(database.get_instance_power_list().size() == 1, "split graph duplicated instance power");
  }
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void checkDriverLoad()
{
  DataManager::initInst();
  DelayCalculator::initInst();
  Database& database = PWDM.getDatabase();
  PWDC.init();
  Instance& instance = database.get_instance_map()["buffer"];
  instance.set_instance_name("buffer");
  instance.set_cell_name("boundary");
  TimingCellPort& contact = database.get_timing_library().get_cell_map()["boundary"].get_port_map()["contact"];
  contact.set_is_input(true);
  contact.set_is_output(true);
  contact.set_capacitance(0.03);
  addPin("external", PinDirection::kInout, true);
  addPin("buffer:contact", PinDirection::kInout, false);
  addNet("pad_net", {"external", "buffer:contact"});
  Net& net = database.get_net_map().at("pad_net");
  net.get_driver_pin_list() = {"external", "buffer:contact"};
  net.get_load_pin_list() = {"external", "buffer:contact"};
  command("set_load 0.07 [get_ports external]");
  ParasiticNet& parasitic = database.get_parasitic_net_map()["pad_net"];
  parasitic.set_net_name("pad_net");
  parasitic.get_node_capacitance_map()["external"] = 0.01;
  parasitic.get_node_capacitance_map()["buffer:contact"] = 0.02;
  ParasiticResistor resistor;
  resistor.set_source_node("external");
  resistor.set_sink_node("buffer:contact");
  resistor.set_resistance(100.0);
  parasitic.get_resistor_list().push_back(resistor);
  GraphBuilder::initInst();
  PWGB.build();
  GraphBuilder::destroyInst();
  for (bool reverse : {false, true, false}) {
    std::string driver = reverse ? "buffer:contact" : "external";
    double load = PWDC.getPowerOutputLoad(driver, AnalysisType::kMax, TransType::kRise);
    double expected = reverse ? 0.10 : 0.06;
    require(std::abs(load - expected) < 1E-9, "RC load includes driver capacitance or reused another driver's root");
  }
  PWDC.destroy();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void checkClockToQ()
{
  DataManager::initInst();
  DelayCalculator::initInst();
  Database& database = PWDM.getDatabase();
  PWDM.getConfig().thread_number = 1;
  Instance& instance = database.get_instance_map()["buffer"];
  instance.set_instance_name("buffer");
  instance.set_cell_name("boundary");
  instance.set_is_sequential(true);
  instance.set_clock_pin_name("buffer:clock");
  instance.set_output_pin_name("buffer:output");
  TimingCell& cell = database.get_timing_library().get_cell_map()["boundary"];
  cell.set_is_sequential(true);
  cell.get_port_map()["clock"].set_is_input(true);
  cell.get_port_map()["output"].set_is_output(true);
  cell.get_port_map()["output"].set_function_expression(PWI.wrapLogicExpression("state"));
  addPin("buffer:clock", PinDirection::kInput, false);
  addPin("buffer:output", PinDirection::kOutput, false);
  addCellArc("clock", "output", "", 0.2);
  cell.get_cell_arc_list().back().set_is_clock_arc(true);
  PWI.wrapPowerIndex();
  GraphBuilder::initInst();
  PWGB.build();
  GraphBuilder::destroyInst();
  require(database.get_arc_list().size() == 1, "clock-to-Q arc missing");
  require(!database.get_arc_list().front().get_is_disable_arc(), "sequential state function disabled clock-to-Q");
  PWDC.destroy();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void checkLoadObjectIdentity()
{
  DataManager::initInst();
  Database& database = PWDM.getDatabase();
  addPin("package", PinDirection::kInout, true);
  addNet("package", {"package"});
  addNet("inner/package", {});
  command("set port [get_ports package]; set net [get_nets -filter {full_name == package}]; set_load 0.7 $port");
  TimingConstraint& constraint = database.get_timing_constraint();
  require(
      !constraint.has_net_load("package", AnalysisType::kMax, TransType::kRise)
          && !constraint.has_net_load("inner/package", AnalysisType::kMax, TransType::kRise),
      "port load also applied to same-name nets"
  );
  command("foreach item $net {set_load 0.2 $item}");
  require(
      constraint.get_net_load("package", AnalysisType::kMax, TransType::kRise) == 0.2,
      "explicit net collection lost its type"
  );
  require(
      constraint.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.7,
      "net load changed a same-name port"
  );
  command("set_load 0.3 [get_nets inner/package]");
  require(
      constraint.get_net_load("inner/package", AnalysisType::kMax, TransType::kRise) == 0.3,
      "hierarchical net load missing"
  );
  command("set_load 0.8 [lindex $port 0]");
  require(
      constraint.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.8,
      "scalar collection element lost its type"
  );
  require(
      constraint.get_net_load("package", AnalysisType::kMax, TransType::kRise) == 0.2,
      "port element overwrote net load"
  );
  command("set_load 0.4 [concat $port $net]");
  require(
      constraint.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.4
          && constraint.get_net_load("package", AnalysisType::kMax, TransType::kRise) == 0.4,
      "mixed port/net collection was not preserved"
  );
  DataManager::destroyInst();
}

int main()
{
  Logger::initInst();
  SdcCommand::initInst();
  for (int32_t mode : {-1, 0, 1}) {
    checkMode(mode, false);
  }
  checkMode(0, true);
  checkMode(1, false, true);
  checkMode(0, false, false, true);
  checkMode(1, false, true, true);
  checkDriverLoad();
  checkClockToQ();
  checkLoadObjectIdentity();
  SdcCommand::destroyInst();
  PWInterface::destroyInst();
  Logger::destroyInst();
}
