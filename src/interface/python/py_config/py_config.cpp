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
#include "py_config.h"

#include "../py_path_utils.h"
#include <flow.h>
#include <idm.h>
#include <tool_manager.h>

namespace python_interface {

bool flow_init(const std::filesystem::path& flow_config)
{
  const std::string flow_config_ = flow_config.string();
  bool init_ok = iplf::plfInst->initFlow(flow_config_);
  return init_ok;
}

bool db_init(const std::optional<std::filesystem::path>& config_path, const std::optional<std::filesystem::path>& tech_lef_path,
             const std::vector<std::filesystem::path>& lef_paths, const std::optional<std::filesystem::path>& def_path,
             const std::optional<std::filesystem::path>& verilog_path, const std::optional<std::filesystem::path>& output_path,
             const std::optional<std::filesystem::path>& feature_path, const std::vector<std::filesystem::path>& lib_paths,
             const std::optional<std::filesystem::path>& sdc_path)
{
  const std::string config_path_ = path_or_empty(config_path);
  const std::string tech_lef_path_ = path_or_empty(tech_lef_path);
  std::vector<std::string> lef_paths_;
  lef_paths_.reserve(lef_paths.size());
  for (const auto& lef_path : lef_paths) {
    lef_paths_.push_back(lef_path.string());
  }
  const std::string def_path_ = path_or_empty(def_path);
  const std::string verilog_path_ = path_or_empty(verilog_path);
  const std::string output_path_ = path_or_empty(output_path);
  const std::string feature_path_ = path_or_empty(feature_path);
  std::vector<std::string> lib_paths_;
  lib_paths_.reserve(lib_paths.size());
  for (const auto& lib_path : lib_paths) {
    lib_paths_.push_back(lib_path.string());
  }
  const std::string sdc_path_ = path_or_empty(sdc_path);

  idm::DataConfig& dm_config = dmInst->get_config();
  if (not config_path_.empty()) {
    bool init_ok = dm_config.initConfig(config_path_);
    if (not init_ok) {
      return false;
    }
  }
  if (not tech_lef_path_.empty()) {
    dm_config.set_tech_lef_path(tech_lef_path_);
  }
  if (not lef_paths_.empty()) {
    dm_config.set_lef_paths(lef_paths_);
  }
  if (not def_path_.empty()) {
    dm_config.set_def_path(def_path_);
  }
  if (not verilog_path_.empty()) {
    dm_config.set_verilog_path(verilog_path_);
  }
  if (not output_path_.empty()) {
    dm_config.set_output_path(output_path_);
  }
  if (not lib_paths_.empty()) {
    dm_config.set_lib_paths(lib_paths_);
  }
  if (not sdc_path_.empty()) {
    dm_config.set_sdc_path(sdc_path_);
  }
  if (not feature_path_.empty()) {
    dm_config.set_feature_path(feature_path_);
  }
  return true;
}

}  // namespace python_interface