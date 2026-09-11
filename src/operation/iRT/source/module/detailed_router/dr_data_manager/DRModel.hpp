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

#include "DRBox.hpp"
#include "DRBoxId.hpp"
#include "DRIterParam.hpp"
#include "DRModelResult.hpp"
#include "DRNet.hpp"
#include "GridMap.hpp"

namespace irt {

class DRModel
{
 public:
  DRModel() = default;
  ~DRModel() = default;
  // getter
  DRModelResult& get_curr_result() { return _curr_result; }
  DRModelResult& get_previous_result() { return _previous_result; }
  DRModelResult& get_best_result() { return _best_result; }
  std::vector<DRNet>& get_dr_net_list() { return _dr_net_list; }
  bool get_initial_routing() const { return _initial_routing; }
  int32_t get_iter() const { return _iter; }
  DRIterParam& get_dr_iter_param() { return _dr_iter_param; }
  DRIterParam& get_previous_dr_iter_param() { return _previous_dr_iter_param; }
  bool get_refine_enabled() const { return _refine_enabled; }
  GridMap<DRBox>& get_dr_box_map() { return _dr_box_map; }
  std::vector<std::vector<DRBoxId>>& get_dr_box_id_list_list() { return _dr_box_id_list_list; }
  std::vector<int32_t>& get_gcell_x_box_idx_list() { return _gcell_x_box_idx_list; }
  std::vector<int32_t>& get_gcell_y_box_idx_list() { return _gcell_y_box_idx_list; }
  // setter
  void set_dr_net_list(const std::vector<DRNet>& dr_net_list) { _dr_net_list = dr_net_list; }
  void set_initial_routing(const bool initial_routing) { _initial_routing = initial_routing; }
  void set_iter(const int32_t iter) { _iter = iter; }
  void set_dr_iter_param(const DRIterParam& dr_iter_param) { _dr_iter_param = dr_iter_param; }
  void set_previous_dr_iter_param(const DRIterParam& dr_iter_param) { _previous_dr_iter_param = dr_iter_param; }
  void set_refine_enabled(bool refine_enabled) { _refine_enabled = refine_enabled; }
  void set_dr_box_map(const GridMap<DRBox>& dr_box_map) { _dr_box_map = dr_box_map; }
  void set_dr_box_id_list_list(const std::vector<std::vector<DRBoxId>>& dr_box_id_list_list) { _dr_box_id_list_list = dr_box_id_list_list; }
  void set_gcell_x_box_idx_list(const std::vector<int32_t>& gcell_x_box_idx_list) { _gcell_x_box_idx_list = gcell_x_box_idx_list; }
  void set_gcell_y_box_idx_list(const std::vector<int32_t>& gcell_y_box_idx_list) { _gcell_y_box_idx_list = gcell_y_box_idx_list; }

 private:
  DRModelResult _curr_result;
  DRModelResult _previous_result;
  DRModelResult _best_result;
  std::vector<DRNet> _dr_net_list;
  bool _initial_routing = true;
  int32_t _iter = -1;
  DRIterParam _dr_iter_param;
  DRIterParam _previous_dr_iter_param;
  bool _refine_enabled = false;
  GridMap<DRBox> _dr_box_map;
  std::vector<std::vector<DRBoxId>> _dr_box_id_list_list;
  std::vector<int32_t> _gcell_x_box_idx_list;
  std::vector<int32_t> _gcell_y_box_idx_list;
};

}  // namespace irt
