// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"

using namespace ista;

static void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

static void design(Database& database, double resistance)
{
  auto& instance = database.network().instances()["buffer"];
  instance.set_instance_name("buffer");
  instance.set_cell_name("boundary");
  auto& contact = database.liberty().get_cell_map()["boundary"].get_port_map()["contact"];
  contact.set_is_input(true);
  contact.set_is_output(true);
  contact.set_capacitance(0.03);
  for (const std::string name : {"external", "buffer:contact"}) {
    auto& pin = database.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name == "external" ? name : "contact");
    pin.set_instance_name(name == "external" ? "" : "buffer");
    pin.set_is_port(name == "external");
    pin.set_direction(PinDirection::kInout);
    pin.set_net_name("pad_net");
  }
  auto& net = database.network().nets()["pad_net"];
  net.set_net_name("pad_net");
  net.get_pin_name_list() = {"external", "buffer:contact"};
  net.get_driver_pin_list() = net.get_load_pin_list() = net.get_pin_name_list();
  auto& load = database.sdc().get_port_constraint_map()["external"];
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall})
      load.set_load(mode, edge, 0.07, false);
  auto& parasitic = database.parasitics().nets()["pad_net"];
  parasitic.set_net_name("pad_net");
  parasitic.get_node_map()["external"].set_capacitance(0.01);
  parasitic.get_node_map()["buffer:contact"].set_capacitance(0.02);
  ParasiticResistor resistor;
  resistor.set_source_node("external");
  resistor.set_sink_node("buffer:contact");
  resistor.set_resistance(resistance);
  parasitic.get_resistor_list().push_back(resistor);
}

static double delay(DelayCalculator& calculator, bool reverse)
{
  Arc arc;
  arc.set_type(ArcType::kNet);
  arc.set_owner_name("pad_net");
  arc.set_source_pin(reverse ? "buffer:contact" : "external");
  arc.set_sink_pin(reverse ? "external" : "buffer:contact");
  auto result = calculator.arcDelay(arc, AnalysisType::kMax, TransType::kRise, TransType::kRise, std::numeric_limits<double>::quiet_NaN());
  require(result.has_value(), "valid wire result missing");
  return result->get_delay();
}

static void checkClockToQ()
{
  Database database;
  Config config;
  auto& instance = database.network().instances()["ff"];
  instance.set_cell_name("flop");
  auto& pin = database.network().pins()["ff:Q"];
  pin.set_full_name("ff:Q");
  pin.set_instance_name("ff");
  pin.set_pin_name("Q");
  auto& cell = database.liberty().get_cell_map()["flop"];
  cell.get_port_map()["Q"].set_is_output(true);
  TimingCellArc cell_arc;
  cell_arc.set_source_port("CK");
  cell_arc.set_sink_port("Q");
  cell_arc.set_is_clock_arc(true);
  TimingArc table_arc;
  table_arc.set_trigger_trans_type(TransType::kRise);
  table_arc.set_sense(TimingArcSense::kPositive);
  TimingTable delay_table;
  delay_table.set_axis_list({{0.0, 1.0}});
  delay_table.set_value_list({2.0, 4.0});
  TimingTable slew_table;
  slew_table.set_axis_list({{0.0, 1.0}});
  slew_table.set_value_list({0.1, 0.9});
  table_arc.get_delay_table_map()[TransType::kRise] = delay_table;
  table_arc.get_slew_table_map()[TransType::kRise] = slew_table;
  cell_arc.get_timing_arc_list().push_back(table_arc);
  cell.get_cell_arc_list().push_back(cell_arc);
  Arc arc;
  arc.set_type(ArcType::kCell);
  arc.set_owner_name("ff");
  arc.set_source_pin("ff:CK");
  arc.set_sink_pin("ff:Q");
  arc.set_library_source_port("CK");
  arc.set_library_sink_port("Q");
  DelayCalculator calculator(database, config);
  require(!calculator.arcDelay(arc, AnalysisType::kMax, TransType::kFall, TransType::kRise, 0.5), "inactive clock edge produced a result");
  auto explicit_zero = calculator.arcDelay(arc, AnalysisType::kMax, TransType::kRise, TransType::kRise, 0.5, 0.0);
  auto inherited = calculator.arcDelay(arc, AnalysisType::kMax, TransType::kRise, TransType::kRise, 0.5);
  require(explicit_zero && inherited, "valid edge failed after an inactive edge");
  require(explicit_zero->get_delay() == 3.0 && inherited->get_delay() == 3.0, "clock slew override changed delay lookup input");
  require(std::abs(explicit_zero->get_slew() - 0.1) < 1E-12 && std::abs(inherited->get_slew() - 0.5) < 1E-12,
          "explicit zero clock slew was confused with an absent override");
}

