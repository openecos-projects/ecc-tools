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

TclSetPropagatedClock::TclSetPropagatedClock(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringListOption("clocks", 1));
}

unsigned TclSetPropagatedClock::exec()
{
  TclOption* clock_option = getOptionOrArg("clocks");
  if (!clock_option->is_set_val()) {
    setTclError("set_propagated_clock requires a clock collection");
    return 0;
  }
  if (std::string error_message; !STAI.setPropagatedClock(clock_option->getStringList(), error_message)) {
    setTclError(error_message);
    return 0;
  }
  return 1;
}

}  // namespace tcl
