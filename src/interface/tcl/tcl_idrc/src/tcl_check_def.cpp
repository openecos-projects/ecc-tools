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
#include "utility/logger/Logger.hpp"
#include <set>

#include "DRCInterface.hpp"
#include "tcl_drc.h"
#include "tcl_util.h"

namespace tcl {

TclCheckDef::TclCheckDef(const char* cmd_name) : TclCmd(cmd_name)
{
}

unsigned TclCheckDef::exec()
{
  if (!check()) {
    return 0;
  }
  DRCI.checkDef();
  return 1;
}

CmdDRCAutoRun::CmdDRCAutoRun(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* file_name_option = new TclStringOption(TCL_CONFIG, 1, nullptr);
  auto* file_path_option = new TclStringOption(TCL_PATH, 1, nullptr);
  addOption(file_name_option);
  addOption(file_path_option);
}

unsigned CmdDRCAutoRun::check()
{
  TclOption* file_name_option = getOptionOrArg(TCL_CONFIG);
  TclOption* file_path_option = getOptionOrArg(TCL_PATH);
  ecc::checkTclOption(file_name_option, TCL_CONFIG);
  ecc::checkTclOption(file_path_option, TCL_PATH);
  return 1;
}

unsigned CmdDRCAutoRun::exec()
{
  if (!check()) {
    return 0;
  }

  DRCI.runDRC();
  ECCLOG.info(ecc::Loc::current(), "iDRC run successfully.");

  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdDRCSaveDetailFile::CmdDRCSaveDetailFile(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* file_path_option = new TclStringOption(TCL_PATH, 1, nullptr);
  addOption(file_path_option);
}

unsigned CmdDRCSaveDetailFile::check()
{
  TclOption* file_path_option = getOptionOrArg(TCL_PATH);
  ecc::checkTclOption(file_path_option, TCL_PATH);
  return 1;
}

unsigned CmdDRCSaveDetailFile::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path_option = getOptionOrArg(TCL_PATH);
  auto data_path = path_option->getStringVal();

  if (DRCI.saveDRC(data_path)) {
    ECCLOG.info(ecc::Loc::current(), "iDRC save detail drc to file success. path = ", data_path);
  }

  return 1;
}

}  // namespace tcl
