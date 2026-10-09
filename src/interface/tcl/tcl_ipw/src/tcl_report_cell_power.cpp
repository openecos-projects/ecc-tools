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

// public

TclReportCellPower::TclReportCellPower(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path_option = new TclStringOption("-path", 0);
  addOption(path_option);

  auto* top_option = new TclIntOption("-top", 0, 10);
  addOption(top_option);

  auto* all_option = new TclSwitchOption("-all");
  addOption(all_option);
}

unsigned TclReportCellPower::exec()
{
  if (!check()) {
    return 0;
  }
  TclOption* path_option = getOptionOrArg("-path");
  TclOption* top_option = getOptionOrArg("-top");
  TclOption* all_option = getOptionOrArg("-all");

  bool is_all = all_option->is_set_val() != 0;
  int32_t top_num = top_option->getIntVal();
  if (top_option->is_set_val() && is_all) {
    ECCLOG.warn(ecc::Loc::current(), "Both -top and -all are specified; -all takes precedence.");
  }
  if (!is_all && top_num < 1) {
    Tcl_SetObjResult(ecc::ScriptEngine::getOrCreateInstance()->get_interp(),
                     Tcl_NewStringObj("The -top option requires a positive integer.", -1));
    return 0;
  }

  std::string directory_path;
  if (path_option->is_set_val()) {
    directory_path = path_option->getStringVal();
  }
  PWI.reportCellPower(directory_path, top_num, is_all);
  return 1;
}

// private

}  // namespace tcl
