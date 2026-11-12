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

#include "DCIOSlot.hpp"

namespace imj {

class DCIOModel
{
 public:
  using SlotRTree = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>;

  DCIOModel() = default;
  ~DCIOModel() = default;
  // getter
  std::vector<DCIOSlot>& get_slot_list() { return _slot_list; }
  std::map<std::string, SlotRTree>& get_layer_slot_rtree_map() { return _layer_slot_rtree_map; }
  std::map<std::string, int32_t>& get_layer_max_spacing_map() { return _layer_max_spacing_map; }
  // const getter
  const std::vector<DCIOSlot>& get_slot_list() const { return _slot_list; }
  const std::map<std::string, SlotRTree>& get_layer_slot_rtree_map() const { return _layer_slot_rtree_map; }
  const std::map<std::string, int32_t>& get_layer_max_spacing_map() const { return _layer_max_spacing_map; }
  // setter
  void set_slot_list(const std::vector<DCIOSlot>& slot_list) { _slot_list = slot_list; }
  // function

 private:
  std::vector<DCIOSlot> _slot_list;
  std::map<std::string, SlotRTree> _layer_slot_rtree_map;
  std::map<std::string, int32_t> _layer_max_spacing_map;
};

}  // namespace imj
