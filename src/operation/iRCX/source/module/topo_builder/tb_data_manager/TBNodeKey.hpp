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

#include "RCXHeader.hpp"

namespace ircx {

class TBNodeKey
{
 public:
  TBNodeKey() = default;
  TBNodeKey(int32_t layer_idx, int32_t x_coord, int32_t y_coord) : _layer_idx(layer_idx), _x_coord(x_coord), _y_coord(y_coord) {}
  ~TBNodeKey() = default;
  // getter
  int32_t get_layer_idx() const { return _layer_idx; }
  int32_t get_x_coord() const { return _x_coord; }
  int32_t get_y_coord() const { return _y_coord; }
  // setter
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_x_coord(int32_t x_coord) { _x_coord = x_coord; }
  void set_y_coord(int32_t y_coord) { _y_coord = y_coord; }
  // function
  bool operator<(const TBNodeKey& other) const
  {
    if (_layer_idx != other._layer_idx) {
      return _layer_idx < other._layer_idx;
    }
    if (_x_coord != other._x_coord) {
      return _x_coord < other._x_coord;
    }
    return _y_coord < other._y_coord;
  }

 private:
  int32_t _layer_idx = -1;
  int32_t _x_coord = -1;
  int32_t _y_coord = -1;
};

}  // namespace ircx
