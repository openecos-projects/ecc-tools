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
#include "py_ifp.h"

#include <any>
#include <map>

#include "FPInterface.hpp"

namespace python_interface {

bool init_fp(const std::string& config)
{
  std::map<std::string, std::any> config_map;
  config_map["-config"] = config;
  FPI.initFP(config_map);
  return true;
}

bool run_fp()
{
  FPI.runFP();
  return true;
}

bool destroy_fp()
{
  FPI.destroyFP();
  return true;
}

}  // namespace python_interface
