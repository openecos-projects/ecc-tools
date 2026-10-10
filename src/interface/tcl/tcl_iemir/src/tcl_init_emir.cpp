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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "EMIRInterface.hpp"
#include "tcl_emir.h"
#include "tcl_util.h"

namespace tcl {

// public

TclInitEMIR::TclInitEMIR(const char* cmd_name) : TclCmd(cmd_name)
{
  // std::string temp_directory_path;       // required
  _config_list.push_back(std::make_pair("-temp_directory_path", ValueType::kString));
  // std::string instance_power_file_path;  // required
  _config_list.push_back(std::make_pair("-instance_power_file_path", ValueType::kString));
  // std::string ploc_file_path;            // optional
  _config_list.push_back(std::make_pair("-ploc_file_path", ValueType::kString));
  _config_list.push_back(std::make_pair("-pad_files", ValueType::kStringList));
  _config_list.push_back(std::make_pair("-add_ploc_from_top_def", ValueType::kString));
  // std::string technology_file_path;    // required
  _config_list.push_back(std::make_pair("-technology_file_path", ValueType::kString));
  // double temperature_c;                  // optional
  _config_list.push_back(std::make_pair("-temperature_c", ValueType::kDouble));
  // std::string em_limit_file_path;        // optional
  _config_list.push_back(std::make_pair("-em_limit_file_path", ValueType::kString));
  // double em_violation_threshold_percent; // optional
  _config_list.push_back(std::make_pair("-em_violation_threshold_percent", ValueType::kDouble));
  // int32_t thread_number;                  // optional
  _config_list.push_back(std::make_pair("-thread_number", ValueType::kInt));

  _config_list.push_back(std::make_pair("-ir_solver", ValueType::kString));
  _config_list.push_back(std::make_pair("-ir_solver_tolerance", ValueType::kDouble));
  _config_list.push_back(std::make_pair("-ir_solver_max_iterations", ValueType::kInt));

  TclUtil::addOption(this, _config_list);
}

unsigned TclInitEMIR::exec()
{
  if (!check()) {
    return 0;
  }
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  try {
    auto def_sources = config_map.find("-add_ploc_from_top_def");
    if (def_sources != config_map.end()) {
      // Validate the complete token before the generic integer parser can
      // truncate malformed values such as "1.5" or "1garbage".
      const auto& value = std::any_cast<const std::string&>(def_sources->second);
      if (value != "0" && value != "1") throw std::invalid_argument("add_ploc_from_top_def must be 0 or 1");
      def_sources->second = static_cast<int32_t>(value == "1");
    }
    EMIRI.initEMIR(config_map);
    return 1;
  } catch (const std::exception& error) {
    Tcl_SetObjResult(ScriptEngine::getOrCreateInstance()->get_interp(), Tcl_NewStringObj(error.what(), -1));
    return 0;
  }
}

// private

}  // namespace tcl
