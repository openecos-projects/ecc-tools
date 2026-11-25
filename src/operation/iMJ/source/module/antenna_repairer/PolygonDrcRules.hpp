// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <boost/polygon/polygon.hpp>
#include <cstdint>
#include <functional>
#include <vector>

#include "ARInternalTypes.hpp"
#include "IdbGeometry.h"

namespace imj {

struct DrcPoint
{
  int32_t x = 0;
  int32_t y = 0;
};

struct DrcEdge
{
  int32_t x1 = 0;
  int32_t y1 = 0;
  int32_t x2 = 0;
  int32_t y2 = 0;
  int8_t nx = 0;
  int8_t ny = 0;
  int64_t len = 0;
  bool convex_start = false;
  bool convex_end = false;
};

struct DrcRing
{
  bool is_hole = false;
  std::vector<DrcPoint> vertices;
  std::vector<DrcEdge> edges;
};

struct DrcPolygon
{
  int64_t area = 0;
  idb::IdbRect bbox;
  std::vector<DrcRing> rings;
  std::vector<idb::IdbRect> max_rects;
};

std::vector<DrcPolygon> extractPolygonRings(const boost::polygon::polygon_90_set_data<int64_t>& poly_set);

bool checkLef58MinArea(const DrcPolygon& poly, int64_t classic_min_area,
                       const std::vector<Lef58MinAreaRule>& lef58_areas, int64_t* out_deficit = nullptr);

bool checkEndOfLineSpacing(const std::vector<DrcPolygon>& existing_polys, const std::vector<DrcPolygon>& merged_polys,
                           const boost::polygon::polygon_90_set_data<int64_t>& merged_foreign_obs,
                           const std::vector<idb::IdbRect>& new_shapes, const std::vector<EolSpacingRule>& eol_rules);

bool checkMergedNetPolygonDrc(const boost::polygon::polygon_90_set_data<int64_t>& existing_set,
                              const boost::polygon::polygon_90_set_data<int64_t>& merged_set,
                              const std::vector<idb::IdbRect>& new_shapes, const RoutingLayerInfo& layer_info,
                              const std::function<int32_t(const idb::IdbRect&, const idb::IdbRect&)>& spacing_fn,
                              std::vector<idb::IdbRect>* out_same_net_conflicts = nullptr);

}  // namespace imj
