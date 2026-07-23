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
#include "py_idrc.h"

#include <tool_manager.h>

#include "../py_path_utils.h"
#include "DRCInterface.hpp"

namespace python_interface {

bool init_drc(const std::optional<std::filesystem::path>& temp_directory_path, const int& thread_number)
{
  const std::string temp_directory_path_ = path_or_empty(temp_directory_path);
  std::map<std::string, std::any> config_map;
  if (temp_directory_path_ != "") {
    config_map.insert(std::make_pair("-temp_directory_path", temp_directory_path_));
  }

  config_map.insert(std::make_pair("-thread_number", thread_number));

  DRCI.initDRC(config_map, false);
  return true;
}

bool run_drc(const std::optional<std::filesystem::path>& config, const std::optional<std::filesystem::path>& report)
{
  const std::string config_ = path_or_empty(config);
  const std::string report_ = path_or_empty(report);
  return iplf::tmInst->autoRunDRC(config_, report_, true);
}

bool save_drc(const std::optional<std::filesystem::path>& path)
{
  const std::string path_ = path_or_empty(path);
  return iplf::tmInst->saveDrcDetailToFile(path_);
}

}  // namespace python_interface