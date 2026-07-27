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

#include "EnvPixelGrid.hpp"
#include "EnvTrackIdx.hpp"
#include "NetEnv.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class EBModel
{
 public:
  EBModel() = default;
  ~EBModel() = default;

  // getter
  double get_bucket_size_um() const { return _bucket_size_um; }
  int32_t get_cross_layer_num() const { return _cross_layer_num; }
  std::map<int32_t, EnvPixelGrid>& get_layer_to_prefer_pixel_grid_map() { return _layer_to_prefer_pixel_grid_map; }
  std::map<int32_t, EnvPixelGrid>& get_layer_to_nonprefer_pixel_grid_map() { return _layer_to_nonprefer_pixel_grid_map; }
  std::map<int32_t, EnvTrackIdx>& get_layer_to_prefer_track_idx_map() { return _layer_to_prefer_track_idx_map; }
  std::map<int32_t, EnvTrackIdx>& get_layer_to_nonprefer_track_idx_map() { return _layer_to_nonprefer_track_idx_map; }
  std::map<int32_t, int32_t>& get_layer_to_search_track_num_map() { return _layer_to_search_track_num_map; }
  // setter
  void set_bucket_size_um(double bucket_size_um) { _bucket_size_um = bucket_size_um; }
  void set_cross_layer_num(int32_t cross_layer_num) { _cross_layer_num = cross_layer_num; }
  // function

  EBModel(const EBModel&) = delete;
  EBModel(EBModel&&) = default;
  EBModel& operator=(const EBModel&) = delete;
  EBModel& operator=(EBModel&&) = default;

 private:
  double _bucket_size_um = -1.0;
  int32_t _cross_layer_num = -1;

  std::map<int32_t, EnvPixelGrid> _layer_to_prefer_pixel_grid_map;
  std::map<int32_t, EnvPixelGrid> _layer_to_nonprefer_pixel_grid_map;
  std::map<int32_t, EnvTrackIdx> _layer_to_prefer_track_idx_map;
  std::map<int32_t, EnvTrackIdx> _layer_to_nonprefer_track_idx_map;
  std::map<int32_t, int32_t> _layer_to_search_track_num_map;
};

}  // namespace ircx
