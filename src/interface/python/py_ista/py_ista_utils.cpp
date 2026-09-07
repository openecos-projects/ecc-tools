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

  std::string value = ecc::getJsonData(json, {"STA", "-temp_directory_path"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-temp_directory_path", value));
  }
  value = ecc::getJsonData(json, {"STA", "-thread_number"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-thread_number", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-output_timing_reports"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-output_timing_reports", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-output_timing_features"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-output_timing_features", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-timing_path_limit"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-timing_path_limit", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-timing_corner"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-timing_corner", value));
  }
  value = ecc::getJsonData(json, {"STA", "-max_paths"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-max_paths", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-nworst"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-nworst", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-slack_lesser_than"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-slack_lesser_than", std::stod(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-slack_greater_than"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-slack_greater_than", std::stod(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-max_path"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-max_path", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"STA", "-path_report_number"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-path_report_number", std::stoi(value)));
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
  if (config_dict.count("-max_paths") > 0 && !config_dict["-max_paths"].empty()) {
    config_map["-max_paths"] = std::stoi(config_dict["-max_paths"]);
  }
  if (config_dict.count("-nworst") > 0 && !config_dict["-nworst"].empty()) {
    config_map["-nworst"] = std::stoi(config_dict["-nworst"]);
  }
  if (config_dict.count("-slack_lesser_than") > 0 && !config_dict["-slack_lesser_than"].empty()) {
    config_map["-slack_lesser_than"] = std::stod(config_dict["-slack_lesser_than"]);
  }
  if (config_dict.count("-slack_greater_than") > 0 && !config_dict["-slack_greater_than"].empty()) {
    config_map["-slack_greater_than"] = std::stod(config_dict["-slack_greater_than"]);
  }
  if (config_dict.count("-max_path") > 0 && !config_dict["-max_path"].empty()) {
    config_map["-max_path"] = std::stoi(config_dict["-max_path"]);
  }
  if (config_dict.count("-path_report_number") > 0 && !config_dict["-path_report_number"].empty()) {
    config_map["-path_report_number"] = std::stoi(config_dict["-path_report_number"]);
  }
}

}  // namespace python_interface
