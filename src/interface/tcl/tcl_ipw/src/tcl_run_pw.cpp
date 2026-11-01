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
#include "PWInterface.hpp"
#include "tcl_pw.h"
#include "tcl_util.h"

namespace tcl {

TclRunPW::TclRunPW(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclSwitchOption("-avg"));
  addOption(new TclSwitchOption("-peak"));
}

unsigned TclRunPW::exec()
{
  if (!check()) {
    return 0;
  }
  TclOption* avg_option = getOptionOrArg("-avg");
  TclOption* peak_option = getOptionOrArg("-peak");

  PWI.runPW(peak_option->is_set_val() != 0, avg_option->is_set_val() != 0);
  return 1;
}

}  // namespace tcl