static void checkExternalDrivingLoad()
{
  for (auto direction : {PinDirection::kInput, PinDirection::kInout}) {
    Database database;
    Config config;
    design(database, 100.0);
    database.parasitics().nets().clear();
    database.network().pins().at("external").set_direction(direction);
    if (direction == PinDirection::kInput)
      database.network().nets().at("pad_net").get_load_pin_list() = {"buffer:contact"};
    TimingCellArc driver;
    TimingArc timing;
    timing.set_sense(TimingArcSense::kPositive);
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      TimingTable delay, slew;
      delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
      slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
      delay.set_axis_list({{0.0, 1.0}});
      slew.set_axis_list({{0.0, 1.0}});
      const double slope = edge == TransType::kRise ? 2.0 : 3.0;
      delay.set_value_list({0.1, 0.1 + slope});
      slew.set_value_list({0.2, 0.2 + 2.0 * slope});
      timing.get_delay_table_map()[edge] = delay;
      timing.get_slew_table_map()[edge] = slew;
    }
    driver.get_timing_arc_list().push_back(timing);
    DelayCalculator calculator(database, config);
    std::string port = "external";
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        const double port_load = (mode == AnalysisType::kMin ? 0.04 : 0.08) + (edge == TransType::kRise ? 0.0 : 0.01);
        database.sdc().get_port_constraint_map().at(port).set_load(mode, edge, port_load, false);
        calculator.clear();
        DelayResult result;
        require(calculator.calculateDrivingCell(port, driver, mode, edge, 0.1, 0.1, result), "external driver produced no result");
        const double load = port_load + 0.03;
        const double slope = edge == TransType::kRise ? 2.0 : 3.0;
        require(std::abs(result.get_delay() - slope * load) < 1e-12, "driving-cell delay omitted the input port's set_load");
        require(std::abs(result.get_slew() - (0.2 + 2.0 * slope * load)) < 1e-12, "driving-cell slew omitted the input port's set_load");
      }
    }
  }
}

static void checkDrivingCellSelection()
{
  Database database;
  Config config;
  design(database, .001);
  database.network().nets().at("pad_net").get_driver_pin_list() = {"external"};
  database.network().nets().at("pad_net").get_load_pin_list() = {"buffer:contact"};
  database.network().pins().at("external").set_direction(PinDirection::kInput);
  database.liberty().get_cell_map().at("boundary").get_port_map().at("contact").set_capacitance(0.);
  auto& net = database.parasitics().nets().at("pad_net");
  net.get_node_map().at("external").set_capacitance(.01);
  net.get_node_map().at("buffer:contact").set_capacitance(.003547);
  database.sdc().get_port_constraint_map().clear();
  TimingCellArc driver;
  // Two conditional PAD strengths. Numerical fit residuals must not select
  // the slow, weak driver for min analysis merely because its fitted delay
  // correction is negative. The extracted PAD probe fits the strong min table.
  for (bool weak : {true, false}) {
    TimingArc timing;
    timing.set_sense(TimingArcSense::kPositive);
    timing.set_slew_lower_threshold_pct_rise(.1);
    timing.set_slew_upper_threshold_pct_rise(.9);
    timing.set_slew_lower_threshold_pct_fall(.1);
    timing.set_slew_upper_threshold_pct_fall(.9);
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      auto& delay = timing.get_delay_table_map()[edge];
      delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
      delay.set_axis_list({{0., 1.}});
      delay.set_value_list(weak ? std::vector<double>{1.629336881, 1.902036481} : std::vector<double>{.998172595, 1.030598978});
      auto& slew = timing.get_slew_table_map()[edge];
      slew.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
      slew.set_axis_list({{0., 1.}});
      slew.set_value_list(weak ? std::vector<double>{1.76141945, 2.35381945} : std::vector<double>{.303250057, .372144657});
    }
    driver.get_timing_arc_list().push_back(timing);
  }
  DelayCalculator calculator(database, config);
  std::string port = "external";
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    DelayResult early, late;
    require(calculator.calculateDrivingCell(port, driver, AnalysisType::kMin, edge, 0., 0., early)
                && calculator.calculateDrivingCell(port, driver, AnalysisType::kMax, edge, 0., 0., late), "conditional driving cell has no result");
    require(early.get_slew() < .4 && late.get_slew() > 1.7, "RC fit residual changed conditional driving-cell table selection");
  }
  for (bool slew_only : {false, true}) {
    TimingCellArc partial;
    partial.get_timing_arc_list().push_back(driver.get_timing_arc_list().back());
    auto& arc = partial.get_timing_arc_list().front();
    if (slew_only)
      arc.get_delay_table_map().clear();
    else
      arc.get_slew_table_map().clear();
    calculator.clear();
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
      DelayResult result;
      require(calculator.calculateDrivingCell(port, partial, mode, TransType::kRise, .12, .2, result), "one-table driving cell rejected");
      require(slew_only ? result.get_delay() == 0. : result.get_slew() == .12,
              "driver fitting synthesized an absent delay or transition table");
    }
  }

}

