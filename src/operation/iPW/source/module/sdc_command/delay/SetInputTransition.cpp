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
#include "delay/DelayCommands.hpp"

namespace ipw::sdc {

TclSetInputTransition::TclSetInputTransition(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-rise"));
  addOption(new ecc::TclSwitchOption("-fall"));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclStringOption("-clock", 0));
  addOption(new ecc::TclSwitchOption("-clock_fall"));
  addOption(new ecc::TclDoubleOption("transition", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetInputTransition::exec()
{
  auto& data_manager = DataManager::getInst();

  if (getOptionOrArg("-clock")->is_set_val()) warn("set_input_transition -clock is deprecated and ignored");
  if (getOptionOrArg("-clock_fall")->is_set_val()) warn("set_input_transition -clock_fall is deprecated and ignored");

  ecc::TclOption* transition_option = getOptionOrArg("transition");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!transition_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_input_transition requires a transition and an object collection");
    return 0;
  }

  const double transition_value = transition_option->getDoubleVal();
  if (!std::isfinite(transition_value) || transition_value < 0.0) {
    setTclError("set_input_transition must be finite and non-negative");
    return 0;
  }
  const bool rise = getOptionOrArg("-rise")->is_set_val();
  const bool fall = getOptionOrArg("-fall")->is_set_val();
  const bool min = getOptionOrArg("-min")->is_set_val();
  const bool max = getOptionOrArg("-max")->is_set_val();
  for (const std::string& port_name : getPortPinNames(data_manager.getDatabase(), object_option->getStringList())) {
    TimingPortConstraint& port_constraint = getOrCreatePortConstraint(data_manager.getDatabase(), port_name);
    for (AnalysisType analysis_type : {AnalysisType::kMin, AnalysisType::kMax}) {
      if ((analysis_type == AnalysisType::kMin && max && !min) || (analysis_type == AnalysisType::kMax && min && !max)) {
        continue;
      }
      if (!fall || rise) {
        port_constraint.set_input_transition(analysis_type, TransType::kRise, transition_value);
      }
      if (!rise || fall) {
        port_constraint.set_input_transition(analysis_type, TransType::kFall, transition_value);
      }
    }
  }
  return 1;
}

}  // namespace ipw::sdc
