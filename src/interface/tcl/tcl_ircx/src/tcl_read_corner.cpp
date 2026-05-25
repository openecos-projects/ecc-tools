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
/**
 * @file tcl_read_corner.cpp
 * @author Yipei Xu (yipeix@163.com)
 * @brief Tcl command wrapper for binding one ITF with one captab.
 * @version 0.1
 * @date 2025-12-08
 */
#include "RCXAPI.hh"
#include "log/Log.hh"
#include "tcl_ircx.h"
#include "tcl_util.h"

namespace tcl {

TclReadCorner::TclReadCorner(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list.push_back(std::make_pair("-name", ValueType::kString));
  _config_list.push_back(std::make_pair("-itf", ValueType::kString));
  _config_list.push_back(std::make_pair("-captab", ValueType::kString));

  TclUtil::addOption(this, _config_list);
}

unsigned TclReadCorner::check()
{
  TclOption* name_option = getOptionOrArg("-name");
  TclOption* itf_option = getOptionOrArg("-itf");
  TclOption* captab_option = getOptionOrArg("-captab");
  if (!name_option || !name_option->is_set_val()) {
    LOG_ERROR << "read_corner requires -name.";
    return 0;
  }
  if (!itf_option || !itf_option->is_set_val()) {
    LOG_ERROR << "read_corner requires -itf.";
    return 0;
  }
  if (!captab_option || !captab_option->is_set_val()) {
    LOG_ERROR << "read_corner requires -captab.";
    return 0;
  }
  return 1;
}

unsigned TclReadCorner::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* name_option = getOptionOrArg("-name");
  TclOption* itf_option = getOptionOrArg("-itf");
  TclOption* captab_option = getOptionOrArg("-captab");

  char* corner_name = name_option->getStringVal();
  char* itf_file = itf_option->getStringVal();
  char* captab_file = captab_option->getStringVal();

  if (corner_name == nullptr || corner_name[0] == '\0') {
    LOG_ERROR << "corner name is empty.";
    return 0;
  }
  if (itf_file == nullptr || itf_file[0] == '\0') {
    LOG_ERROR << "itf file is empty.";
    return 0;
  }
  if (captab_file == nullptr || captab_file[0] == '\0') {
    LOG_ERROR << "captab file is empty.";
    return 0;
  }

  return RCX_API_INST.readCorner(corner_name, itf_file, captab_file) ? 1U : 0U;
}

}  // namespace tcl
