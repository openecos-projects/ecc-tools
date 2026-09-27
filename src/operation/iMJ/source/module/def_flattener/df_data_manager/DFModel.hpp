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
#pragma once

#include "DFConfig.hpp"
#include "DFHierarchy.hpp"
#include "DFNetUnion.hpp"
#include "DFSource.hpp"

namespace imj {

class DFModel
{
 public:
  DFModel() = default;
  ~DFModel() = default;
  DFModel(const DFModel& other) = delete;
  DFModel(DFModel&& other) = default;
  DFModel& operator=(const DFModel& other) = delete;
  DFModel& operator=(DFModel&& other) = default;
  // getter
  DFConfig& get_df_config() { return _df_config; }
  DFHierarchy& get_df_hierarchy() { return _df_hierarchy; }
  std::map<std::string, DFSource>& get_child_master_to_df_source_map() { return _child_master_to_df_source_map; }
  DFNetUnion& get_regular_net_union() { return _regular_net_union; }
  DFNetUnion& get_special_net_union() { return _special_net_union; }
  DFSource* get_df_source(const std::string& master_name)
  {
    std::map<std::string, DFSource>::iterator iter = _child_master_to_df_source_map.find(master_name);
    return iter == _child_master_to_df_source_map.end() ? nullptr : &iter->second;
  }
  bool has_df_source(const std::string& master_name) { return get_df_source(master_name) != nullptr; }
  // const getter
  const DFConfig& get_df_config() const { return _df_config; }
  const DFHierarchy& get_df_hierarchy() const { return _df_hierarchy; }
  // setter
  void set_df_config(const DFConfig& df_config) { _df_config = df_config; }
  // function

 private:
  DFConfig _df_config;
  DFHierarchy _df_hierarchy;
  std::map<std::string, DFSource> _child_master_to_df_source_map;
  DFNetUnion _regular_net_union;
  DFNetUnion _special_net_union;
};

}  // namespace imj
