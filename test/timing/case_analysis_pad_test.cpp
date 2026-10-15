// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>

#include "Database.hpp"
#include "TimingCaseAnalysis.hpp"

namespace {
void require(bool value)
{
  if (!value)
    throw std::runtime_error("pad case analysis test failed");
}
}  // namespace

int main()
{
  using namespace ista;
  Database database;
  auto& instance = database.get_instance_map()["pad"];
  instance.set_instance_name("pad");
  instance.set_cell_name("PAD");
  instance.set_pin_name_list({"pad:I", "pad:IE", "pad:PAD", "pad:C"});
  for (const auto& name : instance.get_pin_name_list()) {
    auto& pin = database.get_pin_map()[name];
    pin.set_instance_name("pad");
    pin.set_pin_name(name.substr(4));
    pin.set_direction(name == "pad:C" ? PinDirection::kOutput : PinDirection::kInput);
  }
  auto portTerm = [](std::string name) {
    LogicExpressionTerm term;
    term.set_operation_type(LogicOperationType::kPort);
    term.set_port_name(name);
    return term;
  };
  auto& ports = database.get_timing_library().get_cell_map()["PAD"].get_port_map();
  ports["PAD"].set_is_input(true);
  ports["PAD"].set_is_output(true);
  LogicExpression output_function;
  output_function.set_term_list({portTerm("I")});
  ports["PAD"].set_function_expression(output_function);
  ports["C"].set_is_output(true);
  LogicExpressionTerm and_term;
  and_term.set_operation_type(LogicOperationType::kAnd);
  LogicExpression input_function;
  input_function.set_term_list({portTerm("PAD"), portTerm("IE"), and_term});
  ports["C"].set_function_expression(input_function);
  auto& constraint = database.get_timing_constraint();
  constraint.get_case_analysis_map()["pad:I"] = TimingCaseValue::kZero;
  constraint.get_case_analysis_map()["pad:IE"] = TimingCaseValue::kOne;
  Arc input_arc;
  input_arc.set_type(ArcType::kCell);
  input_arc.set_owner_name("pad");
  input_arc.set_source_pin("pad:PAD");
  input_arc.set_sink_pin("pad:C");
  input_arc.set_library_source_port("PAD");
  database.get_arc_list().push_back(input_arc);
  auto& flipflop = database.get_instance_map()["ff"];
  flipflop.set_instance_name("ff");
  flipflop.set_cell_name("FF");
  flipflop.set_is_sequential(true);
  flipflop.set_pin_name_list({"ff:CK", "ff:Q"});
  database.get_pin_map()["ff:CK"].set_instance_name("ff");
  database.get_pin_map()["ff:CK"].set_direction(PinDirection::kInput);
  database.get_pin_map()["ff:Q"].set_instance_name("ff");
  database.get_pin_map()["ff:Q"].set_direction(PinDirection::kOutput);
  auto& q = database.get_timing_library().get_cell_map()["FF"].get_port_map()["Q"];
  q.set_is_output(true);
  LogicExpression state_function;
  state_function.set_term_list({portTerm("IQ")});
  q.set_function_expression(state_function);
  Arc clock_arc;
  clock_arc.set_type(ArcType::kCell);
  clock_arc.set_owner_name("ff");
  clock_arc.set_source_pin("ff:CK");
  clock_arc.set_sink_pin("ff:Q");
  clock_arc.set_library_source_port("CK");
  clock_arc.set_library_sink_port("Q");
  clock_arc.set_is_clock_arc(true);
  database.get_arc_list().push_back(clock_arc);
  TimingCaseAnalysis::apply(database);
  require(!constraint.get_effective_case_analysis_map().contains("pad:PAD"));
  require(!constraint.get_effective_case_analysis_map().contains("pad:C"));
  require(!database.get_arc_list()[0].get_is_disable_arc());
  require(!database.get_arc_list()[1].get_is_disable_arc());
  // The same PAD configured as an output must still propagate its function.
  database.get_pin_map()["pad:PAD"].set_direction(PinDirection::kOutput);
  TimingCaseAnalysis::apply(database);
  require(constraint.get_effective_case_analysis_map().at("pad:PAD") == TimingCaseValue::kZero);
  require(constraint.get_effective_case_analysis_map().at("pad:C") == TimingCaseValue::kZero);
  require(database.get_arc_list()[0].get_is_disable_arc());
  // Y = (A0 & A1) | B, with A0=0: A1 contributes electrical slew
  // even though no arrival path through A1 can reach Y.
  auto& gate = database.get_instance_map()["gate"];
  gate.set_instance_name("gate");
  gate.set_cell_name("GATE");
  gate.set_pin_name_list({"gate:A0", "gate:A1", "gate:B", "gate:Y"});
  for (const auto& name : gate.get_pin_name_list()) {
    auto& pin = database.get_pin_map()[name];
    pin.set_instance_name("gate");
    pin.set_pin_name(name.substr(5));
    pin.set_direction(name == "gate:Y" ? PinDirection::kOutput : PinDirection::kInput);
  }
  LogicExpressionTerm or_term;
  or_term.set_operation_type(LogicOperationType::kOr);
  LogicExpression gate_function;
  gate_function.set_term_list({portTerm("A0"), portTerm("A1"), and_term, portTerm("B"), or_term});
  auto& output = database.get_timing_library().get_cell_map()["GATE"].get_port_map()["Y"];
  output.set_is_output(true);
  output.set_function_expression(gate_function);
  Arc insensitive;
  insensitive.set_type(ArcType::kCell);
  insensitive.set_owner_name("gate");
  insensitive.set_source_pin("gate:A1");
  insensitive.set_sink_pin("gate:Y");
  insensitive.set_library_source_port("A1");
  insensitive.set_library_sink_port("Y");
  database.get_arc_list().push_back(insensitive);
  constraint.get_case_analysis_map()["gate:A0"] = TimingCaseValue::kZero;
  TimingCaseAnalysis::apply(database);
  require(database.get_arc_list().back().get_is_disable_arc());
  require(!database.get_arc_list().back().get_is_electrical_disable_arc());
}
