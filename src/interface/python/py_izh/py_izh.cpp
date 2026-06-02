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

#include <any>
#include <map>
#include <string>

#include "ZHInterface.hpp"

namespace python_interface {

bool initZHConfigMapByJSON(const std::string& config, std::map<std::string, std::any>& config_map);

bool fix_fanout(const std::string& config)
{
  std::map<std::string, std::any> config_map;

  bool pass = false;
  pass = !pass ? initZHConfigMapByJSON(config, config_map) : pass;
  if (!pass) {
    return false;
  }

  ZHI.fixFanout(config_map);
  return true;
}

bool insert_filler(const std::string& config)
{
  std::map<std::string, std::any> config_map;

  bool pass = false;
  pass = !pass ? initZHConfigMapByJSON(config, config_map) : pass;
  if (!pass) {
    return false;
  }

  ZHI.insertFiller(config_map);
  return true;
}

}  // namespace python_interface
