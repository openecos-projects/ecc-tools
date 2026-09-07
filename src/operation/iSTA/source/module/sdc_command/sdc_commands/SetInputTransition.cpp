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
#include "SdcCommandUtils.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclSetInputTransition::TclSetInputTransition(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclDoubleOption("transition", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetInputTransition::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* transition_option = getOptionOrArg("transition");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!transition_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_input_transition requires a transition and an object collection");
    return 0;
  }

  const double transition_value = transition_option->getDoubleVal();
  for (const std::string& port_name : resolveObjectList(data_manager.getDatabase(), object_option->getStringList())) {
    TimingPortConstraint& port_constraint = getPortConstraint(data_manager.getDatabase(), port_name);
    port_constraint.set_input_transition(transition_value);
    port_constraint.set_has_input_transition(true);
  }
  return 1;
}

}  // namespace ista::sdc
