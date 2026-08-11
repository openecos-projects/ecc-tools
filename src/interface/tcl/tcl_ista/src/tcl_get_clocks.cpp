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
#include "STAInterface.hpp"
#include "tcl_ista_util.hpp"
#include "tcl_sta.h"

namespace tcl {

TclGetClocks::TclGetClocks(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringListOption("clocks", 1));
}

unsigned TclGetClocks::exec()
{
  TclOption* clock_option = getOptionOrArg("clocks");
  if (!clock_option->is_set_val()) {
    setTclError("get_clocks requires a clock list");
    return 0;
  }
  std::vector<std::string> resolved_clock_list;
  if (std::string error_message; !STAI.getClocks(clock_option->getStringList(), resolved_clock_list, error_message)) {
    setTclError(error_message);
    return 0;
  }
  setResult(resolved_clock_list);
  return 1;
}

}  // namespace tcl
