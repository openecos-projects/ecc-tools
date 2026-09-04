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
#include "STAHeader.hpp"
#include "SdcCommandUtils.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclSetClockUncertainty::TclSetClockUncertainty(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-setup"));
  addOption(new ecc::TclSwitchOption("-hold"));
  addOption(new ecc::TclDoubleOption("uncertainty", 1));
  addOption(new ecc::TclStringListOption("clocks", 1));
}

unsigned TclSetClockUncertainty::exec()
{
  auto& data_manager = DataManager::getInst();

  ecc::TclOption* uncertainty_option = getOptionOrArg("uncertainty");
  ecc::TclOption* clock_option = getOptionOrArg("clocks");
  if (!uncertainty_option->is_set_val() || !clock_option->is_set_val()) {
    setTclError("set_clock_uncertainty requires an uncertainty and a clock collection");
    return 0;
  }

  const double uncertainty = uncertainty_option->getDoubleVal();
  if (!std::isfinite(uncertainty) || uncertainty < 0.0) {
    setTclError("set_clock_uncertainty must be non-negative");
    return 0;
  }

  const std::vector<std::string> clock_list = clock_option->getStringList();
  if (clock_list.empty()) {
    setTclError("set_clock_uncertainty requires a clock collection");
    return 0;
  }
  auto& clock_map = data_manager.getDatabase().get_timing_constraint().get_clock_map();
  const auto clock_it = clock_map.find(clock_list.front());
  if (clock_it == clock_map.end()) {
    setTclError("clock '" + clock_list.front() + "' does not exist");
    return 0;
  }

  const bool setup = getOptionOrArg("-setup")->is_set_val();
  const bool hold = getOptionOrArg("-hold")->is_set_val();
  if (!hold || setup) {
    clock_it->second.set_setup_uncertainty(uncertainty);
  }
  if (!setup || hold) {
    clock_it->second.set_hold_uncertainty(uncertainty);
  }
  return 1;
}

}  // namespace ista::sdc
