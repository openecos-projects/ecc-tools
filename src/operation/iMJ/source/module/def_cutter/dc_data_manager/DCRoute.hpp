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

#include "DCLayerRect.hpp"

namespace imj {

class DCRoute
{
 public:
  DCRoute() = default;
  ~DCRoute() = default;
  // getter
  int32_t get_wire_idx() { return _wire_idx; }
  int32_t get_segment_idx() { return _segment_idx; }
  int32_t get_via_idx() { return _via_idx; }
  std::string& get_layer_name() { return _layer_name; }
  std::vector<DCLayerRect>& get_via_shape_list() { return _via_shape_list; }
  DCRect& get_rect() { return _rect; }
  bool get_retained() { return _retained; }
  // const getter
  int32_t get_wire_idx() const { return _wire_idx; }
  int32_t get_segment_idx() const { return _segment_idx; }
  int32_t get_via_idx() const { return _via_idx; }
  const std::string& get_layer_name() const { return _layer_name; }
  const std::vector<DCLayerRect>& get_via_shape_list() const { return _via_shape_list; }
  const DCRect& get_rect() const { return _rect; }
  bool get_retained() const { return _retained; }
  // setter
  void set_wire_idx(int32_t wire_idx) { _wire_idx = wire_idx; }
  void set_segment_idx(int32_t segment_idx) { _segment_idx = segment_idx; }
  void set_via_idx(int32_t via_idx) { _via_idx = via_idx; }
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_via_shape_list(const std::vector<DCLayerRect>& via_shape_list) { _via_shape_list = via_shape_list; }
  void set_rect(const DCRect& rect) { _rect = rect; }
  void set_retained(bool retained) { _retained = retained; }
  // function

 private:
  int32_t _wire_idx = -1;
  int32_t _segment_idx = -1;
  int32_t _via_idx = -1;
  std::string _layer_name;
  std::vector<DCLayerRect> _via_shape_list;
  DCRect _rect;
  bool _retained = false;
};

}  // namespace imj
