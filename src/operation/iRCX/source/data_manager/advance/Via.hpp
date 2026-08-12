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

#include "LayerShape.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class Via
{
 public:
  Via() = default;
  ~Via() = default;
  // getter
  std::string& get_via_name() { return _via_name; }
  GTLPointInt& get_point() { return _point; }
  LayerShape& get_top_layer_shape() { return _top_layer_shape; }
  LayerShape& get_cut_layer_shape() { return _cut_layer_shape; }
  LayerShape& get_bottom_layer_shape() { return _bottom_layer_shape; }
  // setter
  void set_via_name(const std::string& via_name) { _via_name = via_name; }
  void set_point(const GTLPointInt& point) { _point = point; }
  void set_top_layer_shape(const LayerShape& top_layer_shape) { _top_layer_shape = top_layer_shape; }
  void set_cut_layer_shape(const LayerShape& cut_layer_shape) { _cut_layer_shape = cut_layer_shape; }
  void set_bottom_layer_shape(const LayerShape& bottom_layer_shape) { _bottom_layer_shape = bottom_layer_shape; }
  // function

 private:
  std::string _via_name;
  GTLPointInt _point;
  LayerShape _top_layer_shape = LayerShape();
  LayerShape _cut_layer_shape = LayerShape();
  LayerShape _bottom_layer_shape = LayerShape();
};

}  // namespace ircx
