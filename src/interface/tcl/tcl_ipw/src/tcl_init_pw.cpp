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

TclInitPW::TclInitPW(const char* cmd_name) : TclCmd(cmd_name)
{
  // std::string temp_directory_path;  // required
  _config_list.push_back(std::make_pair("-temp_directory_path", ValueType::kString));
  // int32_t thread_number;             // optional
  _config_list.push_back(std::make_pair("-thread_number", ValueType::kInt));
  // int32_t min_slew_degradation;      // optional
  _config_list.push_back(std::make_pair("-min_slew_degradation", ValueType::kString));
  TclUtil::addOption(this, _config_list);
}

unsigned TclInitPW::exec()
{
  if (!check()) {
    return 0;
  }
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  if (config_map.contains("-min_slew_degradation")) {
    std::string value = std::any_cast<std::string>(config_map.at("-min_slew_degradation"));
    if (value != "0" && value != "1") {
      Tcl_SetObjResult(ecc::ScriptEngine::getOrCreateInstance()->get_interp(),
                       Tcl_NewStringObj("-min_slew_degradation must be 0 or 1", -1));
      return 0;
    }
    config_map["-min_slew_degradation"] = int32_t(value == "1");
  }
  try {
    PWI.initPW(config_map);
  } catch (const std::exception& error) {
    Tcl_SetObjResult(ecc::ScriptEngine::getOrCreateInstance()->get_interp(), Tcl_NewStringObj(error.what(), -1));
    return 0;
  }
  return 1;
}

// private

}  // namespace tcl
