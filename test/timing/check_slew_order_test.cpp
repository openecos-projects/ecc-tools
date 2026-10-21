// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "DelayCalculator.hpp"

int main()
{
  using namespace ista;
  DelayCalculator::initInst();
  for (auto type : {TimingCheckType::kSetup, TimingCheckType::kHold, TimingCheckType::kRecovery, TimingCheckType::kRemoval}) {
    for (bool data_first : {false, true}) {
      TimingTable table;
      table.set_variable_type1(data_first ? TimingTableVariableType::kConstrainedTransition : TimingTableVariableType::kInputTransition);
      table.set_variable_type2(data_first ? TimingTableVariableType::kInputTransition : TimingTableVariableType::kConstrainedTransition);
      table.set_axis_list({{0, 1}, {0, 1}});
      // Both axis orders describe check(clock, data) = clock + 10 * data.
      table.set_value_list(data_first ? std::vector<double>{0, 1, 10, 11} : std::vector<double>{0, 10, 1, 11});
      TimingArc arc;
      arc.set_time_unit_scale(1);
      arc.set_cap_unit_scale(1);
      arc.set_trigger_trans_type(TransType::kRise);
      arc.set_check_trans_type(TransType::kRise);
      arc.get_check_table_map()[TransType::kRise] = table;
      TimingCheckArc check;
      check.set_check_type(type);
      check.set_clock_trans_type(TransType::kRise);
      check.set_timing_arc_list({arc});
      DCTask task;
      task.set_proc_type(DCProcType::kCalculate);
      task.set_timing_check_arc(&check);
      task.set_analysis_type(AnalysisType::kMax);
      task.set_clock_trans_type(TransType::kRise);
      task.set_data_trans_type(TransType::kRise);
      task.set_clock_slew(0.1);
      task.set_data_slew(0.2);
      STADC.calculate(task);
      if (!task.get_is_valid() || std::abs(task.get_timing_result().get_delay() - 2.1) > 1e-10) {
        throw std::runtime_error("native timing check reversed clock/data slew");
      }
    }
  }
  DelayCalculator::destroyInst();
}
