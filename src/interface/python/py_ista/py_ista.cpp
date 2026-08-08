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
#include "py_ista.h"

#include "STAInterface.hpp"

namespace python_interface {

bool initStaConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);
void initStaConfigMapByDict(std::map<std::string, std::string>& config_dict, std::map<std::string, std::any>& config_map);

bool initSTA(std::string& config, std::map<std::string, std::string>& config_dict)
{
  std::map<std::string, std::any> config_map;

  bool pass = false;
  pass = config.empty() ? true : initStaConfigMapByJSON(config, config_map);
  if (!pass) {
    return false;
  }
  initStaConfigMapByDict(config_dict, config_map);
  STAI.initSTA(config_map);
  return true;
}

bool setPropagatedClock(const std::vector<std::string>& clock_name_list)
{
  std::string error_message;
  return STAI.setPropagatedClock(clock_name_list, error_message);
}

bool runSTA()
{
  STAI.runSTA();
  return true;
}

bool extractLib()
{
  STAI.extractLib();
  return true;
}

bool destroySTA()
{
  STAI.destroySTA();
  return true;
}

}  // namespace python_interface
