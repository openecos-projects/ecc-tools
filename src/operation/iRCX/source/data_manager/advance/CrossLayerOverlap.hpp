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

namespace ircx {

class CrossLayerOverlap
{
 public:
  CrossLayerOverlap() = default;
  ~CrossLayerOverlap() = default;
  // getter
  int32_t get_start_coord() const { return _start_coord; }
  int32_t get_end_coord() const { return _end_coord; }
  int32_t get_above_layer_idx() const { return _above_layer_idx; }
  int32_t get_below_layer_idx() const { return _below_layer_idx; }
  // setter
  void set_start_coord(int32_t start_coord) { _start_coord = start_coord; }
  void set_end_coord(int32_t end_coord) { _end_coord = end_coord; }
  void set_above_layer_idx(int32_t above_layer_idx) { _above_layer_idx = above_layer_idx; }
  void set_below_layer_idx(int32_t below_layer_idx) { _below_layer_idx = below_layer_idx; }
  // function

 private:
  int32_t _start_coord = -1;
  int32_t _end_coord = -1;
  int32_t _above_layer_idx = -1;
  int32_t _below_layer_idx = -1;
};

}  // namespace ircx
