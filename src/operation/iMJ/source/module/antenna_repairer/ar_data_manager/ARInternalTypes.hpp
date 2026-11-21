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

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GeometryTypes.h"
#include "IdbCellMaster.h"
#include "IdbEnum.h"
#include "IdbGeometry.h"
#include "IdbLayer.h"
#include "IdbOrientTransform.h"
#include "IdbSpecialNet.h"
#include "IdbTerm.h"
#include "IdbVias.h"

namespace idb {
class IdbDesign;
}  // namespace idb

namespace imj {

inline constexpr double kAntennaRepairEps = 1e-9;

struct PGSpecialNets
{
  idb::IdbSpecialNet* default_pwr = nullptr;
  idb::IdbSpecialNet* default_gnd = nullptr;
};

PGSpecialNets findDefaultPGSpecialNets(idb::IdbDesign* design);
idb::IdbSpecialNet* resolvePGSpecialNet(idb::IdbDesign* design, idb::IdbTerm* term, const PGSpecialNets& pg_nets);

struct DiodeCandidateMaster
{
  idb::IdbCellMaster* master = nullptr;
  idb::IdbTerm* signal_term = nullptr;
  double unqualified_diff_area = 0.0;
  std::unordered_map<int, double> diff_area_by_layer_order;
  int min_diff_decl_order = std::numeric_limits<int>::max();
  int min_effective_diff_order = std::numeric_limits<int>::max();
  double max_diff_area = 0.0;
  bool is_antenna_class = false;
  bool name_hint = false;
  uint32_t width = 0;

  bool supportsLayerOrder(int max_order) const { return min_effective_diff_order <= max_order; }

  bool layerCarriesDiff(int layer_order) const
  {
    if (unqualified_diff_area > kAntennaRepairEps) {
      return true;
    }
    if (min_diff_decl_order <= layer_order) {
      return true;
    }
    auto it = diff_area_by_layer_order.find(layer_order);
    return it != diff_area_by_layer_order.end() && it->second > kAntennaRepairEps;
  }

  double effectiveDiffArea(int layer_order) const
  {
    double best_qualified = 0.0;
    int best_order = -1;
    for (const auto& [q_order, area] : diff_area_by_layer_order) {
      if (q_order <= layer_order && area > kAntennaRepairEps && q_order > best_order) {
        best_order = q_order;
        best_qualified = area;
      }
    }
    return unqualified_diff_area + best_qualified;
  }
};

struct LayerShapeBox
{
  int layer_idx = -1;
  idb::IdbRect rect;
  int32_t min_spacing = 0;
};

struct RoutingObstacleBox
{
  idb::IdbRect rect;
  int32_t min_spacing = 0;
  int32_t effective_width = 0;
  bool except_pgnet = false;
};

struct CutShapeBox
{
  int cut_order = -1;
  idb::IdbRect rect;
};

struct PGShapeBox
{
  int layer_idx = -1;
  idb::IdbRect rect;
  idb::IdbSpecialNet* snet = nullptr;
  std::string term_name;
  bool is_power = false;
  bool is_ground = false;
};

struct PGCutShapeBox
{
  int cut_order = -1;
  idb::IdbRect rect;
  idb::IdbSpecialNet* snet = nullptr;
  std::string term_name;
  bool is_power = false;
  bool is_ground = false;
};

struct CandidateDiodeGeometry
{
  int32_t inst_x = 0;
  int32_t inst_y = 0;
  idb::IdbRect cell_bbox;
  std::vector<LayerShapeBox> start_shapes;
  std::vector<LayerShapeBox> all_signal_metal_shapes;
  std::vector<CutShapeBox> all_signal_cut_shapes;
  std::vector<PGShapeBox> pg_metal_shapes;
  std::vector<PGCutShapeBox> pg_cut_shapes;
  std::vector<LayerShapeBox> obs_metal_shapes;
  std::vector<CutShapeBox> obs_cut_shapes;
  std::vector<LayerShapeBox> internal_obs;

