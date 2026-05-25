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
 * @file tcl_read_mapping.cpp
 * @author Yipei Xu (yipeix@163.com)
 * @brief
 * @version 0.1
 * @date 2025-12-08
 */
#include "RCXAPI.hh"
#include "log/Log.hh"
#include "tcl_ircx.h"
#include "tcl_util.h"

namespace tcl {

TclReadMapping::TclReadMapping(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption("file_name", 1, nullptr));
}

unsigned TclReadMapping::check()
{
  TclOption* file_name_option = getOptionOrArg("file_name");
  if (!file_name_option || !file_name_option->is_set_val()) {
    LOG_ERROR << "read_mapping requires file_name.";
    return 0;
  }
  return 1;
}

unsigned TclReadMapping::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* file_name_option = getOptionOrArg("file_name");
  char* mapping_file = file_name_option->getStringVal();
  if (mapping_file == nullptr || mapping_file[0] == '\0') {
    LOG_ERROR << "mapping file is empty.";
    return 0;
  }

  return RCX_API_INST.readMapping(mapping_file) ? 1U : 0U;
}

}  // namespace tcl
