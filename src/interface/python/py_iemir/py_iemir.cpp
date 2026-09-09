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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "py_iemir.h"

#include <any>
#include <map>

#include "EMIRInterface.hpp"

namespace python_interface {

bool init_emir(const std::string& temp_directory_path, const std::string& instance_power_file_path, const int& thread_number)
{
  std::map<std::string, std::any> config_map;
  if (!temp_directory_path.empty()) {
    config_map.insert(std::make_pair("-temp_directory_path", temp_directory_path));
  }
  if (!instance_power_file_path.empty()) {
    config_map.insert(std::make_pair("-instance_power_file_path", instance_power_file_path));
  }
  config_map.insert(std::make_pair("-thread_number", thread_number));

  EMIRI.initEMIR(config_map);
  return true;
}

bool run_emir()
{
  EMIRI.runEMIR();
  return true;
}

bool destroy_emir()
{
  EMIRI.destroyEMIR();
  return true;
}

}  // namespace python_interface
