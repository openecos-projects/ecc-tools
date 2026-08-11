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

#include <functional>

#include "PlanarCoord.hpp"
#include "PlanarRect.hpp"
#include "RTHeader.hpp"

namespace irt {

using TBSegmentCostQuery = std::function<double(const PlanarCoord&, const PlanarCoord&)>;

class TBTask
{
 public:
  TBTask() = default;
  ~TBTask() = default;
  // getter
  std::vector<PlanarCoord>& get_planar_coord_list() { return _planar_coord_list; }
  const std::vector<PlanarCoord>& get_planar_coord_list() const { return _planar_coord_list; }
  const PlanarRect& get_planar_search_region() const { return _planar_search_region; }
  bool has_planar_search_region() const { return _has_planar_search_region; }
  bool has_segment_cost_query() const { return static_cast<bool>(_segment_cost_query); }
  bool is_congestion_driven() const { return _is_congestion_driven; }
  // setter
  void set_planar_coord_list(std::vector<PlanarCoord> planar_coord_list) { _planar_coord_list = std::move(planar_coord_list); }
  void set_segment_cost_query(TBSegmentCostQuery query) { _segment_cost_query = std::move(query); }
  void set_congestion_driven(bool congestion_driven) { _is_congestion_driven = congestion_driven; }
  void set_planar_search_region(const PlanarRect& planar_search_region)
  {
    _planar_search_region = planar_search_region;
    _has_planar_search_region = true;
  }
  // function
  double get_segment_cost(const PlanarCoord& first, const PlanarCoord& second) const { return _segment_cost_query(first, second); }

 private:
  std::vector<PlanarCoord> _planar_coord_list;
  TBSegmentCostQuery _segment_cost_query;
  PlanarRect _planar_search_region;
  bool _has_planar_search_region = false;
  bool _is_congestion_driven = false;
};

}  // namespace irt
