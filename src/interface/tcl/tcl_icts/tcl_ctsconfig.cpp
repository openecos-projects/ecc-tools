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
#include "tcl_ctsconfig.h"

#include <any>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "tcl_util.h"

namespace tcl {
namespace {

using ConfigOption = std::pair<std::string, ValueType>;

auto MakeSupportedCtsConfigOptions() -> std::vector<ConfigOption>
{
  return {
      {"-config_json_path", ValueType::kString},
      {"-skew_bound", ValueType::kDouble},
      {"-max_buf_tran", ValueType::kDouble},
      {"-root_input_slew", ValueType::kDouble},
      {"-max_sink_tran", ValueType::kDouble},
      {"-max_cap", ValueType::kDouble},
      {"-max_length", ValueType::kDouble},
      {"-wirelength_unit_um", ValueType::kDouble},
      {"-wirelength_iterations", ValueType::kInt},
      {"-slew_steps", ValueType::kInt},
      {"-cap_steps", ValueType::kInt},
      {"-wire_width", ValueType::kDouble},
      {"-max_fanout", ValueType::kInt},
      {"-routing_layer", ValueType::kIntList},
      {"-buffer_type", ValueType::kStringList},
      {"-char_buf_redundancy_pct", ValueType::kDouble},
      {"-force_branch_buffer", ValueType::kString},
      {"-htree_depth_explore_window", ValueType::kInt},
      {"-htree_topology_tolerance", ValueType::kDouble},
      {"-enable_sink_clustering", ValueType::kString},
  };
}

}  // namespace

CmdCTSConfig::CmdCTSConfig(const char* cmd_name) : TclCmd(cmd_name)
{
  _config_list = MakeSupportedCtsConfigOptions();
  TclUtil::addOption(this, _config_list);
}

unsigned CmdCTSConfig::exec()
{
  std::map<std::string, std::any> config_map = TclUtil::getConfigMap(this, _config_list);
  if (config_map.empty()) {
    return 0;
  }

  const auto config_path_iter = config_map.find("-config_json_path");
  if (config_path_iter == config_map.end()) {
    std::cerr << "CTS config update requires -config_json_path." << std::endl;
    return 0;
  }
  const auto config_json_path = std::any_cast<std::string>(config_path_iter->second);
  config_map.erase(config_path_iter);

  if (!config_map.empty() && !TclUtil::alterJsonConfig(config_json_path, config_map)) {
    return 0;
  }
  return 1;
}

}  // namespace tcl
