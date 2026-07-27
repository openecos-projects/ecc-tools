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

class SupplyRouteShape
{
 public:
  SupplyRouteShape() = default;
  ~SupplyRouteShape() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  int32_t get_component_id() const { return _component_id; }
  int32_t get_layer_order() const { return _layer_order; }
  Shape& get_shape() { return _shape; }
  // const getter
  const std::string& get_net_name() const { return _net_name; }
  const Shape& get_shape() const { return _shape; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_component_id(const int32_t component_id) { _component_id = component_id; }
  void set_layer_order(const int32_t layer_order) { _layer_order = layer_order; }
  void set_shape(const Shape& shape) { _shape = shape; }

 private:
  std::string _net_name;
  int32_t _component_id = -1;
  int32_t _layer_order = -1;
  Shape _shape;
};

}  // namespace ilvs
