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

class Patch
{
 public:
  Patch() = default;
  ~Patch() = default;
  // getter
  int32_t get_layer_idx() const { return _layer_idx; }
  GTLRectInt& get_shape() { return _shape; }
  // setter
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_shape(const GTLRectInt& shape) { _shape = shape; }
  // function

 private:
  int32_t _layer_idx = -1;
  GTLRectInt _shape;
};

}  // namespace ircx
