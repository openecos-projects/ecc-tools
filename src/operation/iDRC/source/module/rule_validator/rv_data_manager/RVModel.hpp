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

#include "DRCShape.hpp"
#include "RVCluster.hpp"
#include "RVComParam.hpp"

namespace idrc {

class RVModel
{
 public:
  RVModel() = default;
  ~RVModel() = default;
  // getter
  int32_t get_grid_col_num() const { return _grid_col_num; }
  int32_t get_grid_row_num() const { return _grid_row_num; }
  int32_t get_cluster_width() const { return _cluster_width; }
  int32_t get_cluster_height() const { return _cluster_height; }
  std::vector<DRCShape>& get_drc_env_shape_list() { return _drc_env_shape_list; }
  std::vector<DRCShape>& get_drc_result_shape_list() { return _drc_result_shape_list; }
  std::set<ViolationType>& get_drc_check_type_set() { return _drc_check_type_set; }
  std::vector<DRCShape>& get_drc_check_region_list() { return _drc_check_region_list; }
  RVComParam& get_rv_com_param() { return _rv_com_param; }
  std::vector<RVCluster>& get_rv_cluster_list() { return _rv_cluster_list; }
  std::vector<std::vector<int32_t>>& get_cluster_group_list() { return _rv_cluster_group_list; }
  std::vector<Violation>& get_violation_list() { return _violation_list; }
  // setter
  void set_grid_col_num(int32_t grid_col_num) { _grid_col_num = grid_col_num; }
  void set_grid_row_num(int32_t grid_row_num) { _grid_row_num = grid_row_num; }
  void set_cluster_width(int32_t cluster_width) { _cluster_width = cluster_width; }
  void set_cluster_height(int32_t cluster_height) { _cluster_height = cluster_height; }
  void set_drc_env_shape_list(const std::vector<DRCShape>& drc_env_shape_list) { _drc_env_shape_list = drc_env_shape_list; }
  void set_drc_result_shape_list(const std::vector<DRCShape>& drc_result_shape_list) { _drc_result_shape_list = drc_result_shape_list; }
  void set_drc_check_type_set(const std::set<ViolationType>& drc_check_type_set) { _drc_check_type_set = drc_check_type_set; }
  void set_drc_check_region_list(const std::vector<DRCShape>& drc_check_region_list) { _drc_check_region_list = drc_check_region_list; }
  void set_rv_com_param(const RVComParam& rv_com_param) { _rv_com_param = rv_com_param; }
  void set_rv_cluster_list(const std::vector<RVCluster>& rv_cluster_list) { _rv_cluster_list = rv_cluster_list; }
  void set_rv_cluster_group_list(const std::vector<std::vector<int32_t>>& rv_cluster_group_list) { _rv_cluster_group_list = rv_cluster_group_list; }
  void set_violation_list(const std::vector<Violation>& violation_list) { _violation_list = violation_list; }

 private:
  int32_t _grid_col_num = 0;
  int32_t _grid_row_num = 0;
  int32_t _cluster_width = 0;
  int32_t _cluster_height = 0;
  std::vector<DRCShape> _drc_env_shape_list;
  std::vector<DRCShape> _drc_result_shape_list;
  std::set<ViolationType> _drc_check_type_set;
  std::vector<DRCShape> _drc_check_region_list;
  RVComParam _rv_com_param;
  std::vector<RVCluster> _rv_cluster_list;
  std::vector<std::vector<int32_t>> _rv_cluster_group_list;
  std::vector<Violation> _violation_list;
};

}  // namespace idrc
