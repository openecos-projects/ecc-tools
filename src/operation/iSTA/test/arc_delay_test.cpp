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

int main()
{
  try {
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
