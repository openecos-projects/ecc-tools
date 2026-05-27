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

#include <span>
#include <utility>
#include <vector>

#include "IntervalPool.hh"
#include "Types.hh"
namespace ircx {

struct EtchInterval {
  Micron a0 = 0; // interval start
  Micron a1 = 0; // interval end
  // change with etch
  Micron center;
  Micron width;

  Micron lo_spacing;
  Micron hi_spacing;

  Micron thickness;
  Micron height;
};

class EtchPool { // of each net
 public:
  EtchPool() = default;
  ~EtchPool() = default;

  void append_edge_etch_interval_pool(std::vector<EtchInterval> v) {
    intervals_.append_edge_intervals(std::move(v));
  }

  void clear() {
    intervals_.clear();
  }

  std::span<const EtchInterval> edge_etch_interval_pool(Size edge_id) const {
    return intervals_.edge_intervals(edge_id);
  }
  std::span<EtchInterval> edge_etch_interval_pool(Size edge_id) {
    return intervals_.edge_intervals(edge_id);
  }

 private:
  IntervalPool<EtchInterval> intervals_;
};
}
