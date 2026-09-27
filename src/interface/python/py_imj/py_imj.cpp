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
#include "py_imj.h"

#include <string>

#include "MJInterface.hpp"

namespace python_interface {

bool initMJConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initMJConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);
bool initFillerConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initFillerConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);
bool initAntennaConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initAntennaConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);
bool initMetalConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initMetalConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);
bool initDefFlattenConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initDefFlattenConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);

bool initMJ(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;
  bool pass = config.empty() ? true : initMJConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initMJConfigMapByDict(config_dict, config_map);
  MJI.initMJ(config_map);
  return true;
}

bool insertFiller(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;
  bool pass = config.empty() ? true : initFillerConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initFillerConfigMapByDict(config_dict, config_map);
  MJI.insertFiller(config_map);
  return true;
}

bool checkAntenna(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;
  bool pass = config.empty() ? true : initAntennaConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initAntennaConfigMapByDict(config_dict, config_map);
  MJI.checkAntenna(config_map);
  return true;
}

bool insertMetal(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;
  bool pass = config.empty() ? true : initMetalConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initMetalConfigMapByDict(config_dict, config_map);
  MJI.insertMetal(config_map);
  return true;
}

bool defFlatten(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;
  bool pass = config.empty() ? true : initDefFlattenConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initDefFlattenConfigMapByDict(config_dict, config_map);
  MJI.flattenDef(config_map);
  return true;
}

bool destroyMJ()
{
  MJI.destroyMJ();
  return true;
}

}  // namespace python_interface
