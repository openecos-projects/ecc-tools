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
#include <string>

#include "json_parser.h"
#include "py_irt.h"

namespace python_interface {
bool initRTConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }
  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"RT", "-temp_directory_path"});
  if (!value.empty()) {
    config_map["-temp_directory_path"] = value;
  }
  value = ecc::getJsonData(json, {"RT", "-bottom_routing_layer"});
  if (!value.empty()) {
    config_map["-bottom_routing_layer"] = value;
  }
  value = ecc::getJsonData(json, {"RT", "-top_routing_layer"});
  if (!value.empty()) {
    config_map["-top_routing_layer"] = value;
  }
  value = ecc::getJsonData(json, {"RT", "-thread_number"});
  if (!value.empty()) {
    config_map["-thread_number"] = std::stoi(value);
  }
  value = ecc::getJsonData(json, {"RT", "-output_inter_result"});
  if (!value.empty()) {
    config_map["-output_inter_result"] = std::stoi(value);
  }

  return true;
}

void initRTConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-temp_directory_path") > 0 && !config_dict["-temp_directory_path"].empty()) {
    config_map["-temp_directory_path"] = config_dict["-temp_directory_path"];
  }
  if (config_dict.count("-bottom_routing_layer") > 0 && !config_dict["-bottom_routing_layer"].empty()) {
    config_map["-bottom_routing_layer"] = config_dict["-bottom_routing_layer"];
  }
  if (config_dict.count("-top_routing_layer") > 0 && !config_dict["-top_routing_layer"].empty()) {
    config_map["-top_routing_layer"] = config_dict["-top_routing_layer"];
  }
  if (config_dict.count("-thread_number") > 0 && !config_dict["-thread_number"].empty()) {
    config_map["-thread_number"] = std::stoi(config_dict["-thread_number"]);
  }
  if (config_dict.count("-output_inter_result") > 0 && !config_dict["-output_inter_result"].empty()) {
    config_map["-output_inter_result"] = std::stoi(config_dict["-output_inter_result"]);
  }
}

bool initERTConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }
  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"RT", "-stage"});
  if (!value.empty()) {
    config_map["-stage"] = value;
  }
  value = ecc::getJsonData(json, {"RT", "-resolve_congestion"});
  if (!value.empty()) {
    config_map["-resolve_congestion"] = value;
  }

  return true;
}

void initERTConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-stage") > 0 && !config_dict["-stage"].empty()) {
    config_map["-stage"] = config_dict["-stage"];
  }
  if (config_dict.count("-resolve_congestion") > 0 && !config_dict["-resolve_congestion"].empty()) {
    config_map["-resolve_congestion"] = config_dict["-resolve_congestion"];
  }
}

}  // namespace python_interface
