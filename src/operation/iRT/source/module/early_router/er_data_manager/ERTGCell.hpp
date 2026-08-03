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

#include "Orientation.hpp"
#include "RTHeader.hpp"

namespace irt {

class ERTGCell
{
 public:
  ERTGCell() = default;
  ~ERTGCell() = default;
  // getter
  double get_boundary_wire_unit() const { return _boundary_wire_unit; }
  double get_internal_wire_unit() const { return _internal_wire_unit; }
  double get_internal_via_unit() const { return _internal_via_unit; }
  std::map<int32_t, std::map<Orientation, int32_t>>& get_routing_orient_supply_map() { return _routing_orient_supply_map; }
  std::map<int32_t, std::map<int32_t, std::set<Orientation>>>& get_routing_ignore_net_orient_map() { return _routing_ignore_net_orient_map; }
  // setter
  void set_boundary_wire_unit(const double boundary_wire_unit) { _boundary_wire_unit = boundary_wire_unit; }
  void set_internal_wire_unit(const double internal_wire_unit) { _internal_wire_unit = internal_wire_unit; }
  void set_internal_via_unit(const double internal_via_unit) { _internal_via_unit = internal_via_unit; }
  void set_routing_ignore_net_orient_map(const std::map<int32_t, std::map<int32_t, std::set<Orientation>>>& routing_ignore_net_orient_map)
  {
    _routing_ignore_net_orient_map = routing_ignore_net_orient_map;
  }

 private:
  double _boundary_wire_unit = -1;
  double _internal_wire_unit = -1;
  double _internal_via_unit = -1;
  std::map<int32_t, std::map<Orientation, int32_t>> _routing_orient_supply_map;
  std::map<int32_t, std::map<int32_t, std::set<Orientation>>> _routing_ignore_net_orient_map;
};

}  // namespace irt
