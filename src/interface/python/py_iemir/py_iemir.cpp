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

#include "py_path_utils.h"
#include "EMIRInterface.hpp"

namespace python_interface {

bool init_emir(const std::optional<std::filesystem::path>& temp_directory_path,
               const std::optional<std::filesystem::path>& instance_power_file_path, const int& thread_number,
               const std::optional<std::filesystem::path>& technology_file_path,
               const std::optional<std::filesystem::path>& ploc_file_path, const std::vector<std::filesystem::path>& pad_files,
               bool add_ploc_from_top_def)
{
  const std::string temp_directory_path_ = path_or_empty(temp_directory_path);
  const std::string instance_power_file_path_ = path_or_empty(instance_power_file_path);
  const std::string technology_file_path_ = path_or_empty(technology_file_path);
  const std::string ploc_file_path_ = path_or_empty(ploc_file_path);
  std::vector<std::string> pad_files_;
  pad_files_.reserve(pad_files.size());
  for (const auto& pad_file : pad_files) {
    pad_files_.push_back(pad_file.string());
  }

  std::map<std::string, std::any> config_map;
  if (!temp_directory_path_.empty()) {
    config_map.insert(std::make_pair("-temp_directory_path", temp_directory_path_));
  }
  if (!instance_power_file_path_.empty()) {
    config_map.insert(std::make_pair("-instance_power_file_path", instance_power_file_path_));
  }
  config_map.insert(std::make_pair("-thread_number", thread_number));
  if (!technology_file_path_.empty()) config_map["-technology_file_path"] = technology_file_path_;
  if (!ploc_file_path_.empty()) config_map["-ploc_file_path"] = ploc_file_path_;
  if (!pad_files_.empty()) config_map["-pad_files"] = pad_files_;
  config_map["-add_ploc_from_top_def"] = static_cast<int32_t>(add_ploc_from_top_def);

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
