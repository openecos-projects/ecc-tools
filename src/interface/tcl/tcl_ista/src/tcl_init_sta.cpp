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
#include "tcl_sta.h"
#include "tcl_util.h"

namespace tcl {

// public

TclInitSTA::TclInitSTA(const char* cmd_name) : TclCmd(cmd_name)
{
  // std::string temp_directory_path;  // required
  _config_list.push_back(std::make_pair("-temp_directory_path", ValueType::kString));
  // int32_t thread_number;            // optional
  _config_list.push_back(std::make_pair("-thread_number", ValueType::kInt));
  // int32_t output_timing_reports;     // optional
  _config_list.push_back(std::make_pair("-output_timing_reports", ValueType::kInt));
  // int32_t output_timing_features;    // optional
  _config_list.push_back(std::make_pair("-output_timing_features", ValueType::kInt));
  // int32_t timing_path_limit;         // optional
  _config_list.push_back(std::make_pair("-timing_path_limit", ValueType::kInt));
  // std::string timing_corner;         // optional
  _config_list.push_back(std::make_pair("-timing_corner", ValueType::kString));
  // int32_t max_paths;                 // optional
  _config_list.push_back(std::make_pair("-max_paths", ValueType::kInt));
  // int32_t nworst;                    // optional
  _config_list.push_back(std::make_pair("-nworst", ValueType::kInt));
  // double slack_lesser_than;          // optional
  _config_list.push_back(std::make_pair("-slack_lesser_than", ValueType::kDouble));
  // double slack_greater_than;         // optional
  _config_list.push_back(std::make_pair("-slack_greater_than", ValueType::kDouble));
  // compatibility aliases
  _config_list.push_back(std::make_pair("-max_path", ValueType::kInt));
  _config_list.push_back(std::make_pair("-path_report_number", ValueType::kInt));
  // std::string delay_type;            // optional, max|min|max_min
  _config_list.push_back(std::make_pair("-delay_type", ValueType::kString));
  // std::string start_end_type;        // optional, all|reg_to_reg|reg_to_out|in_to_reg|in_to_out
  _config_list.push_back(std::make_pair("-start_end_type", ValueType::kString));

  TclUtil::addOption(this, _config_list);
}

unsigned TclInitSTA::exec()
{
  if (!check()) {
    return 0;
  }
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  STAI.initSTA(config_map);
  return 1;
}

// private

}  // namespace tcl
