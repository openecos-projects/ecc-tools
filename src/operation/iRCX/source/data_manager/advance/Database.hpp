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
#pragma once

#include "CornerData.hpp"
#include "CornerNetPool.hpp"
#include "LayerTable.hpp"
#include "LayoutData.hpp"
#include "NetEnv.hpp"
#include "NetEtchProfile.hpp"
#include "RCData.hpp"
#include "SPEFNameData.hpp"

namespace ircx {

class Database
{
 public:
  Database() = default;
  ~Database() = default;
  // getter
  std::string& get_design_name() { return _layout_data.get_design_name(); }
  LayoutData& get_layout_data() { return _layout_data; }
  LayerTable& get_layer_table() { return _layer_table; }
  SPEFNameData& get_spef_name_data() { return _spef_name_data; }
  TopoPool& get_topo_pool() { return _topo_pool; }
  RCData& get_rc_data() { return _rc_data; }
  std::vector<CornerData>& get_corner_data_list() { return _corner_data_list; }
  std::vector<NetEnv>& get_net_env_list() { return _net_env_list; }
  CornerNetPool<NetEtchProfile>& get_corner_net_etch_profile_pool() { return _corner_net_etch_profile_pool; }
  // setter
  void set_design_name(const std::string& design_name) { _layout_data.set_design_name(design_name); }
  void set_layout_data(const LayoutData& layout_data) { _layout_data = layout_data; }
  void set_layer_table(const LayerTable& layer_table) { _layer_table = layer_table; }
  void set_spef_name_data(const SPEFNameData& spef_name_data) { _spef_name_data = spef_name_data; }
  void set_topo_pool(const TopoPool& topo_pool) { _topo_pool = topo_pool; }
  void set_rc_data(const RCData& rc_data) { _rc_data = rc_data; }
  void set_corner_data_list(const std::vector<CornerData>& corner_data_list) { _corner_data_list = corner_data_list; }
  void set_net_env_list(const std::vector<NetEnv>& net_env_list) { _net_env_list = net_env_list; }
  void set_corner_net_etch_profile_pool(const CornerNetPool<NetEtchProfile>& corner_net_etch_profile_pool)
  {
    _corner_net_etch_profile_pool = corner_net_etch_profile_pool;
  }
  // function

 private:
  LayoutData _layout_data;
  LayerTable _layer_table;
  SPEFNameData _spef_name_data;
  TopoPool _topo_pool;
  RCData _rc_data;
  std::vector<CornerData> _corner_data_list;
  std::vector<NetEnv> _net_env_list;
  CornerNetPool<NetEtchProfile> _corner_net_etch_profile_pool;
};

}  // namespace ircx
