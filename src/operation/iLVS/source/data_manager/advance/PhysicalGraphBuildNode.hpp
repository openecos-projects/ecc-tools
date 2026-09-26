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

class PhysicalGraphBuildNode
{
 public:
  PhysicalGraphBuildNode() = default;
  ~PhysicalGraphBuildNode() = default;
  // getter
  int32_t get_net_id() const { return _net_id; }
  const Shape& get_shape() const { return *_shape; }
  bool get_is_terminal() const { return _is_terminal; }
  int32_t get_routing_shape_idx() const { return _routing_shape_idx; }
  // setter
  void set_net_id(int32_t net_id) { _net_id = net_id; }
  void set_shape(const Shape& shape) { _shape = &shape; }
  void set_is_terminal(bool is_terminal) { _is_terminal = is_terminal; }
  void set_routing_shape_idx(const int32_t routing_shape_idx) { _routing_shape_idx = routing_shape_idx; }

 private:
  int32_t _net_id = -1;
  const Shape* _shape = nullptr;
  bool _is_terminal = false;
  int32_t _routing_shape_idx = -1;
};

}  // namespace ilvs
