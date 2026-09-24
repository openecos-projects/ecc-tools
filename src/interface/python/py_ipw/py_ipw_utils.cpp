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

#include <stdexcept>
#include <string>

#include "json_parser.h"
#include "py_ipw.h"

namespace python_interface {

bool initPwConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map)
{
  std::ifstream config_file(config);
  if (!config_file.is_open()) {
    return false;
  }

  nlohmann::json json;
  config_file >> json;

  std::string value = ecc::getJsonData(json, {"PW", "-temp_directory_path"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-temp_directory_path", value));
  }
  value = ecc::getJsonData(json, {"PW", "-thread_number"});
  if (!value.empty()) {
    config_map.insert(std::make_pair("-thread_number", std::stoi(value)));
  }
  value = ecc::getJsonData(json, {"PW", "-min_slew_degradation"});
  if (!value.empty()) {
    if (value != "0" && value != "1") {
      throw std::invalid_argument("-min_slew_degradation must be 0 or 1");
    }
    config_map.insert(std::make_pair("-min_slew_degradation", int32_t(value == "1")));
  }
  return true;
}

void initPwConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map)
{
  if (config_dict.count("-temp_directory_path") > 0 && !config_dict["-temp_directory_path"].empty()) {
    config_map["-temp_directory_path"] = config_dict["-temp_directory_path"];
  }
  if (config_dict.count("-thread_number") > 0 && !config_dict["-thread_number"].empty()) {
    config_map["-thread_number"] = std::stoi(config_dict["-thread_number"]);
  }
  if (config_dict.count("-min_slew_degradation") > 0 && !config_dict["-min_slew_degradation"].empty()) {
    std::string& value = config_dict.at("-min_slew_degradation");
    if (value != "0" && value != "1") {
      throw std::invalid_argument("-min_slew_degradation must be 0 or 1");
    }
    config_map["-min_slew_degradation"] = int32_t(value == "1");
  }
}

}  // namespace python_interface
