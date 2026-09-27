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
#include "py_irt.h"

#include <string>

#include "RTInterface.hpp"

namespace python_interface {

bool initRTConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initRTConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);
bool initERTConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initERTConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);

bool initRT(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;

  bool pass = config.empty() ? true : initRTConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initRTConfigMapByDict(config_dict, config_map);
  RTI.initRT(config_map);
  return true;
}

bool runERT(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;

  bool pass = config.empty() ? true : initERTConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initERTConfigMapByDict(config_dict, config_map);
  RTI.runERT(config_map);
  return true;
}

bool runRT()
{
  RTI.runRT();
  return true;
}

bool destroyRT()
{
  RTI.destroyRT();
  return true;
}

}  // namespace python_interface
