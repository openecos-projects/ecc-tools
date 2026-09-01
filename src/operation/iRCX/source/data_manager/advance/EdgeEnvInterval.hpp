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

#include "CrossLayerOverlap.hpp"
#include "TopoEdge.hpp"

namespace ircx {

class EdgeEnvInterval
{
 public:
  EdgeEnvInterval() = default;
  ~EdgeEnvInterval() = default;
  // getter
  int32_t get_start_coord() const { return _start_coord; }
  int32_t get_end_coord() const { return _end_coord; }
  int32_t get_lower_spacing() const { return _lower_spacing; }
  int32_t get_upper_spacing() const { return _upper_spacing; }
  TopoEdge* get_lower_adjacent_edge() const { return _lower_adjacent_edge; }
  TopoEdge* get_upper_adjacent_edge() const { return _upper_adjacent_edge; }
  std::vector<CrossLayerOverlap>& get_cross_layer_overlap_list() { return _cross_layer_overlap_list; }
  // setter
  void set_start_coord(int32_t start_coord) { _start_coord = start_coord; }
  void set_end_coord(int32_t end_coord) { _end_coord = end_coord; }
  void set_lower_spacing(int32_t lower_spacing) { _lower_spacing = lower_spacing; }
  void set_upper_spacing(int32_t upper_spacing) { _upper_spacing = upper_spacing; }
  void set_lower_adjacent_edge(TopoEdge* lower_adjacent_edge) { _lower_adjacent_edge = lower_adjacent_edge; }
  void set_upper_adjacent_edge(TopoEdge* upper_adjacent_edge) { _upper_adjacent_edge = upper_adjacent_edge; }
  void set_cross_layer_overlap_list(const std::vector<CrossLayerOverlap>& cross_layer_overlap_list) { _cross_layer_overlap_list = cross_layer_overlap_list; }
  // function

 private:
  int32_t _start_coord = -1;
  int32_t _end_coord = -1;
  int32_t _lower_spacing = -1;
  int32_t _upper_spacing = -1;
  TopoEdge* _lower_adjacent_edge = nullptr;
  TopoEdge* _upper_adjacent_edge = nullptr;
  std::vector<CrossLayerOverlap> _cross_layer_overlap_list;
};

}  // namespace ircx
