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
#include "Logger.hpp"
#include "SdcCommands.hpp"

namespace ista::sdc {

TclGetClocks::TclGetClocks(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("clocks", 1));
}

unsigned TclGetClocks::exec()
{
  ecc::TclOption* clock_option = getOptionOrArg("clocks");
  if (!clock_option->is_set_val()) {
    setTclError("get_clocks requires a clock list");
    return 0;
  }
  const std::vector<std::string> clock_name_list = clock_option->getStringList();
  if (clock_name_list.empty()) {
    setTclError("get_clocks requires at least one clock name");
    return 0;
  }

  auto& clock_map = STADM.getDatabase().get_timing_constraint().get_clock_map();
  std::vector<std::string> resolved_clocks;
  resolved_clocks.reserve(clock_name_list.size());
  for (const std::string& clock_name : clock_name_list) {
    if (!clock_map.contains(clock_name)) {
      STALOG.error(Loc::current(), "clock '", clock_name, "' does not exist");
      setTclError("clock does not exist");
      return 0;
    }
    resolved_clocks.push_back(clock_name);
  }
  setResult(std::move(resolved_clocks));
  return 1;
}

}  // namespace ista::sdc
