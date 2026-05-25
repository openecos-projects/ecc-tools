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
 * @file tcl_init_rcx.cpp
 * @author Yipei Xu (yipeix@163.com)
 * @brief
 * @version 0.1
 * @date 2025-12-09
 */
#include <iostream>

#include "RCXAPI.hh"
#include "tcl_ircx.h"
#include "tcl_util.h"

namespace tcl {

TclInitRCX::TclInitRCX(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list.push_back(std::make_pair("-thread", ValueType::kInt));
  _config_list.push_back(std::make_pair("-temperature", ValueType::kDouble));

  TclUtil::addOption(this, _config_list);
}

unsigned TclInitRCX::exec()
{
  if (!check()) {
    return 0;
  }

  const std::string hello_info =
      "\033[49;32m***************************\n"
      "  _  _____   _____ __   __\n"
      " (_)|  __ \\ /  __ \\\\ \\ / /\n"
      "  _ | |__) || |     \\ V / \n"
      " | ||  _  / | |      > <  \n"
      " | || | \\ \\ | |____ / . \\ \n"
      " |_||_|  \\_\\ \\_____/_/ \\_\\\n"
      "***************************\n"
      "WELCOME TO iRCX TCL-shell interface. \e[0m";
  std::cout << hello_info << std::endl;

  ircx::RCXInitOptions options;

  TclOption* thread_option = getOptionOrArg("-thread");
  if (thread_option && thread_option->is_set_val()) {
    options.thread_number = thread_option->getIntVal();
  }

  TclOption* temperature_option = getOptionOrArg("-temperature");
  if (temperature_option && temperature_option->is_set_val()) {
    options.operating_temperature = temperature_option->getDoubleVal();
  }

  RCX_API_INST.init(options);

  return 1;
}

}  // namespace tcl
