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

#include "py_path_utils.h"
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
bool initDEFCutConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initDEFCutConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);

bool initMJ(const std::optional<std::filesystem::path>& config, std::map<std::string, std::string>& config_dict)
{
  const std::string config_ = path_or_empty(config);
  std::map<std::string, std::any> config_map;
  bool pass = config_.empty() ? true : initMJConfigMapByJSON(config_, config_map);
  if (!pass) {
    return false;
  }
  initMJConfigMapByDict(config_dict, config_map);
  MJI.initMJ(config_map);
  return true;
}

bool insertFiller(const std::optional<std::filesystem::path>& config, std::map<std::string, std::string>& config_dict)
{
  const std::string config_ = path_or_empty(config);
  std::map<std::string, std::any> config_map;
  bool pass = config_.empty() ? true : initFillerConfigMapByJSON(config_, config_map);
  if (!pass) {
    return false;
  }
  initFillerConfigMapByDict(config_dict, config_map);
  MJI.insertFiller(config_map);
  return true;
}

bool checkAntenna(const std::optional<std::filesystem::path>& config, std::map<std::string, std::string>& config_dict)
{
  const std::string config_ = path_or_empty(config);
  std::map<std::string, std::any> config_map;
  bool pass = config_.empty() ? true : initAntennaConfigMapByJSON(config_, config_map);
  if (!pass) {
    return false;
  }
  initAntennaConfigMapByDict(config_dict, config_map);
  MJI.checkAntenna(config_map);
  return true;
}

bool insertMetal(const std::optional<std::filesystem::path>& config, std::map<std::string, std::string>& config_dict)
{
  const std::string config_ = path_or_empty(config);
  std::map<std::string, std::any> config_map;
  bool pass = config_.empty() ? true : initMetalConfigMapByJSON(config_, config_map);
  if (!pass) {
    return false;
  }
  initMetalConfigMapByDict(config_dict, config_map);
  MJI.insertMetal(config_map);
  return true;
}

bool defFlatten(const std::optional<std::filesystem::path>& config, std::map<std::string, std::string>& config_dict)
{
  const std::string config_ = path_or_empty(config);
  std::map<std::string, std::any> config_map;
  bool pass = config_.empty() ? true : initDefFlattenConfigMapByJSON(config_, config_map);
  if (!pass) {
    return false;
  }
  initDefFlattenConfigMapByDict(config_dict, config_map);
  MJI.flattenDef(config_map);
  return true;
}

bool analyzeDriver()
{
  MJI.analyzeDriver();
  return true;
}

bool cutDEF(const std::optional<std::filesystem::path>& config, std::map<std::string, std::string>& config_dict)
{
  const std::string config_ = path_or_empty(config);
  std::map<std::string, std::any> config_map;
  if (!config_.empty() && !initDEFCutConfigMapByJSON(config_, config_map)) {
    return false;
  }
  initDEFCutConfigMapByDict(config_dict, config_map);
  MJI.cutDEF(config_map);
  return true;
}

bool destroyMJ()
{
  MJI.destroyMJ();
  return true;
}

}  // namespace python_interface
