// SPDX-License-Identifier: MulanPSL-2.0
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Database.hpp"
#include "LogicSimulator.hpp"

namespace {
using namespace ista;
void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

LogicExpression expression(std::initializer_list<std::string> tokens)
{
  LogicExpression result;
  for (const auto& token : tokens) {
    LogicExpressionTerm term;
    term.set_operation_type(token == "!" ? LogicOperationType::kNot : token == "&" ? LogicOperationType::kAnd : LogicOperationType::kPort);
    if (token != "!" && token != "&")
      term.set_port_name(token);
    result.get_term_list().push_back(term);
  }
  return result;
}

void pin(Network& network, const std::string& name, PinDirection direction, bool port = false)
{
  auto& p = network.pins()[name];
  p.set_full_name(name);
  p.set_direction(direction);
  p.set_is_port(port);
  auto colon = name.find(':');
  p.set_pin_name(colon == std::string::npos ? name : name.substr(colon + 1));
  if (colon != std::string::npos) {
    const auto instance = name.substr(0, colon);
    p.set_instance_name(instance);
    network.instances()[instance].set_instance_name(instance);
    network.instances()[instance].set_cell_name("cell");
    network.instances()[instance].get_pin_name_list().push_back(name);
  }
}

void net(Network& network, const std::string& name, std::vector<std::string> drivers, std::vector<std::string> loads)
{
  auto& n = network.nets()[name];
  n.set_driver_pin_list(drivers);
  n.set_load_pin_list(loads);
}

void propagate(Database& db)
{
  // const domains guarantee simulation cannot rewrite user input or connectivity.
  const auto& input = std::as_const(db);
  LogicSimulator(input.network(), input.liberty(), input.sdc(), db.graph()).propagate();
}

void caseValuesAndRecomputation()
{
  Database db;
  pin(db.network(), "in", PinDirection::kInput, true);
  pin(db.network(), "out", PinDirection::kOutput, true);
  net(db.network(), "n", {"in"}, {"out"});
  db.network().pins().at("in").set_constant_value(TimingCaseValue::kZero);
  db.sdc().get_case_analysis_map()["in"] = TimingCaseValue::kOne;
  propagate(db);
  require(db.graph().simValues().value("out") == TimingCaseValue::kOne, "user case did not override built-in constant");
  require(db.sdc().get_case_analysis_map().size() == 1, "propagated result polluted user constraints");
  require(db.network().pins().at("in").get_constant_value() == TimingCaseValue::kZero, "built-in constant was overwritten");

  db.sdc().get_case_analysis_map().clear();
  propagate(db);
  require(db.graph().simValues().value("out") == TimingCaseValue::kZero, "recompute retained previous case value");
  db.network().pins().at("in").set_constant_value(TimingCaseValue::kNone);
  propagate(db);
  require(!db.graph().simValues().value("out"), "recompute retained old derived constant");

  struct Case
  {
    TimingCaseValue value;
    bool rise;
    bool fall;
  };
  for (const Case c : {Case{TimingCaseValue::kRise, true, false}, Case{TimingCaseValue::kFall, false, true}, Case{TimingCaseValue::kStatic, false, false},
                       Case{TimingCaseValue::kNone, true, true}}) {
    db.sdc().get_case_analysis_map()["in"] = c.value;
    propagate(db);
    require(db.graph().simValues().allowsTransition("out", TransType::kRise) == c.rise, "rise case qualification changed");
    require(db.graph().simValues().allowsTransition("out", TransType::kFall) == c.fall, "fall case qualification changed");
  }
  // An explicit receiver override must survive a conflicting net driver.
  db.sdc().get_case_analysis_map() = {{"in", TimingCaseValue::kZero}, {"out", TimingCaseValue::kOne}};
  propagate(db);
  require(db.graph().simValues().value("out") == TimingCaseValue::kOne, "explicit receiver case lost precedence");
}

void inoutAndMultipleDrivers()
{
  Database db;
  for (const auto& name : {"u:D", "u:EN"})
    pin(db.network(), name, PinDirection::kInput);
  pin(db.network(), "u:PAD", PinDirection::kInout);
  pin(db.network(), "external", PinDirection::kInout, true);
  pin(db.network(), "load", PinDirection::kOutput, true);
  net(db.network(), "bus", {"external", "u:PAD"}, {"u:PAD", "load"});
  auto& port = db.liberty().get_cell_map()["cell"].get_port_map()["PAD"];
  port.set_is_output(true);
  port.set_function_expression(expression({"D"}));
  port.set_three_state_expression(expression({"EN", "!"}));
  db.sdc().get_case_analysis_map() = {{"u:D", TimingCaseValue::kOne}, {"u:EN", TimingCaseValue::kOne}, {"external", TimingCaseValue::kZero}};
  propagate(db);
  const auto& values = db.graph().simValues();
  require(values.value("u:PAD") == TimingCaseValue::kZero && values.value("u:PAD", true) == TimingCaseValue::kOne,
          "inout driver and receiver values were merged");
  require(!values.value("load"), "conflicting drivers propagated the first constant");
  db.sdc().get_case_analysis_map()["u:EN"] = TimingCaseValue::kZero;
  propagate(db);
  require(values.isDriverDisabled("u:PAD") && !values.value("u:PAD", true), "three-state driver not cleared");
  require(values.value("load") == TimingCaseValue::kZero, "disabled driver still participated in net propagation");
  db.sdc().get_case_analysis_map().erase("external");
  propagate(db);
  require(!values.value("load"), "unknown active driver replaced by disabled constant driver");
  db.sdc().get_case_analysis_map()["u:EN"] = TimingCaseValue::kOne;
  propagate(db);
  require(!values.isDriverDisabled("u:PAD"), "recompute retained disabled driver");
  require(!values.value("load"), "unknown driver replaced by known constant driver");
}

void arcConditionsAndSequentialSensitivity()
{
  Database db;
  for (const auto& name : {"u:A", "u:EN", "u:CLK"})
    pin(db.network(), name, PinDirection::kInput);
  pin(db.network(), "u:Y", PinDirection::kOutput);
  pin(db.network(), "u:Q", PinDirection::kOutput);
  auto& cell = db.liberty().get_cell_map()["cell"];
  auto& y = cell.get_port_map()["Y"];
  y.set_is_output(true);
  y.set_function_expression(expression({"A"}));
  auto& q = cell.get_port_map()["Q"];
  q.set_is_output(true);
  q.set_function_expression(expression({"state"}));
  TimingCellArc conditional;
  TimingArc timing;
  timing.set_when_expression(expression({"EN"}));
  conditional.get_timing_arc_list().push_back(timing);
  cell.get_cell_arc_list().push_back(conditional);
  Arc data;
  data.set_type(ArcType::kCell);
  data.set_source_pin("u:A");
  data.set_sink_pin("u:Y");
  data.set_owner_name("u");
  data.set_library_source_port("A");
  data.set_library_sink_port("Y");
  data.setLibertyArcId(LibertyArcId{"cell", 0});
  db.graph().arcs().push_back(data);
  Arc clock;
  clock.set_type(ArcType::kCell);
  clock.set_source_pin("u:CLK");
  clock.set_sink_pin("u:Q");
  clock.set_owner_name("u");
  clock.set_library_source_port("CLK");
  clock.set_library_sink_port("Q");
  clock.set_is_clock_arc(true);
  db.graph().arcs().push_back(clock);

  db.sdc().get_case_analysis_map()["u:EN"] = TimingCaseValue::kZero;
  propagate(db);
  require(db.graph().arcs()[0].get_is_case_analysis_disable(), "false when condition left arc enabled");
  require(!db.graph().arcs()[1].get_is_case_analysis_disable(), "sequential state function disabled clock-to-Q");
  db.sdc().get_case_analysis_map()["u:EN"] = TimingCaseValue::kOne;
  propagate(db);
  require(!db.graph().arcs()[0].get_is_case_analysis_disable(), "changed condition retained disabled arc");

  Sdc other_sdc;
  other_sdc.get_case_analysis_map()["u:A"] = TimingCaseValue::kStatic;
  Graph other_graph = db.graph();
  LogicSimulator(std::as_const(db).network(), std::as_const(db).liberty(), other_sdc, other_graph).propagate();
  require(other_graph.simValues().value("u:Y") == TimingCaseValue::kStatic, "static logic result lost");
  require(other_graph.arcs()[0].get_is_case_analysis_disable(), "static logic failed to disable arc");
  require(!db.graph().simValues().value("u:Y") && !db.graph().arcs()[0].get_is_case_analysis_disable(), "independent simulation changed first graph");
}
}  // namespace

int main()
{
  caseValuesAndRecomputation();
  inoutAndMultipleDrivers();
  arcConditionsAndSequentialSensitivity();
}
