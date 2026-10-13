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
#include "py_imj.h"

namespace python_interface {

bool initMJConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }
  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"MJ", "-temp_directory_path"});
  if (!value.empty()) {
    config_map["-temp_directory_path"] = value;
  }
  return true;
}

void initMJConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-temp_directory_path") > 0 && !config_dict["-temp_directory_path"].empty()) {
    config_map["-temp_directory_path"] = config_dict["-temp_directory_path"];
  }
}

bool initFillerConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }
  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"MJ", "-filler"});
  if (!value.empty()) {
    config_map["-filler"] = value;
  }
  return true;
}

void initFillerConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-filler") > 0 && !config_dict["-filler"].empty()) {
    config_map["-filler"] = config_dict["-filler"];
  }
}

bool initAntennaConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }
  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"MJ", "-report_dir"});
  if (!value.empty()) {
    config_map["-report_dir"] = value;
  }
  return true;
}

void initAntennaConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-report_dir") > 0 && !config_dict["-report_dir"].empty()) {
    config_map["-report_dir"] = config_dict["-report_dir"];
  }
}

bool initMetalConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }
  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"MJ", "-min_fill_layer"});
  if (!value.empty()) {
    config_map["-min_fill_layer"] = value;
  }
  value = ecc::getJsonData(json, {"MJ", "-max_fill_layer"});
  if (!value.empty()) {
    config_map["-max_fill_layer"] = value;
  }
  return true;
}

void initMetalConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-min_fill_layer") > 0 && !config_dict["-min_fill_layer"].empty()) {
    config_map["-min_fill_layer"] = config_dict["-min_fill_layer"];
  }
  if (config_dict.count("-max_fill_layer") > 0 && !config_dict["-max_fill_layer"].empty()) {
    config_map["-max_fill_layer"] = config_dict["-max_fill_layer"];
  }
}

bool initDefFlattenConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }
  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"MJ", "-hierarchy"});
  if (!value.empty()) {
    config_map["-hierarchy"] = value;
  }
  value = ecc::getJsonData(json, {"MJ", "-pg_connect"});
  if (!value.empty()) {
    config_map["-pg_connect"] = value;
  }
  return true;
}

void initDefFlattenConfigMapByDict(std::map<std::string, std::string>& config_dict,
                                   std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-hierarchy") > 0 && !config_dict["-hierarchy"].empty()) {
    config_map["-hierarchy"] = config_dict["-hierarchy"];
  }
  if (config_dict.count("-pg_connect") > 0 && !config_dict["-pg_connect"].empty()) {
    config_map["-pg_connect"] = config_dict["-pg_connect"];
  }
}

}  // namespace python_interface
