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
#include "Shape.hpp"

namespace ilvs {

class RoutingShape
{
 public:
  RoutingShape() = default;
  ~RoutingShape() = default;
  // getter
  Shape& get_shape() { return _shape; }
  int32_t get_layer_order() const { return _layer_order; }
  // const getter
  const Shape& get_shape() const { return _shape; }
  // setter
  void set_shape(const Shape& shape) { _shape = shape; }
  void set_layer_order(const int32_t layer_order) { _layer_order = layer_order; }

 private:
  Shape _shape;
  int32_t _layer_order = -1;
};

}  // namespace ilvs
