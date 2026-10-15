// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "TimingCaseAnalysis.hpp"

int main()
{
  using namespace ista;
  DataManager::initInst();
  DelayCalculator::initInst();
  auto& database = STADM.getDatabase();
  auto& instance = database.get_instance_map()["pad"];
  instance.set_instance_name("pad");
  instance.set_cell_name("PAD");
  instance.set_pin_name_list({"pad:I", "pad:DS", "pad:Y"});
  for (const auto& name : instance.get_pin_name_list()) {
    auto& pin = database.get_pin_map()[name];
    pin.set_instance_name("pad");
    pin.set_pin_name(name.substr(4));
    pin.set_direction(name == "pad:Y" ? PinDirection::kOutput : PinDirection::kInput);
  }
  LogicExpressionTerm term;
  term.set_operation_type(LogicOperationType::kPort);
  term.set_port_name("DS");
  LogicExpression enabled;
  enabled.set_term_list({term});
  LogicExpressionTerm negate;
  negate.set_operation_type(LogicOperationType::kNot);
  LogicExpression disabled;
  disabled.set_term_list({term, negate});
  TimingTable table;
  TimingCellArc cell_arc;
  for (int index = 0; index < 2; ++index) {
    TimingArc variant;
    variant.set_arc_idx(index);
    variant.set_sense(TimingArcSense::kPositive);
    variant.set_when(index == 0 ? disabled : enabled);
    table.set_value_list({index == 0 ? 1.0 : 9.0});
    variant.get_delay_table_map()[TransType::kRise] = table;
    table.set_value_list({index == 0 ? 2.0 : 10.0});
    variant.get_slew_table_map()[TransType::kRise] = table;
    cell_arc.get_timing_arc_list().push_back(variant);
  }
  Arc arc;
  arc.set_type(ArcType::kCell);
  arc.set_owner_name("pad");
  arc.set_source_pin("pad:I");
  arc.set_sink_pin("pad:Y");
  arc.set_timing_cell_arc(&cell_arc);
  database.get_arc_list().push_back(arc);
  auto& constraint = database.get_timing_constraint();
  auto& graph_arc = database.get_arc_list().front();
  for (bool select : {false, true, false}) {
    constraint.get_case_analysis_map()["pad:DS"] = select ? TimingCaseValue::kOne : TimingCaseValue::kZero;
    TimingCaseAnalysis::apply(database);
    DCTask task;
    task.set_proc_type(DCProcType::kCalculate);
    task.set_arc(&graph_arc);
    task.set_analysis_type(AnalysisType::kMax);
    task.set_input_trans_type(TransType::kRise);
    task.set_output_trans_type(TransType::kRise);
    STADC.calculate(task);
    if (!task.get_is_valid() || std::abs(task.get_timing_result().get_delay() - (select ? 9.0 : 1.0)) > 1e-9
        || std::abs(task.get_timing_result().get_slew() - (select ? 10.0 : 2.0)) > 1e-9
        || graph_arc.get_disabled_timing_arcs() != std::set<int32_t>{select ? 0 : 1}) {
      throw std::runtime_error("conditional delay/slew arc did not follow instance case analysis");
    }
  }
  constraint.get_case_analysis_map().clear();
  TimingCaseAnalysis::apply(database);
  if (!graph_arc.get_disabled_timing_arcs().empty())
    throw std::runtime_error("stale conditional arc mask");
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}
