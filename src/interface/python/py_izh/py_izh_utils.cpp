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
#include "py_izh.h"

namespace python_interface {

bool initAntennaConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }

  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"ZH", "-report_dir"});
  if (!value.empty()) {
    config_map["-report_dir"] = value;
  }
  return true;
}

bool initFillerConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }

  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"ZH", "-filler"});
  if (!value.empty()) {
    config_map["-filler"] = value;
  }
  return true;
}

bool initMetalConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  auto config_file = std::ifstream(config);
  if (!config_file.is_open()) {
    return false;
  }

  nlohmann::json json;
  config_file >> json;
  std::string value = ecc::getJsonData(json, {"ZH", "-min_fill_layer"});
  if (!value.empty()) {
    config_map["-min_fill_layer"] = value;
  }
  value = ecc::getJsonData(json, {"ZH", "-max_fill_layer"});
  if (!value.empty()) {
    config_map["-max_fill_layer"] = value;
  }
  return true;
}

}  // namespace python_interface
