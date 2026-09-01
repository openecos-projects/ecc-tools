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

TclSetInputDelay::TclSetInputDelay(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("-clock", 0));
  addOption(new ecc::TclSwitchOption("-min"));
  addOption(new ecc::TclSwitchOption("-max"));
  addOption(new ecc::TclDoubleOption("delay", 1));
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclSetInputDelay::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* delay_option = getOptionOrArg("delay");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!delay_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_input_delay requires a delay and an object collection");
    return 0;
  }

  ecc::TclOption* clock_option = getOptionOrArg("-clock");
  const double delay_value = delay_option->getDoubleVal();
  const bool set_min = getOptionOrArg("-min")->is_set_val();
  const bool set_max = getOptionOrArg("-max")->is_set_val();
  const std::string clock_name = clock_option->is_set_val() ? clock_option->getStringVal() : std::string{};
  for (const std::string& port_name : resolveObjectList(data_manager.getDatabase(), object_option->getStringList())) {
    TimingPortConstraint& port_constraint = getPortConstraint(data_manager.getDatabase(), port_name);
    port_constraint.set_clock_name(clock_name);
    if (set_min && !set_max) {
      port_constraint.set_input_delay_min(delay_value);
      port_constraint.set_has_input_delay_min(true);
    } else if (set_max && !set_min) {
      port_constraint.set_input_delay_max(delay_value);
      port_constraint.set_has_input_delay_max(true);
    } else {
      port_constraint.set_input_delay_min(delay_value);
      port_constraint.set_input_delay_max(delay_value);
      port_constraint.set_has_input_delay_min(true);
      port_constraint.set_has_input_delay_max(true);
    }
  }
  return 1;
}

}  // namespace ista::sdc
