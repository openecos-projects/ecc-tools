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
#include "RCXInterface.hpp"
#include "log/Log.hh"
#include "tcl_rcx.h"
#include "tcl_util.h"

namespace tcl {

// public

TclInitRCX::TclInitRCX(const char* cmd_name) : TclCmd(cmd_name)
{
  // std::string config_file_path;       // required
  _config_list.push_back(std::make_pair("-config", ValueType::kString));

  TclUtil::addOption(this, _config_list);
}

unsigned TclInitRCX::check()
{
  TclOption* config_option = getOptionOrArg("-config");
  if (config_option == nullptr || !config_option->is_set_val() || config_option->getStringVal() == nullptr
      || config_option->getStringVal()[0] == '\0') {
    LOG_ERROR << "init_rcx requires a non-empty -config file.";
    return 0;
  }
  return 1;
}

unsigned TclInitRCX::exec()
{
  if (!check()) {
    return 0;
  }
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  RCXI.initRCX(config_map);
  return 1;
}

// private

}  // namespace tcl
