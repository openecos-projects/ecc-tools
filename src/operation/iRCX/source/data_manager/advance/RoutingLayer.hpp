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

#include "RCXHeader.hpp"
#include "TrackInfo.hpp"

namespace ircx {

class RoutingLayer
{
 public:
  RoutingLayer() = default;
  ~RoutingLayer() = default;
  // getter
  int32_t get_layer_idx() const { return _layer_idx; }
  std::string& get_layer_name() { return _layer_name; }
  int32_t get_layer_width() const { return _layer_width; }
  bool get_is_prefer_horizontal() const { return _is_prefer_horizontal; }
  TrackInfo& get_track_info() { return _track_info; }
  // setter
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_layer_width(int32_t layer_width) { _layer_width = layer_width; }
  void set_is_prefer_horizontal(bool is_prefer_horizontal) { _is_prefer_horizontal = is_prefer_horizontal; }
  void set_track_info(const TrackInfo& track_info) { _track_info = track_info; }
  // function

 private:
  int32_t _layer_idx = -1;
  std::string _layer_name;
  int32_t _layer_width = -1;
  bool _is_prefer_horizontal = false;
  TrackInfo _track_info;
};

}  // namespace ircx
