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
#include <tcl_util.h>

#include <string>

#include "json_parser.h"
#include "py_ista.h"

namespace python_interface {

bool initStaConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }

  nlohmann::json json;
  config_file >> json;

  std::string value = ieda::getJsonData(json, {"STA", "-temp_directory_path"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-temp_directory_path", value));
  }
  value = ieda::getJsonData(json, {"STA", "-thread_number"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-thread_number", std::stoi(value)));
  }
  return true;
}

void initStaConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-temp_directory_path") > 0 && !config_dict["-temp_directory_path"].empty()) {
    config_map["-temp_directory_path"] = config_dict["-temp_directory_path"];
  }
  if (config_dict.count("-thread_number") > 0 && !config_dict["-thread_number"].empty()) {
    config_map["-thread_number"] = std::stoi(config_dict["-thread_number"]);
  }
}

}  // namespace python_interface
