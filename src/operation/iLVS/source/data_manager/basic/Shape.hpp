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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"
namespace ilvs {

class Shape
{
 public:
  Shape() = default;
  ~Shape() = default;
  // getter
  int32_t get_layer_idx() const { return _layer_idx; }
  int32_t get_ll_x() const { return _ll_x; }
  int32_t get_ll_y() const { return _ll_y; }
  int32_t get_ur_x() const { return _ur_x; }
  int32_t get_ur_y() const { return _ur_y; }
  // setter
  void set_layer_idx(const int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_ll_x(const int32_t ll_x) { _ll_x = ll_x; }
  void set_ll_y(const int32_t ll_y) { _ll_y = ll_y; }
  void set_ur_x(const int32_t ur_x) { _ur_x = ur_x; }
  void set_ur_y(const int32_t ur_y) { _ur_y = ur_y; }

 private:
  int32_t _layer_idx = -1;
  int32_t _ll_x = 0;
  int32_t _ll_y = 0;
  int32_t _ur_x = 0;
  int32_t _ur_y = 0;
};

}  // namespace ilvs
