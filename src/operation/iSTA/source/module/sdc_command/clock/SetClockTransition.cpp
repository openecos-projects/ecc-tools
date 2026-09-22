// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DataManager.hpp"
#include "object/ObjectQuery.hpp"
#include "clock/ClockCommands.hpp"

namespace ista::sdc {

TclSetClockTransition::TclSetClockTransition(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclDoubleOption("transition", 1));
  addOption(new ecc::TclStringOption("clocks", 1));
}

unsigned TclSetClockTransition::exec()
{
  ecc::TclOption* transition_option = getOptionOrArg("transition");
  ecc::TclOption* clock_option = getOptionOrArg("clocks");
  if (!transition_option->is_set_val() || !clock_option->is_set_val()) {
    setTclError("set_clock_transition requires a transition and a clock collection");
    return 0;
  }
  const double transition = transition_option->getDoubleVal();
  if (!std::isfinite(transition) || transition < 0.0) {
    setTclError("set_clock_transition must be finite and non-negative");
    return 0;
  }
  Database& database = STADM.getDatabase();
  const std::set<std::string> clocks = findClocks(database, parseObjectPatterns(clock_option->getStringVal(), false));
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  for (const std::string& name : clocks) {
    TimingClock& clock = database.get_timing_constraint().get_clock_map().at(name);
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis_type == AnalysisType::kMin && max && !min) || (analysis_type == AnalysisType::kMax && min && !max)) {
        continue;
      }
      if (!fall || rise) {
        clock.get_transition_map()[analysis_type][TransType::kRise] = transition;
      }
      if (!rise || fall) {
        clock.get_transition_map()[analysis_type][TransType::kFall] = transition;
      }
    }
  }
  return 1;
}

}  // namespace ista::sdc
