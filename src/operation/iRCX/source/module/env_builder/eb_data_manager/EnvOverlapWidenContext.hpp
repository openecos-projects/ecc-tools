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
#include "TopoEdge.hpp"

namespace ircx {

class EnvOverlapWidenContext
{
 public:
  EnvOverlapWidenContext(int32_t track_distance, int32_t overlap_length, TopoEdge& edge)
      : _track_distance(track_distance), _overlap_length(overlap_length), _edge(edge)
  {
  }
  ~EnvOverlapWidenContext() = default;
  // getter
  int32_t get_track_distance() const { return _track_distance; }
  int32_t get_overlap_length() const { return _overlap_length; }
  TopoEdge& get_edge() const { return _edge; }
  // setter
  // function

 private:
  int32_t _track_distance;
  int32_t _overlap_length;
  TopoEdge& _edge;
};

}  // namespace ircx
