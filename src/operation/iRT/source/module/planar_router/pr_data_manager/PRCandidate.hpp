// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <boost/container/small_vector.hpp>

#include "PlanarCoord.hpp"
#include "RTHeader.hpp"
#include "Segment.hpp"

namespace irt {

struct PRCandidateCost
{
  int32_t total_corner_num = 0;
  int32_t total_wire_length = 0;
  bool is_path_blocked = false;
  bool is_overflow = false;
  double total_cost = 0.0;
  int32_t saturation_edge_num = 0;
  int32_t hotspot_edge_num = 0;
};

class PRCandidate
{
 public:
  using RoutingSegmentList = boost::container::small_vector<Segment<PlanarCoord>, 4>;

  PRCandidate() = default;
  explicit PRCandidate(RoutingSegmentList&& routing_segment_list) : _routing_segment_list(std::move(routing_segment_list)) {}
  ~PRCandidate() = default;
  // getter
  RoutingSegmentList& get_routing_segment_list() { return _routing_segment_list; }
  int32_t get_total_corner_num() const { return _total_corner_num; }
  int32_t get_total_wire_length() const { return _total_wire_length; }
  bool get_is_path_blocked() const { return _is_path_blocked; }
  bool get_is_overflow() const { return _is_overflow; }
  double get_total_cost() const { return _total_cost; }
  int32_t get_saturation_edge_num() const { return _saturation_edge_num; }
  int32_t get_hotspot_edge_num() const { return _hotspot_edge_num; }
  // setter
  void set_candidate_cost(const PRCandidateCost& candidate_cost)
  {
    _total_corner_num = candidate_cost.total_corner_num;
    _total_wire_length = candidate_cost.total_wire_length;
    _is_path_blocked = candidate_cost.is_path_blocked;
    _is_overflow = candidate_cost.is_overflow;
    _total_cost = candidate_cost.total_cost;
    _saturation_edge_num = candidate_cost.saturation_edge_num;
    _hotspot_edge_num = candidate_cost.hotspot_edge_num;
  }

 private:
  RoutingSegmentList _routing_segment_list;
  int32_t _total_corner_num = 0;
  int32_t _total_wire_length = 0;
  bool _is_path_blocked = false;
  bool _is_overflow = false;
  double _total_cost = 0.0;
  int32_t _saturation_edge_num = 0;
  int32_t _hotspot_edge_num = 0;
};

}  // namespace irt
