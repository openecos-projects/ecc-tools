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
  value = ieda::getJsonData(json, {"STA", "-output_timing_reports"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-output_timing_reports", std::stoi(value)));
  }
  value = ieda::getJsonData(json, {"STA", "-output_timing_features"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-output_timing_features", std::stoi(value)));
  }
  value = ieda::getJsonData(json, {"STA", "-timing_path_limit"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-timing_path_limit", std::stoi(value)));
  }
  value = ieda::getJsonData(json, {"STA", "-timing_corner"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-timing_corner", value));
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
  if (config_dict.count("-output_timing_reports") > 0 && !config_dict["-output_timing_reports"].empty()) {
    config_map["-output_timing_reports"] = std::stoi(config_dict["-output_timing_reports"]);
  }
  if (config_dict.count("-output_timing_features") > 0 && !config_dict["-output_timing_features"].empty()) {
    config_map["-output_timing_features"] = std::stoi(config_dict["-output_timing_features"]);
  }
  if (config_dict.count("-timing_path_limit") > 0 && !config_dict["-timing_path_limit"].empty()) {
    config_map["-timing_path_limit"] = std::stoi(config_dict["-timing_path_limit"]);
  }
  if (config_dict.count("-timing_corner") > 0 && !config_dict["-timing_corner"].empty()) {
    config_map["-timing_corner"] = config_dict["-timing_corner"];
  }
}

}  // namespace python_interface
