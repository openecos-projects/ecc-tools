// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"

int main()
{
  using namespace ista;
  DataManager::initInst();
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
      const double result = STADC.checkDelay(check, AnalysisType::kMax, TransType::kRise, TransType::kRise, 0.1, 0.2);
      if (std::abs(result - 2.1) > 1e-10) {
        throw std::runtime_error("native timing check reversed clock/data slew");
      }
    }
  }
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}
