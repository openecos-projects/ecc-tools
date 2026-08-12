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
#include "py_izh.h"

#include <algorithm>
#include <any>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace python_interface {

namespace {

void setJsonValue(std::map<std::string, std::any>& config_map, const std::string& key, const nlohmann::json& value)
{
  if (value.is_string()) {
    config_map[key] = value.get<std::string>();
  } else if (value.is_number_integer()) {
    config_map[key] = value.get<int>();
  } else if (value.is_number_float()) {
    config_map[key] = value.get<double>();
  } else if (value.is_boolean()) {
    config_map[key] = value.get<bool>();
  } else if (value.is_array()) {
    if (value.empty() || std::all_of(value.begin(), value.end(), [](const nlohmann::json& item) { return item.is_string(); })) {
      config_map[key] = value.get<std::vector<std::string>>();
    } else if (std::all_of(value.begin(), value.end(), [](const nlohmann::json& item) { return item.is_number_integer(); })) {
      config_map[key] = value.get<std::vector<int>>();
    } else if (std::all_of(value.begin(), value.end(), [](const nlohmann::json& item) { return item.is_number(); })) {
      config_map[key] = value.get<std::vector<double>>();
    }
  }
}

void flattenJson(std::map<std::string, std::any>& config_map, const nlohmann::json& json)
{
  for (const auto& item : json.items()) {
    if (item.value().is_object()) {
      flattenJson(config_map, item.value());
    } else {
      setJsonValue(config_map, "-" + item.key(), item.value());
    }
  }
}

}  // namespace

bool initZHConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  if (config.empty()) {
    return false;
  }

  std::ifstream config_file(config);
  if (!config_file.is_open()) {
    return false;
  }

  nlohmann::json json;
  config_file >> json;
  flattenJson(config_map, json);

  if (json.contains("insert_buffer")) {
    setJsonValue(config_map, "-buffer_name", json["insert_buffer"]);
  }

  return true;
}

}  // namespace python_interface