  void clear()
  {
    inst_x = 0;
    inst_y = 0;
    cell_bbox = idb::IdbRect();
    start_shapes.clear();
    all_signal_metal_shapes.clear();
    all_signal_cut_shapes.clear();
    pg_metal_shapes.clear();
    pg_cut_shapes.clear();
    obs_metal_shapes.clear();
    obs_cut_shapes.clear();
    internal_obs.clear();
  }
};

struct EolSpacingRule
{
  int32_t spacing = 0;
  int32_t eol_width = 0;
  int32_t eol_within = 0;
  bool has_parallel_edge = false;
  int32_t par_space = 0;
  int32_t par_within = 0;
  bool two_edges = false;
  bool has_end_to_end = false;
  int32_t end_to_end_space = 0;
  bool subtract_eol_width = false;
  bool exact_width = false;
  bool has_min_length = false;
  int32_t min_length = 0;
  bool has_max_length = false;
  int32_t max_length = 0;
  int32_t enlarge = 0;
};

struct NotchSpacingRule
{
  bool valid = false;
  int32_t min_spacing = 0;
  int32_t notch_length = 0;
  bool concave_ends = false;
  int32_t concave_length = 0;
};

struct MinStepRule
{
  bool valid = false;
  int32_t min_step_length = 0;
  int32_t max_edges = -1;
  bool has_min_adj_length = false;
  int32_t min_adj_length = 0;
  bool has_length_sum = false;
  int32_t length_sum = 0;
  int type = 0;
  bool concave_corner = false;
  bool convex_corner = false;
};

struct MinEnclosedAreaRule
{
  int64_t area = 0;
  int32_t width = -1;
};

struct Lef58MinAreaRule
{
  int64_t min_area = 0;
  std::vector<std::pair<int32_t, int32_t>> except_min_sizes;
  int32_t except_min_edge_length = -1;
  int32_t except_max_edge_length = -1;
  int32_t rect_width = -1;
};

struct CutAdjacentRule
{
  int32_t adjacent_cuts = 0;
  int32_t cut_spacing = 0;
  int32_t cut_within = 0;
  bool except_same_pgnet = false;
};

struct CutArraySpacingRule
{
  int32_t cut_spacing = 0;
  int32_t array_cuts = 0;
  int32_t array_spacing = 0;
  int32_t via_width = -1;
  bool long_array = false;
  bool parallel_overlap = false;
};

struct DiffLayerCutSpacingRule
{
  int other_cut_order = -1;
  int32_t spacing = 0;
  int32_t prl = -1;
  int32_t prl_spacing = 0;
  bool stack = false;
};

struct RoutingLayerInfo
{
  idb::IdbLayerRouting* layer = nullptr;
  ecc::geometry::LayerId geom_layer_id = 0;
  int order = -1;
  int32_t width = 1;
  int32_t min_width = 1;
  int32_t max_width = 0;
  int32_t half_low = 0;
  int32_t half_high = 1;
  int32_t end_ext = 0;
  bool has_custom_wire_ext = false;
  bool routing_width_legal = true;
  int32_t min_spacing = 1;
  int32_t max_spacing = 1;
  int64_t min_area = 0;
  int32_t pitch_x = 1;
  int32_t pitch_y = 1;
  bool is_horizontal = true;
  bool is_vertical = false;
  std::vector<EolSpacingRule> eol_rules;
  NotchSpacingRule notch_rule;
  MinStepRule min_step_rule;
  std::vector<MinEnclosedAreaRule> min_enclosed_areas;
  std::vector<Lef58MinAreaRule> lef58_areas;
};

struct ViaOption
{
  idb::IdbVia* tmpl = nullptr;
  idb::IdbLayer* bottom_layer = nullptr;
  idb::IdbLayer* top_layer = nullptr;
  idb::IdbLayerCut* cut_layer = nullptr;
  ecc::geometry::LayerId cut_geom_layer_id = 0;
  int32_t cut_spacing = 0;
  int32_t same_net_cut_spacing = 0;
  std::vector<idb::IdbRect> bottom_rects;
  std::vector<idb::IdbRect> top_rects;
  std::vector<idb::IdbRect> cut_rects;
};

struct StubMove
{
  bool is_via = false;
  int layer_idx = 0;       // Wire: routing layer idx. Via: lower routing layer idx.
  int next_layer_idx = 0;  // Via: upper routing layer idx.
  int32_t x1 = 0;
  int32_t y1 = 0;
  int32_t x2 = 0;
  int32_t y2 = 0;
};

struct RoutePlan
{
  bool found = false;
  std::vector<StubMove> moves;
};

struct SiteCandidate
{
  size_t row_idx = 0;
  int32_t site_idx = 0;
  int32_t sites_needed = 1;
  int32_t inst_x = 0;
  int32_t inst_y = 0;
  idb::IdbOrient orient = idb::IdbOrient::kN_R0;
  int64_t dist = 0;
};

inline int64_t floorDiv(int64_t a, int64_t b)
{
  int64_t q = a / b;
  if ((a ^ b) < 0 && a % b != 0) {
    --q;
  }
  return q;
}

inline int64_t ceilDiv(int64_t a, int64_t b)
{
  return -floorDiv(-a, b);
}

inline bool rectClearOfObstacle(const idb::IdbRect& a, const idb::IdbRect& b, int32_t spacing)
{
  const int64_t sp = std::max<int32_t>(0, spacing);
  const bool x_too_close = static_cast<int64_t>(a.get_low_x()) < static_cast<int64_t>(b.get_high_x()) + sp &&
                           static_cast<int64_t>(a.get_high_x()) + sp > static_cast<int64_t>(b.get_low_x());
  const bool y_too_close = static_cast<int64_t>(a.get_low_y()) < static_cast<int64_t>(b.get_high_y()) + sp &&
                           static_cast<int64_t>(a.get_high_y()) + sp > static_cast<int64_t>(b.get_low_y());
  return !(x_too_close && y_too_close);
}

inline bool pointInsideRect(int32_t x, int32_t y, const idb::IdbRect& r)
{
  return x >= r.get_low_x() && x <= r.get_high_x() && y >= r.get_low_y() && y <= r.get_high_y();
}

inline idb::IdbRect shiftedRect(const idb::IdbRect& local_rect, int32_t cx, int32_t cy)
{
  return idb::IdbRect(local_rect.get_low_x() + cx, local_rect.get_low_y() + cy, local_rect.get_high_x() + cx,
                      local_rect.get_high_y() + cy);
}

inline idb::IdbOrient normalizeOrient(idb::IdbOrient orient)
{
  return orient == idb::IdbOrient::kNone ? idb::IdbOrient::kN_R0 : orient;
}

inline idb::IdbRect transformMasterRect(const idb::IdbRect& master_rect, int32_t inst_x, int32_t inst_y,
                                        idb::IdbOrient orient, uint32_t master_w, uint32_t master_h)
{
  idb::IdbRect rect(master_rect);
  rect.moveByStep(inst_x, inst_y);
  idb::IdbCoordinate<int32_t> origin(inst_x, inst_y);
  idb::IdbOrientTransform tf(normalizeOrient(orient), &origin, static_cast<int32_t>(master_w),
                             static_cast<int32_t>(master_h));
  tf.transformRect(&rect);
  return rect;
}

}  // namespace imj