static void checkConditionalCombinationalArc()
{
  Database db;
  Config config;
  db.network().instances()["mux"].set_cell_name("MUX");
  for (const auto* name : {"S0", "B", "Y"}) {
    auto& pin = db.network().pins()[std::string("mux:") + name];
    pin.set_instance_name("mux");
    pin.set_pin_name(name);
    pin.set_full_name(std::string("mux:") + name);
  }
  TimingCellArc cell_arc;
  cell_arc.set_source_port("S0");
  cell_arc.set_sink_port("Y");
  for (bool positive : {true, false}) {
    TimingArc timing;
    timing.set_sense(positive ? TimingArcSense::kPositive : TimingArcSense::kNegative);
    LogicExpression condition;
    LogicExpressionTerm port;
    port.set_operation_type(LogicOperationType::kPort);
    port.set_port_name("B");
    condition.get_term_list().push_back(port);
    if (!positive) {
      LogicExpressionTerm negate;
      negate.set_operation_type(LogicOperationType::kNot);
      condition.get_term_list().push_back(negate);
    }
    timing.set_when_expression(condition);
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      timing.get_delay_table_map()[edge].set_value_list({positive ? .03 : .1});
      timing.get_slew_table_map()[edge].set_value_list({.04});
    }
    cell_arc.get_timing_arc_list().push_back(timing);
  }
  db.liberty().get_cell_map()["MUX"].get_cell_arc_list().push_back(cell_arc);
  db.graph().simValues().setValue("mux:B", TimingCaseValue::kZero);
  Arc arc;
  arc.set_type(ArcType::kCell);
  arc.set_owner_name("mux");
  arc.set_source_pin("mux:S0");
  arc.set_sink_pin("mux:Y");
  arc.set_library_source_port("S0");
  arc.set_library_sink_port("Y");
  DelayCalculator calculator(db, config);
  calculator.initialize(arc);
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
    for (auto input : {TransType::kRise, TransType::kFall}) {
      const auto output = input == TransType::kRise ? TransType::kFall : TransType::kRise;
      require(!arc.get_input_output_delay_map().at(mode).at(input).contains(input), "false combinational condition created a timing lane");
      require(!calculator.arcDelay(arc, mode, input, input, .2), "disabled combinational lane returned a fallback delay");
      const auto valid = calculator.arcDelay(arc, mode, input, output, .2);
      require(valid && std::abs(valid->get_delay() - .1) < 1e-12, "active negative-unate condition was lost");
      auto& lib_arc = db.liberty().get_cell_map()["MUX"].get_cell_arc_list().front();
      std::string pin = "mux:Y";
      require(!calculator.cellDelay(pin, lib_arc, mode, input, input, .2), "direct cell query resurrected a false condition");
    }
  }
  db.graph().simValues().clear();
  calculator.initialize(arc);
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto input : {TransType::kRise, TransType::kFall}) {
      const auto opposite = input == TransType::kRise ? TransType::kFall : TransType::kRise;
      require(calculator.arcDelay(arc, mode, input, input, .2).has_value()
                  && calculator.arcDelay(arc, mode, input, opposite, .2).has_value(),
              "unknown condition disabled a possible combinational timing lane");
    }
}

int main()
{
  try {
    checkConditionalCombinationalArc();
    checkDrivingCellSelection();
    checkExternalDrivingLoad();
    checkClockToQ();
    Database first, second;
    Config config;
    design(first, 100.0);
    design(second, 200.0);
    DelayCalculator a(first, config), b(second, config);
    for (bool reverse : {false, true, false}) {
      double expected = reverse ? 0.008 : 0.005 * std::log(2.0);
      require(std::abs(delay(a, reverse) - expected) < 1E-9, "driver-root cache reused another source");
      require(std::abs(delay(b, reverse) - 2 * expected) < 1E-9, "calculator read another database or cache");
    }
    first.parasitics().nets().at("pad_net").get_resistor_list().front().set_resistance(300.0);
    a.clear();
    require(std::abs(delay(a, false) - 0.015 * std::log(2.0)) < 1E-9, "clear did not discard derived models");
    require(std::abs(delay(b, false) - 0.010 * std::log(2.0)) < 1E-9, "clear affected another calculator");
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
