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
#include "LocalStubRouter.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_set>

#include <boost/polygon/polygon.hpp>

#include "GeometryBuilder.h"
#include "IdbBlockages.h"
#include "IdbCutLayerLef58Property.h"
#include "IdbDie.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbMacroProperty.h"
#include "IdbMaxViaStack.h"
#include "IdbPins.h"
#include "IdbRoutingLayerLef58Property.h"
#include "IdbViaMaster.h"
#include "PolygonDrcRules.hpp"
#include "StubMazeSearch.hpp"

namespace imj {
namespace {

constexpr ecc::geometry::OwnerId kDerivedOwnerPayloadMask = 0x00ffffffffffffffULL;

bool rectsTouchOrOverlap(const idb::IdbRect& a, const idb::IdbRect& b)
{
  return a.get_low_x() <= b.get_high_x() && a.get_high_x() >= b.get_low_x() && a.get_low_y() <= b.get_high_y() &&
         a.get_high_y() >= b.get_low_y();
}

bool rectsHavePositiveOverlap(const idb::IdbRect& a, const idb::IdbRect& b)
{
  return a.get_low_x() < b.get_high_x() && a.get_high_x() > b.get_low_x() && a.get_low_y() < b.get_high_y() &&
         a.get_high_y() > b.get_low_y();
}

bool rectContainedIn(const idb::IdbRect& inner, const idb::IdbRect& outer)
{
  return inner.get_low_x() >= outer.get_low_x() && inner.get_high_x() <= outer.get_high_x() &&
         inner.get_low_y() >= outer.get_low_y() && inner.get_high_y() <= outer.get_high_y();
}

bool isPinOwnerType(ecc::geometry::OwnerType type)
{
  return type == ecc::geometry::OwnerType::kPinPortShape ||
         type == ecc::geometry::OwnerType::kInstancePinPortShape ||
         type == ecc::geometry::OwnerType::kIoPinPortShape;
}

std::vector<idb::IdbRect> mergeMaximalRects(const std::vector<idb::IdbRect>& rects)
{
  std::vector<idb::IdbRect> merged;
  if (rects.empty()) {
    return merged;
  }
  if (rects.size() == 1) {
    if (rects[0].get_low_x() < rects[0].get_high_x() && rects[0].get_low_y() < rects[0].get_high_y()) {
      merged.push_back(rects[0]);
    }
    return merged;
  }

  namespace gtl = boost::polygon;
  using GtlRect = gtl::rectangle_data<int32_t>;
  gtl::polygon_90_set_data<int32_t> polyset;
  for (const idb::IdbRect& r : rects) {
    const int32_t lx = std::min(r.get_low_x(), r.get_high_x());
    const int32_t ly = std::min(r.get_low_y(), r.get_high_y());
    const int32_t hx = std::max(r.get_low_x(), r.get_high_x());
    const int32_t hy = std::max(r.get_low_y(), r.get_high_y());
    if (lx < hx && ly < hy) {
      polyset.insert(GtlRect(lx, ly, hx, hy));
    }
  }
  std::vector<GtlRect> max_rects;
  gtl::get_max_rectangles(max_rects, polyset);
  merged.reserve(max_rects.size());
  for (const GtlRect& gr : max_rects) {
    merged.emplace_back(gtl::xl(gr), gtl::yl(gr), gtl::xh(gr), gtl::yh(gr));
  }
  return merged;
}

ecc::geometry::OwnerId makeDerivedOwnerId(ecc::geometry::OwnerType parent_type, ecc::geometry::OwnerId parent_id)
{
  return (static_cast<ecc::geometry::OwnerId>(static_cast<uint8_t>(parent_type)) << 56U) |
         (parent_id & kDerivedOwnerPayloadMask);
}

bool isPhysicalObstacleOwner(ecc::geometry::OwnerType type)
{
  switch (type) {
    case ecc::geometry::OwnerType::kNetWireSegment:
    case ecc::geometry::OwnerType::kSpecialWireSegment:
    case ecc::geometry::OwnerType::kVia:
    case ecc::geometry::OwnerType::kPinPortShape:
    case ecc::geometry::OwnerType::kInstancePinPortShape:
    case ecc::geometry::OwnerType::kIoPinPortShape:
    case ecc::geometry::OwnerType::kObs:
    case ecc::geometry::OwnerType::kBlockage:
    case ecc::geometry::OwnerType::kFill:
      return true;
    default:
      return false;
  }
}

idb::IdbRect idbRectFromRect32(ecc::geometry::Rect32 r)
{
  r = ecc::geometry::normalize(r);
  return idb::IdbRect(r.lx, r.ly, r.hx, r.hy);
}

ecc::geometry::Rect32 rect32FromIdb(const idb::IdbRect& r)
{
  return ecc::geometry::normalize(
      ecc::geometry::Rect32{r.get_low_x(), r.get_low_y(), r.get_high_x(), r.get_high_y()});
}

int64_t rectEuclidDistSq(const idb::IdbRect& a, const idb::IdbRect& b)
{
  int64_t dx = 0;
  if (a.get_high_x() < b.get_low_x()) {
    dx = static_cast<int64_t>(b.get_low_x()) - a.get_high_x();
  } else if (b.get_high_x() < a.get_low_x()) {
    dx = static_cast<int64_t>(a.get_low_x()) - b.get_high_x();
  }
  int64_t dy = 0;
  if (a.get_high_y() < b.get_low_y()) {
    dy = static_cast<int64_t>(b.get_low_y()) - a.get_high_y();
  } else if (b.get_high_y() < a.get_low_y()) {
    dy = static_cast<int64_t>(a.get_low_y()) - b.get_high_y();
  }
  return dx * dx + dy * dy;
}

bool euclidDistLessThan(const idb::IdbRect& a, const idb::IdbRect& b, int32_t dist)
{
  if (dist <= 0) {
    return false;
  }
  const int64_t d64 = dist;
  return rectEuclidDistSq(a, b) < d64 * d64;
}

bool euclidDistAtMost(const idb::IdbRect& a, const idb::IdbRect& b, int32_t dist)
{
  if (dist < 0) {
    return false;
  }
  const int64_t d64 = dist;
  return rectEuclidDistSq(a, b) <= d64 * d64;
}

int32_t cutParallelLength(const idb::IdbRect& a, const idb::IdbRect& b)
{
  const int64_t ox = static_cast<int64_t>(std::min(a.get_high_x(), b.get_high_x())) -
                     std::max(a.get_low_x(), b.get_low_x());
  const int64_t oy = static_cast<int64_t>(std::min(a.get_high_y(), b.get_high_y())) -
                     std::max(a.get_low_y(), b.get_low_y());
  return static_cast<int32_t>(
      std::clamp<int64_t>(std::max(ox, oy), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

using CutRectKey = std::tuple<int32_t, int32_t, int32_t, int32_t>;

CutRectKey makeCutRectKey(const idb::IdbRect& r)
{
  return {std::min(r.get_low_x(), r.get_high_x()), std::min(r.get_low_y(), r.get_high_y()),
          std::max(r.get_low_x(), r.get_high_x()), std::max(r.get_low_y(), r.get_high_y())};
}

std::pair<int32_t, int32_t> resolveCutSpacings(idb::IdbLayerCut* cut_layer, int32_t meta_fallback)
{
  const int32_t fallback = std::max<int32_t>(0, meta_fallback);
  if (!cut_layer) {
    return {fallback, fallback};
  }
  int32_t max_non_samenet = 0;
  int32_t max_samenet = 0;
  for (idb::IdbLayerCutSpacing* sp : cut_layer->get_spacings()) {
    if (!sp || sp->get_spacing() <= 0 || sp->get_adjacent_cuts().has_value()) {
      continue;
    }
    if (sp->get_has_same_net()) {
      max_samenet = std::max(max_samenet, sp->get_spacing());
    } else {
      max_non_samenet = std::max(max_non_samenet, sp->get_spacing());
    }
  }
  const int32_t diff_net = max_non_samenet > 0 ? max_non_samenet : (max_samenet > 0 ? max_samenet : fallback);
  const int32_t same_net = max_samenet > 0 ? max_samenet : diff_net;
  return {diff_net, same_net};
}

void populateCutSpacingRules(idb::IdbLayerCut* cut_layer, idb::IdbLayers* layers, int32_t meta_fallback,
                             LocalStubRouter::CutSpacingRules& rules)
{
  const auto [diff_sp, same_sp] = resolveCutSpacings(cut_layer, meta_fallback);
  rules.diff_net = std::max(rules.diff_net, diff_sp);
  rules.same_net = std::max(rules.same_net, same_sp);
  rules.max_spacing = std::max({rules.max_spacing, rules.diff_net, rules.same_net});
  if (!cut_layer) {
    return;
  }

  for (idb::IdbLayerCutSpacing* sp : cut_layer->get_spacings()) {
    if (!sp || sp->get_spacing() <= 0) {
      continue;
    }
    if (const auto adj = sp->get_adjacent_cuts(); adj.has_value()) {
      CutAdjacentRule ar;
      ar.adjacent_cuts = adj->get_adjacent_cuts();
      ar.cut_spacing = sp->get_spacing();
      ar.cut_within = adj->get_cut_within();
      if (ar.adjacent_cuts > 0 && ar.cut_spacing > 0 && ar.cut_within > 0) {
        rules.adjacent_rules.push_back(ar);
        rules.max_spacing = std::max({rules.max_spacing, ar.cut_spacing, ar.cut_within});
      }
    }
  }

  if (idb::IdbLayerCutArraySpacing* as = cut_layer->get_array_spacing()) {
    const int32_t c_sp = std::max<int32_t>(0, as->get_cut_spacing());
    for (const idb::IdbArrayCut& ac : as->get_array_cut_list()) {
      if (ac._array_cut >= 2 && ac._array_spacing > 0) {
        CutArraySpacingRule cr;
        cr.cut_spacing = c_sp;
        cr.array_cuts = ac._array_cut;
        cr.array_spacing = ac._array_spacing;
        cr.long_array = as->is_long_array();
        rules.array_rules.push_back(cr);
        rules.max_spacing = std::max({rules.max_spacing, cr.cut_spacing, cr.array_spacing});
      }
    }
  }

  for (const auto& tbl : cut_layer->get_lef58_spacing_table()) {
    if (!tbl) {
      continue;
    }
    const auto& cc = tbl->get_cutclass();
    if (cc.get_class_name1_list().empty() || cc.get_class_name2_list().empty()) {
      continue;
    }
    int32_t sp1 = 0;
    int32_t sp2 = 0;
    const int n1 = static_cast<int>(cc.get_class_name1_list().size());
    const int n2 = static_cast<int>(cc.get_class_name2_list().size());
    for (int i1 = 0; i1 < n1; ++i1) {
      for (int i2 = 0; i2 < n2; ++i2) {
        const auto entry = cc.get_cut_spacing(i1, i2);
        if (entry.get_cut_spacing1().has_value()) {
          sp1 = std::max(sp1, *entry.get_cut_spacing1());
        }
        if (entry.get_cut_spacing2().has_value()) {
          sp2 = std::max(sp2, *entry.get_cut_spacing2());
        }
      }
    }
    const bool has_prl = tbl->get_prl().has_value();
    const int32_t prl_val = has_prl ? tbl->get_prl()->get_prl() : std::numeric_limits<int32_t>::max();
    if (tbl->get_second_layer().has_value()) {
      if (layers != nullptr) {
        if (idb::IdbLayer* other_l = layers->find_layer(tbl->get_second_layer()->get_second_layer_name());
            other_l != nullptr && other_l->is_cut()) {
          DiffLayerCutSpacingRule dr;
          dr.other_cut_order = static_cast<int>(other_l->get_order());
          dr.spacing = sp1;
          dr.prl = prl_val;
          dr.prl_spacing = sp2 > 0 ? sp2 : sp1;
          dr.stack = true;
          rules.diff_layer_rules.push_back(dr);
          rules.max_spacing = std::max({rules.max_spacing, dr.spacing, dr.prl_spacing});
        }
      }
    } else {
      if (sp1 > 0) {
        rules.diff_net = std::max(rules.diff_net, sp1);
      }
      if (has_prl) {
        rules.prl = prl_val;
        rules.prl_spacing = std::max(rules.prl_spacing, sp2 > 0 ? sp2 : sp1);
      } else if (sp2 > 0) {
        rules.diff_net = std::max(rules.diff_net, sp2);
      }
      rules.max_spacing = std::max({rules.max_spacing, rules.diff_net, rules.prl_spacing});
    }
  }
}

bool isCoordOnGrid(int32_t coord, int32_t grid)
{
  if (grid <= 1) {
    return true;
  }
  const int64_t g = grid;
  const int64_t rem = static_cast<int64_t>(coord) % g;
  return rem == 0;
}

}  // namespace

bool LocalStubRouter::init(idb::IdbDesign* design)
{
  _design = design;
  _store.clear();
  _has_die = false;
  _die_is_polygon = false;
  _mfg_grid = 1;
  _die_bbox = idb::IdbRect();
  _die_polygon.clear();
  _layers.clear();
  _order_to_idx.clear();
  _order_to_geom_layer_id.clear();
  _order_to_cut_spacing.clear();
  _cut_orders_sorted.clear();
  _max_via_stack = MaxViaStackRule{};
  _vias_by_low_idx.clear();
  _blockage_rules.clear();
  _obs_layer_spacing.clear();
  _committed_obs_spacing.clear();
  _owner_to_net.clear();
  _net_owners.clear();
  _net_owner_id.clear();
  _merged_net_cache.clear();
  _owner_to_special_net.clear();
  _special_net_owner_id.clear();
  _owner_pg_tag.clear();
  _next_commit_path = 1;

  if (!_design || !_design->get_layout() || !_design->get_layout()->get_layers()) {
    return false;
  }
  idb::IdbLayout* layout = _design->get_layout();
  _mfg_grid = std::max<int32_t>(1, layout->get_munufacture_grid());

  ecc::geometry::GeometryBuilder builder;
  builder.rebuild_from_design(*_design, *layout, _store);
  const std::vector<ecc::geometry::GeometryLayerMetadata> layer_meta = builder.collect_layer_metadata(*layout);

  initLayersAndMetadata(layout, layer_meta);
  if (_layers.empty()) {
    return false;
  }
  initDieGeometry(layout);
  buildVias(_design);
  buildOwnerMap(_design);
  for (int l = 0; l < static_cast<int>(_layers.size()); ++l) {
    for (const auto& [okey, sp] : _obs_layer_spacing) {
      if (okey.second == l) {
        _layers[l].max_spacing = std::max(_layers[l].max_spacing, sp);
      }
    }
    for (const auto& [bid, rule] : _blockage_rules) {
      (void)bid;
      if (rule.is_slots_or_fills) {
        continue;
      }
      _layers[l].max_spacing = std::max(_layers[l].max_spacing, rule.min_spacing);
      if (rule.effective_width > 0) {
        const idb::IdbRect probe_a(0, 0, _layers[l].width, _layers[l].width);
        const idb::IdbRect probe_b(0, 0, rule.effective_width, rule.effective_width);
        _layers[l].max_spacing =
            std::max(_layers[l].max_spacing, requiredSpacing(l, probe_a, probe_b, rule.min_spacing, rule.effective_width));
      }
    }
  }
  return true;
}

void LocalStubRouter::initDieGeometry(idb::IdbLayout* layout)
{
  idb::IdbDie* die = layout ? layout->get_die() : nullptr;
  if (die == nullptr || die->get_points().size() < 2 || die->get_width() <= 0 || die->get_height() <= 0) {
    return;
  }
  _die_bbox = idb::IdbRect(die->get_llx(), die->get_lly(), die->get_urx(), die->get_ury());
  _has_die = true;
  if (die->is_polygon() && die->get_points().size() >= 3) {
    for (idb::IdbCoordinate<int32_t>* pt : die->get_points()) {
      if (pt != nullptr) {
        _die_polygon.outer().emplace_back(static_cast<int64_t>(pt->get_x()), static_cast<int64_t>(pt->get_y()));
      }
    }
    if (_die_polygon.outer().size() >= 3) {
      const auto& front = _die_polygon.outer().front();
      const auto& back = _die_polygon.outer().back();
      if (front.x() != back.x() || front.y() != back.y()) {
        _die_polygon.outer().push_back(front);
      }
      boost::geometry::correct(_die_polygon);
      _die_is_polygon = true;
    }
  }
}

bool LocalStubRouter::rectInsideDie(const idb::IdbRect& r) const
{
  if (!_has_die) {
    return true;
  }
  const int32_t lx = std::min(r.get_low_x(), r.get_high_x());
  const int32_t ly = std::min(r.get_low_y(), r.get_high_y());
  const int32_t hx = std::max(r.get_low_x(), r.get_high_x());
  const int32_t hy = std::max(r.get_low_y(), r.get_high_y());
  if (lx < _die_bbox.get_low_x() || hx > _die_bbox.get_high_x() || ly < _die_bbox.get_low_y() ||
      hy > _die_bbox.get_high_y()) {
    return false;
  }
  if (!_die_is_polygon) {
    return true;
  }
  using BgBox = boost::geometry::model::box<DiePoint>;
  const BgBox box(DiePoint(lx, ly), DiePoint(hx, hy));
  return boost::geometry::covered_by(box, _die_polygon);
}

bool LocalStubRouter::rectOnMfgGrid(const idb::IdbRect& r) const
{
  if (_mfg_grid <= 1) {
    return true;
  }
  return isCoordOnGrid(r.get_low_x(), _mfg_grid) && isCoordOnGrid(r.get_high_x(), _mfg_grid) &&
         isCoordOnGrid(r.get_low_y(), _mfg_grid) && isCoordOnGrid(r.get_high_y(), _mfg_grid);
}

bool LocalStubRouter::coordOnLayerMfgGrid(int lidx, int32_t coord) const
{
  if (_mfg_grid <= 1) {
    return true;
  }
  if (lidx < 0 || lidx >= static_cast<int>(_layers.size())) {
    return false;
  }
  const RoutingLayerInfo& info = _layers[lidx];
  return isCoordOnGrid(coord - info.half_low, _mfg_grid) && isCoordOnGrid(coord + info.half_high, _mfg_grid);
}

int32_t LocalStubRouter::snapCoordDown(int lidx, int32_t coord) const
{
  if (_mfg_grid <= 1 || lidx < 0 || lidx >= static_cast<int>(_layers.size())) {
    return coord;
  }
  const int32_t off = _layers[lidx].half_low;
  const int64_t snapped = floorDiv(static_cast<int64_t>(coord) - off, _mfg_grid) * _mfg_grid + off;
  return static_cast<int32_t>(std::clamp<int64_t>(snapped, std::numeric_limits<int32_t>::min(),
                                                   std::numeric_limits<int32_t>::max()));
}

int32_t LocalStubRouter::snapCoordUp(int lidx, int32_t coord) const
{
  if (_mfg_grid <= 1 || lidx < 0 || lidx >= static_cast<int>(_layers.size())) {
    return coord;
  }
  const int32_t off = _layers[lidx].half_low;
  const int64_t snapped = ceilDiv(static_cast<int64_t>(coord) - off, _mfg_grid) * _mfg_grid + off;
  return static_cast<int32_t>(std::clamp<int64_t>(snapped, std::numeric_limits<int32_t>::min(),
                                                   std::numeric_limits<int32_t>::max()));
}

void LocalStubRouter::initLayersAndMetadata(idb::IdbLayout* layout,
                                            const std::vector<ecc::geometry::GeometryLayerMetadata>& layer_meta)
{
  for (const auto& meta : layer_meta) {
    const int order = static_cast<int>(meta.order);
    _order_to_geom_layer_id[order] = meta.layer_id;
    CutSpacingRules rules;
    rules.diff_net = std::max<int32_t>(0, meta.cut_spacing);
    rules.same_net = rules.diff_net;
    rules.max_spacing = rules.diff_net;
    _order_to_cut_spacing[order] = std::move(rules);
  }
  if (layout && layout->get_layers()) {
    idb::IdbLayers* layers = layout->get_layers();
    for (idb::IdbLayer* l : layers->get_cut_layers()) {
      if (auto* cut_layer = dynamic_cast<idb::IdbLayerCut*>(l)) {
        const int order = static_cast<int>(cut_layer->get_order());
        _cut_orders_sorted.push_back(order);
        CutSpacingRules& rules = _order_to_cut_spacing[order];
        const int32_t fallback = rules.diff_net;
        populateCutSpacingRules(cut_layer, layers, fallback, rules);
      }
    }
    std::sort(_cut_orders_sorted.begin(), _cut_orders_sorted.end());
    _cut_orders_sorted.erase(std::unique(_cut_orders_sorted.begin(), _cut_orders_sorted.end()),
                             _cut_orders_sorted.end());

    // Symmetrize different-layer cut spacing rules so checking either layer catches pairs.
    std::vector<std::pair<int, DiffLayerCutSpacingRule>> reverse_rules;
    for (const auto& [src_order, rules] : _order_to_cut_spacing) {
      for (const DiffLayerCutSpacingRule& dr : rules.diff_layer_rules) {
        if (dr.other_cut_order >= 0 && dr.other_cut_order != src_order) {
          DiffLayerCutSpacingRule rev = dr;
          rev.other_cut_order = src_order;
          reverse_rules.emplace_back(dr.other_cut_order, rev);
        }
      }
    }
    for (const auto& [dst_order, rev] : reverse_rules) {
      if (auto it = _order_to_cut_spacing.find(dst_order); it != _order_to_cut_spacing.end()) {
        bool exists = false;
        for (const auto& cur : it->second.diff_layer_rules) {
          if (cur.other_cut_order == rev.other_cut_order && cur.spacing == rev.spacing &&
              cur.prl == rev.prl && cur.prl_spacing == rev.prl_spacing && cur.stack == rev.stack) {
            exists = true;
            break;
          }
        }
        if (!exists) {
          it->second.diff_layer_rules.push_back(rev);
          it->second.max_spacing = std::max({it->second.max_spacing, rev.spacing, rev.prl_spacing});
        }
      }
    }

    if (idb::IdbMaxViaStack* mvs = layout->get_max_via_stack(); mvs != nullptr && mvs->get_stacked_via_num() > 0) {
      _max_via_stack.max_cuts = static_cast<int32_t>(mvs->get_stacked_via_num());
      _max_via_stack.no_single = mvs->is_no_single();
      if (mvs->is_range()) {
        idb::IdbLayer* bot_l = layers->find_layer(mvs->get_layer_bottom());
        idb::IdbLayer* top_l = layers->find_layer(mvs->get_layer_top());
        if (bot_l != nullptr && top_l != nullptr) {
          const int b_ord = static_cast<int>(bot_l->get_order());
          const int t_ord = static_cast<int>(top_l->get_order());
          _max_via_stack.min_layer_order = std::min(b_ord, t_ord);
          _max_via_stack.max_layer_order = std::max(b_ord, t_ord);
        }
      }
    }
  }

  std::vector<idb::IdbLayerRouting*> rlayers;
  for (idb::IdbLayer* l : layout->get_layers()->get_routing_layers()) {
    if (auto* r = dynamic_cast<idb::IdbLayerRouting*>(l)) {
      rlayers.push_back(r);
    }
  }
  std::sort(rlayers.begin(), rlayers.end(),
            [](idb::IdbLayerRouting* a, idb::IdbLayerRouting* b) { return a->get_order() < b->get_order(); });

  for (size_t i = 0; i < rlayers.size(); ++i) {
    idb::IdbLayerRouting* r = rlayers[i];
    RoutingLayerInfo info;
    info.layer = r;
    info.order = static_cast<int>(r->get_order());
    if (auto it = _order_to_geom_layer_id.find(info.order); it != _order_to_geom_layer_id.end()) {
      info.geom_layer_id = it->second;
    } else {
      const int32_t lid = r->get_id();
      info.geom_layer_id = lid > info.order ? static_cast<ecc::geometry::LayerId>(lid)
                                            : static_cast<ecc::geometry::LayerId>(info.order + 1);
      _order_to_geom_layer_id[info.order] = info.geom_layer_id;
    }

    const int32_t w = r->get_width() > 0 ? r->get_width() : r->get_min_width();
    info.width = std::max<int32_t>(1, w);
    info.min_width = r->get_min_width() > 0 ? r->get_min_width() : info.width;
    info.max_width = r->get_max_width() > 0 ? r->get_max_width() : 0;
    info.half_low = info.width / 2;
    info.half_high = info.width - info.half_low;
    const int32_t raw_wire_ext = r->get_wire_extension();
    info.has_custom_wire_ext = (raw_wire_ext >= 0 && (raw_wire_ext != info.half_low || raw_wire_ext != info.half_high));
    info.end_ext = (raw_wire_ext >= 0) ? raw_wire_ext : info.half_high;
    info.routing_width_legal = (info.width >= info.min_width) && (info.max_width <= 0 || info.width <= info.max_width);

    int32_t sp = 0;
    auto tbl = r->get_spacing_table();
    if (tbl && tbl->is_parallel() && tbl->get_parallel()) {
      sp = std::max(sp, tbl->get_parallel_spacing(info.width, 0));
    }
    if (r->get_spacing_list()) {
      for (idb::IdbLayerSpacing* item : r->get_spacing_list()->get_spacing_list()) {
        if (!item) {
          continue;
        }
        if (item->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingDefault) {
          sp = std::max(sp, item->get_min_spacing());
        } else if (item->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingRange &&
                   info.width >= item->get_min_width() && info.width <= item->get_max_width()) {
          sp = std::max(sp, item->get_min_spacing());
        }
      }
    }
    info.min_spacing = sp > 0 ? sp : info.width;
    info.max_spacing = info.min_spacing;
    info.min_area = std::max<int64_t>(0, static_cast<int64_t>(r->get_area()));

    if (tbl && tbl->is_parallel() && tbl->get_parallel()) {
      for (const auto& row : tbl->get_parallel()->get_spacing_table()) {
        for (int32_t val : row) {
          info.max_spacing = std::max(info.max_spacing, val);
        }
      }
    }
    if (r->get_spacing_list()) {
      for (idb::IdbLayerSpacing* item : r->get_spacing_list()->get_spacing_list()) {
        if (!item) {
          continue;
        }
        info.max_spacing = std::max(info.max_spacing, item->get_min_spacing());
        if (item->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingEndOfLine || item->get_eol_width() > 0) {
          EolSpacingRule er;
          er.spacing = std::max<int32_t>(0, item->get_min_spacing());
          er.eol_width = item->get_eol_width();
          er.eol_within = item->get_eol_within() > 0 ? item->get_eol_within() : 0;
          er.has_parallel_edge = item->get_has_parallel_edge();
          if (er.has_parallel_edge) {
            er.par_space = std::max<int32_t>(0, item->get_par_space());
            er.par_within = std::max<int32_t>(0, item->get_par_within());
            er.two_edges = item->get_has_two_edges();
          }
          if (er.spacing > 0 && er.eol_width > 0) {
            info.eol_rules.push_back(er);
            info.max_spacing = std::max({info.max_spacing, er.spacing, er.par_space});
          }
        }
      }
    }
    for (const auto& leol : r->get_lef58_spacing_eol_list()) {
      if (!leol || leol->get_eol_space() <= 0 || leol->get_eol_width() <= 0) {
        continue;
      }
      EolSpacingRule er;
      er.spacing = leol->get_eol_space();
      er.eol_width = leol->get_eol_width();
      er.eol_within = leol->get_eol_within().has_value() ? *leol->get_eol_within() : 0;
      if (const auto& ete = leol->get_end_to_end(); ete.has_value()) {
        er.has_end_to_end = true;
        er.end_to_end_space = std::max<int32_t>(0, ete->get_end_to_end_space());
      }
      if (const auto& pe = leol->get_parallel_edge(); pe.has_value()) {
        er.has_parallel_edge = true;
        er.par_space = std::max<int32_t>(0, pe->get_par_space());
        er.par_within = std::max<int32_t>(0, pe->get_par_within());
        er.subtract_eol_width = pe->is_subtract_eol_width();
        er.two_edges = pe->is_two_edges();
        if (pe->get_min_length().has_value()) {
          er.has_min_length = true;
          er.min_length = std::max<int32_t>(0, *pe->get_min_length());
        }
      }
      if (const auto& adj = leol->get_adj_edge_length(); adj.has_value()) {
        if (adj->get_min_length().has_value()) {
          er.has_min_length = true;
          er.min_length = std::max(er.min_length, *adj->get_min_length());
        }
        if (adj->get_max_length().has_value()) {
          er.has_max_length = true;
          er.max_length = *adj->get_max_length();
        }
      }
      info.eol_rules.push_back(er);
      info.max_spacing = std::max({info.max_spacing, er.spacing, er.end_to_end_space, er.par_space});
    }

    if (const auto& ln = r->get_lef58_spacing_notchlength();
        ln != nullptr && ln->get_min_spacing() > 0 && ln->get_min_notch_length() > 0) {
      info.notch_rule.valid = true;
      info.notch_rule.min_spacing = ln->get_min_spacing();
      info.notch_rule.notch_length = ln->get_min_notch_length();
      if (ln->get_concave_ends_side_of_notch_width().has_value()) {
        info.notch_rule.concave_ends = true;
        info.notch_rule.concave_length = *ln->get_concave_ends_side_of_notch_width();
      }
      info.max_spacing = std::max(info.max_spacing, info.notch_rule.min_spacing);
    } else if (r->get_spacing_notchlength().exist() && r->get_spacing_notchlength().get_min_spacing() > 0) {
      info.notch_rule.valid = true;
      info.notch_rule.min_spacing = r->get_spacing_notchlength().get_min_spacing();
      info.notch_rule.notch_length = r->get_spacing_notchlength().get_notch_length();
      info.max_spacing = std::max(info.max_spacing, info.notch_rule.min_spacing);
    }

    if (const auto& ms = r->get_min_step(); ms != nullptr && ms->get_min_step_length() > 0) {
      info.min_step_rule.valid = true;
      info.min_step_rule.min_step_length = ms->get_min_step_length();
      info.min_step_rule.max_edges = ms->has_max_edges() ? ms->get_max_edges() : -1;
      info.min_step_rule.has_length_sum = ms->has_length_sum();
      info.min_step_rule.length_sum = ms->get_max_length_sum();
      info.min_step_rule.type = static_cast<int>(ms->get_type());
    }
    if (!info.min_step_rule.valid && !r->get_lef58_min_step().empty()) {
      for (const auto& lms : r->get_lef58_min_step()) {
        if (!lms || lms->get_min_step_length() <= 0) {
          continue;
        }
        info.min_step_rule.valid = true;
        info.min_step_rule.min_step_length = lms->get_min_step_length();
        info.min_step_rule.max_edges = lms->get_max_edges().has_value() ? *lms->get_max_edges() : -1;
        if (const auto& adj = lms->get_min_adjacent_length(); adj.has_value()) {
          info.min_step_rule.has_min_adj_length = true;
          info.min_step_rule.min_adj_length = adj->get_min_adj_length();
          info.min_step_rule.convex_corner = adj->is_convex_corner();
        }
        break;
      }
    }

    if (r->get_min_enclose_area_list()) {
      for (const idb::IdbMinEncloseArea& ea : r->get_min_enclose_area_list()->get_min_area_list()) {
        if (ea._area > 0) {
          info.min_enclosed_areas.push_back({static_cast<int64_t>(ea._area), ea._width});
        }
      }
    }
    for (const auto& la : r->get_lef58_area()) {
      if (!la || la->get_min_area() <= 0) {
        continue;
      }
      Lef58MinAreaRule ar;
      ar.min_area = static_cast<int64_t>(la->get_min_area());
      for (const auto& ems : la->get_except_min_size()) {
        ar.except_min_sizes.emplace_back(ems.get_min_width(), ems.get_min_length());
      }
      if (const auto eel = la->get_except_edge_length()) {
        if (eel->get_min_edge_length().has_value()) {
          ar.except_min_edge_length = *eel->get_min_edge_length();
        }
        ar.except_max_edge_length = eel->get_max_edge_length();
      }
      info.lef58_areas.push_back(std::move(ar));
    }

    const int32_t px = r->get_pitch_x();
    const int32_t py = r->get_pitch_y();
    const int32_t def_pitch = info.width + info.min_spacing;
    info.pitch_x = px > 0 ? px : (py > 0 ? py : def_pitch);
    info.pitch_y = py > 0 ? py : (px > 0 ? px : def_pitch);
    info.is_horizontal = r->is_horizontal();
    info.is_vertical = r->is_vertical();
    if (!info.is_horizontal && !info.is_vertical) {
      info.is_horizontal = (i % 2 == 0);
      info.is_vertical = !info.is_horizontal;
    }
    _order_to_idx[info.order] = static_cast<int>(i);
    _layers.push_back(info);
  }
}

void LocalStubRouter::buildVias(idb::IdbDesign* design)
{
  std::map<int, std::tuple<int, bool, bool, int64_t>> best_score;

  auto viaGridTier = [&](const ViaOption& opt, int lo, int hi) -> int {
    if (_mfg_grid <= 1) {
      return 2;
    }
    std::vector<idb::IdbRect> all_rects;
    all_rects.insert(all_rects.end(), opt.bottom_rects.begin(), opt.bottom_rects.end());
    all_rects.insert(all_rects.end(), opt.top_rects.begin(), opt.top_rects.end());
    all_rects.insert(all_rects.end(), opt.cut_rects.begin(), opt.cut_rects.end());
    if (all_rects.empty()) {
      return 0;
    }
    const int32_t g = _mfg_grid;
    const int32_t dx = ((-all_rects[0].get_low_x() % g) + g) % g;
    const int32_t dy = ((-all_rects[0].get_low_y() % g) + g) % g;
    for (const idb::IdbRect& r : all_rects) {
      if (!isCoordOnGrid(r.get_low_x() + dx, g) || !isCoordOnGrid(r.get_high_x() + dx, g) ||
          !isCoordOnGrid(r.get_low_y() + dy, g) || !isCoordOnGrid(r.get_high_y() + dy, g)) {
        return 0;
      }
    }
    const bool both_axes_legal =
        coordOnLayerMfgGrid(lo, dx) && coordOnLayerMfgGrid(lo, dy) &&
        coordOnLayerMfgGrid(hi, dx) && coordOnLayerMfgGrid(hi, dy);
    if (both_axes_legal) {
      return 2;
    }
    const bool lo_pref_ok = _layers[lo].is_horizontal ? coordOnLayerMfgGrid(lo, dy) : coordOnLayerMfgGrid(lo, dx);
    const bool hi_pref_ok = _layers[hi].is_horizontal ? coordOnLayerMfgGrid(hi, dy) : coordOnLayerMfgGrid(hi, dx);
    return (lo_pref_ok && hi_pref_ok) ? 1 : 0;
  };

  auto scanViaList = [&](idb::IdbVias* vias) {
    if (!vias) {
      return;
    }
    for (idb::IdbVia* via : vias->get_via_list()) {
      if (!via || !via->get_instance()) {
        continue;
      }
      idb::IdbViaMaster* vm = via->get_instance();
      idb::IdbLayerShape* bs = vm->get_bottom_layer_shape();
      idb::IdbLayerShape* ts = vm->get_top_layer_shape();
      if (!bs || !ts || !bs->get_layer() || !ts->get_layer()) {
        continue;
      }
      const int bi = layerIdxByOrder(static_cast<int>(bs->get_layer()->get_order()));
      const int ti = layerIdxByOrder(static_cast<int>(ts->get_layer()->get_order()));
      if (bi < 0 || ti < 0 || std::abs(bi - ti) != 1) {
        continue;
      }
      const int lo = std::min(bi, ti);
      const int hi = std::max(bi, ti);
      int64_t area = 0;
      for (idb::IdbRect* r : bs->get_rect_list()) {
        if (r) {
          area += r->get_area();
        }
      }
      ViaOption opt;
      opt.tmpl = via;
      opt.bottom_layer = (bi < ti) ? bs->get_layer() : ts->get_layer();
      opt.top_layer = (bi < ti) ? ts->get_layer() : bs->get_layer();
      for (idb::IdbRect* r : (bi < ti ? bs : ts)->get_rect_list()) {
        if (r) {
          opt.bottom_rects.push_back(*r);
        }
      }
      for (idb::IdbRect* r : (bi < ti ? ts : bs)->get_rect_list()) {
        if (r) {
          opt.top_rects.push_back(*r);
        }
      }
      if (idb::IdbLayerShape* cs = vm->get_cut_layer_shape(); cs && cs->get_layer()) {
        const int cut_order = static_cast<int>(cs->get_layer()->get_order());
        opt.cut_layer = dynamic_cast<idb::IdbLayerCut*>(cs->get_layer());
        if (auto it = _order_to_geom_layer_id.find(cut_order); it != _order_to_geom_layer_id.end()) {
          opt.cut_geom_layer_id = it->second;
        }
        opt.cut_spacing = cutSpacingForOrder(cut_order, false);
        opt.same_net_cut_spacing = cutSpacingForOrder(cut_order, true);
        for (idb::IdbRect* r : cs->get_rect_list()) {
          if (r) {
            opt.cut_rects.push_back(*r);
          }
        }
      }
      const int gtier = viaGridTier(opt, lo, hi);
      const auto score = std::make_tuple(gtier, vm->is_default(), vm->isOneCut(), -area);
      if (best_score.count(lo) && !(score > best_score[lo])) {
        continue;
      }
      best_score[lo] = score;
      _vias_by_low_idx[lo] = std::move(opt);
    }
  };

  if (design && design->get_layout()) {
    scanViaList(design->get_layout()->get_via_list());
  }
  if (design) {
    scanViaList(design->get_via_list());
  }
}

void LocalStubRouter::buildOwnerMap(idb::IdbDesign* design)
{
  if (!design) {
    return;
  }
  const PGSpecialNets pg_nets = findDefaultPGSpecialNets(design);

  auto registerNetOwner = [&](ecc::geometry::OwnerType owner_type, ecc::geometry::OwnerId owner_id, idb::IdbNet* net) {
    if (net == nullptr) {
      return;
    }
    const ecc::geometry::OwnerIndexKey key{owner_type, owner_id};
    if (_owner_to_net.emplace(key, net).second) {
      _net_owners[net].push_back(key);
    }
  };

  auto registerPinPG = [&](ecc::geometry::OwnerType pin_type, ecc::geometry::OwnerId pin_owner_id, idb::IdbPin* pin) {
    idb::IdbSpecialNet* sn = pin->get_special_net();
    idb::IdbTerm* term = pin->get_term();
    if (sn == nullptr && term != nullptr && (term->is_special_net() || term->is_power() || term->is_ground())) {
      sn = resolvePGSpecialNet(design, term, pg_nets);
    }
    if (sn != nullptr) {
      _owner_to_special_net[{pin_type, pin_owner_id}] = sn;
      _owner_to_special_net[{ecc::geometry::OwnerType::kVia, makeDerivedOwnerId(pin_type, pin_owner_id)}] = sn;
    } else if (term != nullptr && (term->is_special_net() || term->is_power() || term->is_ground())) {
      PGPinTag tag{term->get_name(), term->is_power(), term->is_ground()};
      _owner_pg_tag[{pin_type, pin_owner_id}] = tag;
      _owner_pg_tag[{ecc::geometry::OwnerType::kVia, makeDerivedOwnerId(pin_type, pin_owner_id)}] = tag;
    }
  };

  if (idb::IdbInstanceList* instances = design->get_instance_list()) {
    uint32_t instance_index = 0;
    for (idb::IdbInstance* instance : instances->get_instance_list()) {
      if (instance == nullptr || (!instance->is_fixed() && !instance->is_cover() && !instance->is_placed())) {
        ++instance_index;
        continue;
      }
      const ecc::geometry::OwnerId inst_owner_id =
          instance->get_id() != 0 ? static_cast<ecc::geometry::OwnerId>(instance->get_id())
                                  : static_cast<ecc::geometry::OwnerId>(instance_index);
      if (idb::IdbCellMaster* cm = instance->get_cell_master()) {
        if (auto* mprop = cm->get_macro_property(); mprop != nullptr && mprop->has_obs_spacing()) {
          for (idb::macro::ObsSpacing& osp : mprop->get_obs_spacing_list()) {
            if (osp.get_layer() != nullptr && osp.get_layer()->is_routing()) {
              const int lidx = layerIdxByOrder(static_cast<int>(osp.get_layer()->get_order()));
              if (lidx >= 0) {
                auto key = std::make_pair(inst_owner_id, lidx);
                _obs_layer_spacing[key] = std::max(_obs_layer_spacing[key], std::max<int32_t>(0, osp.get_min_spacing()));
              }
            }
          }
        }
      }
      if (idb::IdbPins* pins = instance->get_pin_list()) {
        uint32_t pin_index = 0;
        for (idb::IdbPin* pin : pins->get_pin_list()) {
          if (pin != nullptr) {
            const ecc::geometry::OwnerId pin_owner_id =
                pin->get_id() != 0 ? static_cast<ecc::geometry::OwnerId>(pin->get_id())
                                   : ((static_cast<ecc::geometry::OwnerId>(instance_index) << 32U) | pin_index);
            if (idb::IdbNet* net = pin->get_net()) {
              registerNetOwner(ecc::geometry::OwnerType::kInstancePinPortShape, pin_owner_id, net);
              registerNetOwner(ecc::geometry::OwnerType::kVia,
                               makeDerivedOwnerId(ecc::geometry::OwnerType::kInstancePinPortShape, pin_owner_id), net);
            } else {
              registerPinPG(ecc::geometry::OwnerType::kInstancePinPortShape, pin_owner_id, pin);
            }
          }
          ++pin_index;
        }
      }
      ++instance_index;
    }
  }

  if (auto* blockages = design->get_blockage_list()) {
    uint32_t blockage_index = 0;
    for (idb::IdbBlockage* blockage : blockages->get_blockage_list()) {
      if (auto* rblk = dynamic_cast<idb::IdbRoutingBlockage*>(blockage)) {
        BlockageRule br;
        br.min_spacing = std::max<int32_t>(0, rblk->get_min_spacing());
        br.effective_width = std::max<int32_t>(0, rblk->get_effective_width());
        br.except_pgnet = rblk->is_except_pgnet();
        br.is_slots_or_fills = rblk->is_slots() || rblk->is_fills();
        _blockage_rules[static_cast<ecc::geometry::OwnerId>(blockage_index)] = br;
      }
      ++blockage_index;
    }
  }

  if (idb::IdbPins* io_pins = design->get_io_pin_list()) {
    uint32_t pin_index = 0;
    for (idb::IdbPin* pin : io_pins->get_pin_list()) {
      if (pin != nullptr) {
        const ecc::geometry::OwnerId pin_owner_id =
            pin->get_id() != 0 ? static_cast<ecc::geometry::OwnerId>(pin->get_id())
                               : static_cast<ecc::geometry::OwnerId>(pin_index);
        if (idb::IdbNet* net = pin->get_net()) {
          registerNetOwner(ecc::geometry::OwnerType::kIoPinPortShape, pin_owner_id, net);
          registerNetOwner(ecc::geometry::OwnerType::kVia,
                           makeDerivedOwnerId(ecc::geometry::OwnerType::kIoPinPortShape, pin_owner_id), net);
        } else {
          registerPinPG(ecc::geometry::OwnerType::kIoPinPortShape, pin_owner_id, pin);
        }
      }
      ++pin_index;
    }
  }

  if (idb::IdbNetList* nets = design->get_net_list()) {
    uint32_t net_index = 0;
    for (idb::IdbNet* net : nets->get_net_list()) {
      if (net != nullptr) {
        const ecc::geometry::OwnerId net_owner_id =
            net->get_id() != 0 ? static_cast<ecc::geometry::OwnerId>(net->get_id()) : net_index;
        _net_owner_id[net] = net_owner_id;
        registerNetOwner(ecc::geometry::OwnerType::kNetWireSegment, net_owner_id, net);
        registerNetOwner(ecc::geometry::OwnerType::kVia,
                         makeDerivedOwnerId(ecc::geometry::OwnerType::kNetWireSegment, net_owner_id), net);
      }
      ++net_index;
    }
  }

  if (idb::IdbSpecialNetList* snets = design->get_special_net_list()) {
    uint32_t snet_index = 0;
    for (idb::IdbSpecialNet* sn : snets->get_net_list()) {
      if (sn != nullptr) {
        const ecc::geometry::OwnerId snet_owner_id = snet_index;
        _special_net_owner_id[sn] = snet_owner_id;
        _owner_to_special_net[{ecc::geometry::OwnerType::kSpecialWireSegment, snet_owner_id}] = sn;
        _owner_to_special_net[{ecc::geometry::OwnerType::kVia,
                               makeDerivedOwnerId(ecc::geometry::OwnerType::kSpecialWireSegment, snet_owner_id)}] = sn;
      }
      ++snet_index;
    }
  }
}

int LocalStubRouter::layerIdxByOrder(int order) const
{
  auto it = _order_to_idx.find(order);
  return it == _order_to_idx.end() ? -1 : it->second;
}

int LocalStubRouter::maxLayerIdxForOrder(int max_route_order) const
{
  int max_idx = -1;
  for (size_t i = 0; i < _layers.size(); ++i) {
    if (_layers[i].order <= max_route_order) {
      max_idx = static_cast<int>(i);
    }
  }
  return max_idx;
}

const ViaOption* LocalStubRouter::viaBetween(int layer_a, int layer_b) const
{
  const int lo = std::min(layer_a, layer_b);
  if (lo < 0 || lo + 1 >= static_cast<int>(_layers.size())) {
    return nullptr;
  }
  auto it = _vias_by_low_idx.find(lo);
  return (it != _vias_by_low_idx.end() && it->second.tmpl != nullptr) ? &it->second : nullptr;
}

idb::IdbRect LocalStubRouter::wireBox(int layer_idx, int32_t x1, int32_t y1, int32_t x2, int32_t y2) const
{
  const RoutingLayerInfo& info = _layers[layer_idx];
  const bool horiz = (y1 == y2 && x1 != x2);
  const bool vert = (x1 == x2 && y1 != y2);
  const int32_t ext_lo_x = (horiz && info.has_custom_wire_ext) ? info.end_ext : info.half_low;
  const int32_t ext_hi_x = (horiz && info.has_custom_wire_ext) ? info.end_ext : info.half_high;
  const int32_t ext_lo_y = (vert && info.has_custom_wire_ext) ? info.end_ext : info.half_low;
  const int32_t ext_hi_y = (vert && info.has_custom_wire_ext) ? info.end_ext : info.half_high;
  return idb::IdbRect(std::min(x1, x2) - ext_lo_x, std::min(y1, y2) - ext_lo_y, std::max(x1, x2) + ext_hi_x,
                      std::max(y1, y2) + ext_hi_y);
}

int32_t LocalStubRouter::requiredSpacing(int lidx, const idb::IdbRect& a, const idb::IdbRect& b,
                                         int32_t custom_min_spacing, int32_t b_effective_width) const
{
  const int32_t wa = std::min(a.get_width(), a.get_height());
  const int32_t wb = std::max(std::min(b.get_width(), b.get_height()), b_effective_width);
  const int32_t max_w = std::max(wa, wb);
  const int64_t ox = std::max<int64_t>(0, static_cast<int64_t>(std::min(a.get_high_x(), b.get_high_x())) -
                                              std::max(a.get_low_x(), b.get_low_x()));
  const int64_t oy = std::max<int64_t>(0, static_cast<int64_t>(std::min(a.get_high_y(), b.get_high_y())) -
                                              std::max(a.get_low_y(), b.get_low_y()));
  const int32_t par_len = static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(), std::max(ox, oy)));
  int32_t sp = std::max(_layers[lidx].min_spacing, custom_min_spacing);
  idb::IdbLayerRouting* layer = _layers[lidx].layer;
  if (layer != nullptr) {
    auto tbl = layer->get_spacing_table();
    if (tbl && tbl->is_parallel() && tbl->get_parallel()) {
      sp = std::max(sp, tbl->get_parallel_spacing(max_w, par_len));
    }
    if (layer->get_spacing_list() != nullptr) {
      for (idb::IdbLayerSpacing* item : layer->get_spacing_list()->get_spacing_list()) {
        if (!item) {
          continue;
        }
        if (item->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingDefault) {
          sp = std::max(sp, item->get_min_spacing());
        } else if (item->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingRange &&
                   max_w >= item->get_min_width() && max_w <= item->get_max_width()) {
          sp = std::max(sp, item->get_min_spacing());
        }
      }
    }
  }
  return sp;
}

ecc::geometry::LayerId LocalStubRouter::geomCutLayerIdForOrder(int cut_order) const
{
  auto it = _order_to_geom_layer_id.find(cut_order);
  return it == _order_to_geom_layer_id.end() ? 0 : it->second;
}

int32_t LocalStubRouter::cutSpacingForOrder(int cut_order, bool same_net) const
{
  if (auto it = _order_to_cut_spacing.find(cut_order); it != _order_to_cut_spacing.end()) {
    return same_net ? it->second.same_net : it->second.diff_net;
  }
  if (_design && _design->get_layout() && _design->get_layout()->get_layers()) {
    for (idb::IdbLayer* l : _design->get_layout()->get_layers()->get_cut_layers()) {
      if (l != nullptr && static_cast<int>(l->get_order()) == cut_order) {
        const auto [diff_sp, same_sp] = resolveCutSpacings(dynamic_cast<idb::IdbLayerCut*>(l), 0);
        return same_net ? same_sp : diff_sp;
      }
    }
  }
  return 0;
}

std::pair<uint8_t, uint64_t> LocalStubRouter::obstacleGroupKey(ecc::geometry::OwnerRef owner) const
{
  const ecc::geometry::OwnerIndexKey key{owner.type, owner.owner_id};
  if (auto it = _owner_to_net.find(key); it != _owner_to_net.end() && it->second != nullptr) {
    return {1U, reinterpret_cast<uint64_t>(it->second)};
  }
  if (auto it = _owner_to_special_net.find(key); it != _owner_to_special_net.end() && it->second != nullptr) {
    return {2U, reinterpret_cast<uint64_t>(it->second)};
  }
  return {static_cast<uint8_t>(owner.type), owner.owner_id};
}

std::vector<RoutingObstacleBox> LocalStubRouter::queryMergedRoutingObstacles(int lidx, const idb::IdbRect& query_rect,
                                                                               idb::IdbNet* exempt_signal_net,
                                                                               const PGShapeBox* exempt_pg) const
{
  std::vector<RoutingObstacleBox> merged;
  if (lidx < 0 || lidx >= static_cast<int>(_layers.size())) {
    return merged;
  }
  const ecc::geometry::Rect32 qbox = rect32FromIdb(query_rect);
  using BucketKey = std::tuple<uint8_t, uint64_t, int32_t, int32_t, bool>;
  std::map<BucketKey, std::vector<idb::IdbRect>> grouped;
  for (ecc::geometry::ShapeId sid : _store.query_intersect(_layers[lidx].geom_layer_id, qbox)) {
    const ecc::geometry::ShapeRecord* rec = _store.find_shape(sid);
    if (!rec || rec->state != ecc::geometry::ShapeState::kAlive || rec->kind != ecc::geometry::ShapeKind::kRect) {
      continue;
    }
    const ecc::geometry::OwnerRef owner = _store.owner_of(sid);
    if (!isPhysicalObstacleOwner(owner.type)) {
      continue;
    }
    const ecc::geometry::OwnerIndexKey okey{owner.type, owner.owner_id};
    if (exempt_signal_net != nullptr) {
      auto it = _owner_to_net.find(okey);
      if (it != _owner_to_net.end() && it->second == exempt_signal_net) {
        continue;
      }
    }
    if (exempt_pg != nullptr) {
      if (auto net_it = _owner_to_net.find(okey); net_it != _owner_to_net.end() && net_it->second != nullptr) {
        // Never exempt signal-net wires when validating PG shapes.
      } else if (exempt_pg->snet != nullptr) {
        auto sit = _owner_to_special_net.find(okey);
        if (sit != _owner_to_special_net.end() && sit->second == exempt_pg->snet) {
          continue;
        }
      } else if (isPinOwnerType(owner.type)) {
        auto pit = _owner_pg_tag.find(okey);
        if (pit != _owner_pg_tag.end()) {
          const PGPinTag& tag = pit->second;
          if ((!exempt_pg->term_name.empty() && tag.term_name == exempt_pg->term_name) ||
              (exempt_pg->is_power && tag.is_power) || (exempt_pg->is_ground && tag.is_ground)) {
            continue;
          }
        }
      }
    }

    int32_t ob_min_sp = 0;
    int32_t ob_eff_w = 0;
    bool ob_except_pg = false;
    if (owner.type == ecc::geometry::OwnerType::kBlockage) {
      if (auto bit = _blockage_rules.find(owner.owner_id); bit != _blockage_rules.end()) {
        if (bit->second.is_slots_or_fills) {
          continue;
        }
        if (exempt_pg != nullptr && bit->second.except_pgnet) {
          continue;
        }
        ob_min_sp = bit->second.min_spacing;
        ob_eff_w = bit->second.effective_width;
        ob_except_pg = bit->second.except_pgnet;
      }
    } else if (owner.type == ecc::geometry::OwnerType::kObs) {
      if (auto cit = _committed_obs_spacing.find(sid); cit != _committed_obs_spacing.end()) {
        ob_min_sp = cit->second;
      } else if (auto oit = _obs_layer_spacing.find({owner.owner_id, lidx}); oit != _obs_layer_spacing.end()) {
        ob_min_sp = oit->second;
      }
    }

    const auto gkey = obstacleGroupKey(owner);
    grouped[{gkey.first, gkey.second, ob_min_sp, ob_eff_w, ob_except_pg}].push_back(idbRectFromRect32(rec->bbox));
  }
  for (auto& [bkey, group_rects] : grouped) {
    const int32_t ob_min_sp = std::get<2>(bkey);
    const int32_t ob_eff_w = std::get<3>(bkey);
    const bool ob_except_pg = std::get<4>(bkey);
    for (const idb::IdbRect& mr : mergeMaximalRects(group_rects)) {
      merged.push_back({mr, ob_min_sp, ob_eff_w, ob_except_pg});
    }
  }
  return merged;
}

const LocalStubRouter::CachedNetLayer& LocalStubRouter::getMergedNetLayer(idb::IdbNet* target_net, int lidx) const
{
  static const CachedNetLayer kEmpty;
  if (!target_net || lidx < 0 || lidx >= static_cast<int>(_layers.size())) {
    return kEmpty;
  }
  const std::pair<idb::IdbNet*, int> cache_key{target_net, lidx};
  if (auto it = _merged_net_cache.find(cache_key); it != _merged_net_cache.end()) {
    return it->second;
  }

  std::vector<idb::IdbRect> raw_rects;
  if (auto owners_it = _net_owners.find(target_net); owners_it != _net_owners.end()) {
    const ecc::geometry::LayerId geom_lid = _layers[lidx].geom_layer_id;
    for (const ecc::geometry::OwnerIndexKey& okey : owners_it->second) {
      for (ecc::geometry::ShapeId sid : _store.query_owner(okey.type, okey.owner_id)) {
        const ecc::geometry::ShapeRecord* rec = _store.find_shape(sid);
        if (!rec || rec->state != ecc::geometry::ShapeState::kAlive || rec->kind != ecc::geometry::ShapeKind::kRect ||
            rec->layer_id != geom_lid) {
          continue;
        }
        raw_rects.push_back(idbRectFromRect32(rec->bbox));
      }
    }
  }

  CachedNetLayer entry;
  entry.rects = mergeMaximalRects(raw_rects);
  std::sort(entry.rects.begin(), entry.rects.end(), [](const idb::IdbRect& a, const idb::IdbRect& b) {
    return std::make_tuple(a.get_low_x(), a.get_low_y(), a.get_high_x(), a.get_high_y()) <
           std::make_tuple(b.get_low_x(), b.get_low_y(), b.get_high_x(), b.get_high_y());
  });
  entry.prefix_max_high_x.resize(entry.rects.size());
  int32_t running_max = std::numeric_limits<int32_t>::min();
  for (size_t i = 0; i < entry.rects.size(); ++i) {
    running_max = std::max(running_max, entry.rects[i].get_high_x());
    entry.prefix_max_high_x[i] = running_max;
  }
  auto [ins_it, inserted] = _merged_net_cache.emplace(cache_key, std::move(entry));
  (void) inserted;
  return ins_it->second;
}

std::vector<idb::IdbRect> LocalStubRouter::collectTouchingTargetNetRects(int lidx,
                                                                         const std::vector<idb::IdbRect>& seeds,
                                                                         idb::IdbNet* target_net) const
{
  std::vector<idb::IdbRect> collected;
  if (!target_net || lidx < 0 || lidx >= static_cast<int>(_layers.size()) || seeds.empty()) {
    return collected;
  }
  const CachedNetLayer& net_layer = getMergedNetLayer(target_net, lidx);
  if (net_layer.rects.empty()) {
    return collected;
  }

  std::vector<bool> visited(net_layer.rects.size(), false);
  std::queue<idb::IdbRect> q;
  for (const idb::IdbRect& s : seeds) {
    q.push(s);
  }
  while (!q.empty()) {
    const idb::IdbRect raw_cur = q.front();
    q.pop();
    const idb::IdbRect cur(std::min(raw_cur.get_low_x(), raw_cur.get_high_x()),
                           std::min(raw_cur.get_low_y(), raw_cur.get_high_y()),
                           std::max(raw_cur.get_low_x(), raw_cur.get_high_x()),
                           std::max(raw_cur.get_low_y(), raw_cur.get_high_y()));
    auto upper = std::upper_bound(net_layer.rects.begin(), net_layer.rects.end(), cur.get_high_x(),
                                  [](int32_t hx, const idb::IdbRect& r) { return hx < r.get_low_x(); });
    size_t idx = static_cast<size_t>(upper - net_layer.rects.begin());
    while (idx > 0) {
      --idx;
      if (net_layer.prefix_max_high_x[idx] < cur.get_low_x()) {
        break;
      }
      if (visited[idx]) {
        continue;
      }
      const idb::IdbRect& cand = net_layer.rects[idx];
      if (!rectsTouchOrOverlap(cur, cand)) {
        continue;
      }
      visited[idx] = true;
      collected.push_back(cand);
      q.push(cand);
    }
  }
  return collected;
}

std::vector<idb::IdbRect> LocalStubRouter::collectNearbyTargetNetRects(int lidx,
                                                                       const std::vector<idb::IdbRect>& seeds,
                                                                       int32_t margin, idb::IdbNet* target_net) const
{
  if (!target_net || lidx < 0 || lidx >= static_cast<int>(_layers.size()) || seeds.empty()) {
    return {};
  }
  const CachedNetLayer& net_layer = getMergedNetLayer(target_net, lidx);
  if (net_layer.rects.empty()) {
    return {};
  }

  int32_t lx = std::numeric_limits<int32_t>::max();
  int32_t ly = std::numeric_limits<int32_t>::max();
  int32_t hx = std::numeric_limits<int32_t>::min();
  int32_t hy = std::numeric_limits<int32_t>::min();
  for (const idb::IdbRect& s : seeds) {
    lx = std::min({lx, s.get_low_x(), s.get_high_x()});
    ly = std::min({ly, s.get_low_y(), s.get_high_y()});
    hx = std::max({hx, s.get_low_x(), s.get_high_x()});
    hy = std::max({hy, s.get_low_y(), s.get_high_y()});
  }
  const int32_t m = std::max<int32_t>(0, margin);
  const idb::IdbRect qbox(lx - m, ly - m, hx + m, hy + m);

  std::vector<idb::IdbRect> all_seeds = seeds;
  auto upper = std::upper_bound(net_layer.rects.begin(), net_layer.rects.end(), qbox.get_high_x(),
                                [](int32_t x, const idb::IdbRect& r) { return x < r.get_low_x(); });
  size_t idx = static_cast<size_t>(upper - net_layer.rects.begin());
  while (idx > 0) {
    --idx;
    if (net_layer.prefix_max_high_x[idx] < qbox.get_low_x()) {
      break;
    }
    const idb::IdbRect& cand = net_layer.rects[idx];
    if (rectsTouchOrOverlap(qbox, cand)) {
      all_seeds.push_back(cand);
    }
  }
  return collectTouchingTargetNetRects(lidx, all_seeds, target_net);
}

bool LocalStubRouter::checkNetPolygonDrcWithNearby(int lidx, const std::vector<idb::IdbRect>& new_rects,
                                                   idb::IdbNet* target_net,
                                                   std::vector<idb::IdbRect>* out_same_net_conflicts) const
{
  if (lidx < 0 || lidx >= static_cast<int>(_layers.size()) || new_rects.empty()) {
    return true;
  }
  const RoutingLayerInfo& linfo = _layers[lidx];
  const bool need_poly_drc =
      linfo.max_width > 0 || linfo.min_width > 0 || !linfo.min_enclosed_areas.empty() || linfo.notch_rule.valid ||
      linfo.min_step_rule.valid || linfo.min_spacing > 0;
  if (!need_poly_drc) {
    return true;
  }

  namespace gtl = boost::polygon;
  const int32_t margin = linfo.max_spacing + 2 * linfo.width;
  const std::vector<idb::IdbRect> existing = collectNearbyTargetNetRects(lidx, new_rects, margin, target_net);
  gtl::polygon_90_set_data<int64_t> existing_ps;
  for (const idb::IdbRect& r : existing) {
    const int32_t lx = std::min(r.get_low_x(), r.get_high_x());
    const int32_t ly = std::min(r.get_low_y(), r.get_high_y());
    const int32_t hx = std::max(r.get_low_x(), r.get_high_x());
    const int32_t hy = std::max(r.get_low_y(), r.get_high_y());
    if (lx < hx && ly < hy) {
      existing_ps.insert(gtl::rectangle_data<int64_t>(lx, ly, hx, hy));
    }
  }
  auto merged_ps = existing_ps;
  for (const idb::IdbRect& r : new_rects) {
    const int32_t lx = std::min(r.get_low_x(), r.get_high_x());
    const int32_t ly = std::min(r.get_low_y(), r.get_high_y());
    const int32_t hx = std::max(r.get_low_x(), r.get_high_x());
    const int32_t hy = std::max(r.get_low_y(), r.get_high_y());
    if (lx < hx && ly < hy) {
      merged_ps.insert(gtl::rectangle_data<int64_t>(lx, ly, hx, hy));
    }
  }
  return checkMergedNetPolygonDrc(
      existing_ps, merged_ps, new_rects, linfo,
      [&](const idb::IdbRect& a, const idb::IdbRect& b) { return requiredSpacing(lidx, a, b); },
      out_same_net_conflicts);
}

bool LocalStubRouter::checkMazeViaCutsValid(int low_idx, int32_t cx, int32_t cy,
                                            const std::vector<CutShapeBox>& ancestor_cuts,
                                            idb::IdbNet* target_net) const
{
  const ViaOption* vo = viaBetween(low_idx, low_idx + 1);
  if (!vo || !vo->cut_layer) {
    return true;
  }
  const int cut_order = static_cast<int>(vo->cut_layer->get_order());
  std::vector<CutShapeBox> proposed;
  proposed.reserve(vo->cut_rects.size());
  for (const idb::IdbRect& cr : vo->cut_rects) {
    proposed.push_back({cut_order, shiftedRect(cr, cx, cy)});
  }
  return checkCutSetValid(proposed, target_net, nullptr, &ancestor_cuts, nullptr);
}

bool LocalStubRouter::checkCutSetValid(const std::vector<CutShapeBox>& proposed_cuts, idb::IdbNet* target_net,
                                       const PGCutShapeBox* exempt_pg, const std::vector<CutShapeBox>* extra_peer_cuts,
                                       const std::vector<CutShapeBox>* extra_foreign_cuts) const
{
  if (proposed_cuts.empty()) {
    return true;
  }

  struct SceneCut
  {
    int cut_order = -1;
    idb::IdbRect rect;
    CutRectKey key;
    const idb::IdbNet* net = nullptr;
    bool is_proposed = false;
    bool is_peer_same_net = false;
  };

  int32_t min_x = std::numeric_limits<int32_t>::max();
  int32_t min_y = std::numeric_limits<int32_t>::max();
  int32_t max_x = std::numeric_limits<int32_t>::min();
  int32_t max_y = std::numeric_limits<int32_t>::min();
  std::unordered_set<int> needed_orders;
  std::unordered_set<int> proposed_orders;
  for (const CutShapeBox& pc : proposed_cuts) {
    min_x = std::min({min_x, pc.rect.get_low_x(), pc.rect.get_high_x()});
    min_y = std::min({min_y, pc.rect.get_low_y(), pc.rect.get_high_y()});
    max_x = std::max({max_x, pc.rect.get_low_x(), pc.rect.get_high_x()});
    max_y = std::max({max_y, pc.rect.get_low_y(), pc.rect.get_high_y()});
    needed_orders.insert(pc.cut_order);
    proposed_orders.insert(pc.cut_order);
    if (auto it = _order_to_cut_spacing.find(pc.cut_order); it != _order_to_cut_spacing.end()) {
      for (const DiffLayerCutSpacingRule& dr : it->second.diff_layer_rules) {
        if (dr.other_cut_order >= 0) {
          needed_orders.insert(dr.other_cut_order);
        }
      }
    }
  }
  std::vector<int> stack_orders;
  if (_max_via_stack.max_cuts > 0) {
    for (int co : _cut_orders_sorted) {
      if (co >= _max_via_stack.min_layer_order && co <= _max_via_stack.max_layer_order) {
        stack_orders.push_back(co);
        needed_orders.insert(co);
      }
    }
  }

  std::unordered_map<int, std::vector<SceneCut>> before_by_order;
  std::unordered_map<int, std::vector<SceneCut>> after_by_order;

  for (int co : needed_orders) {
    CutSpacingRules rules;
    if (auto it = _order_to_cut_spacing.find(co); it != _order_to_cut_spacing.end()) {
      rules = it->second;
    }
    int32_t qmargin = std::max<int32_t>(1, rules.max_spacing);
    for (const CutAdjacentRule& ar : rules.adjacent_rules) {
      qmargin = std::max(qmargin, ar.cut_within * 2);
    }
    for (const CutArraySpacingRule& arr : rules.array_rules) {
      qmargin = std::max(qmargin, std::max(arr.cut_spacing, arr.array_spacing) * arr.array_cuts);
    }
    if (auto p_it = proposed_orders.find(co); p_it == proposed_orders.end()) {
      for (int pco : proposed_orders) {
        if (auto rit = _order_to_cut_spacing.find(pco); rit != _order_to_cut_spacing.end()) {
          qmargin = std::max(qmargin, rit->second.max_spacing);
        }
      }
    }

    auto& bvec = before_by_order[co];
    const ecc::geometry::LayerId cut_lid = geomCutLayerIdForOrder(co);
    if (cut_lid != 0) {
      const ecc::geometry::Rect32 qbox{min_x - qmargin, min_y - qmargin, max_x + qmargin, max_y + qmargin};
      for (ecc::geometry::ShapeId sid : _store.query_intersect(cut_lid, qbox)) {
        const ecc::geometry::ShapeRecord* rec = _store.find_shape(sid);
        if (!rec || rec->state != ecc::geometry::ShapeState::kAlive || rec->kind != ecc::geometry::ShapeKind::kRect) {
          continue;
        }
        const ecc::geometry::OwnerRef owner = _store.owner_of(sid);
        if (!isPhysicalObstacleOwner(owner.type)) {
          continue;
        }
        const ecc::geometry::OwnerIndexKey okey{owner.type, owner.owner_id};
        const auto net_it = _owner_to_net.find(okey);
        const idb::IdbNet* existing_net = (net_it != _owner_to_net.end()) ? net_it->second : nullptr;
        if (exempt_pg != nullptr) {
          if (existing_net != nullptr) {
            // Never exempt signal nets.
          } else if (exempt_pg->snet != nullptr) {
            auto sit = _owner_to_special_net.find(okey);
            if (sit != _owner_to_special_net.end() && sit->second == exempt_pg->snet) {
              continue;
            }
          } else if (isPinOwnerType(owner.type)) {
            auto pit = _owner_pg_tag.find(okey);
            if (pit != _owner_pg_tag.end()) {
              const PGPinTag& tag = pit->second;
              if ((!exempt_pg->term_name.empty() && tag.term_name == exempt_pg->term_name) ||
                  (exempt_pg->is_power && tag.is_power) || (exempt_pg->is_ground && tag.is_ground)) {
                continue;
              }
            }
          }
        }
        const idb::IdbRect ir = idbRectFromRect32(rec->bbox);
        bvec.push_back({co, ir, makeCutRectKey(ir), existing_net, false, false});
      }
    }
    if (extra_peer_cuts != nullptr) {
      for (const CutShapeBox& ep : *extra_peer_cuts) {
        if (ep.cut_order == co) {
          bvec.push_back({co, ep.rect, makeCutRectKey(ep.rect), target_net, false, true});
        }
      }
    }
    if (extra_foreign_cuts != nullptr) {
      for (const CutShapeBox& ef : *extra_foreign_cuts) {
        if (ef.cut_order == co) {
          bvec.push_back({co, ef.rect, makeCutRectKey(ef.rect), nullptr, false, false});
        }
      }
    }
    after_by_order[co] = bvec;
  }

  for (const CutShapeBox& pc : proposed_cuts) {
    after_by_order[pc.cut_order].push_back({pc.cut_order, pc.rect, makeCutRectKey(pc.rect), target_net, true, true});
  }

  for (int co : proposed_orders) {
    CutSpacingRules rules;
    if (auto it = _order_to_cut_spacing.find(co); it != _order_to_cut_spacing.end()) {
      rules = it->second;
    } else {
      rules.diff_net = cutSpacingForOrder(co, false);
      rules.same_net = cutSpacingForOrder(co, true);
    }

    const auto& after = after_by_order[co];
    const auto& before = before_by_order[co];

    // 1. Direct cut spacing and LEF58 cut PRL spacing.
    for (size_t i = 0; i < after.size(); ++i) {
      const SceneCut& p = after[i];
      if (!p.is_proposed) {
        continue;
      }
      for (size_t j = 0; j < after.size(); ++j) {
        if (i == j) {
          continue;
        }
        const SceneCut& q = after[j];
        if (q.is_proposed && j < i) {
          continue;
        }
        const bool is_same_net = (q.is_proposed || q.is_peer_same_net || (target_net != nullptr && q.net == target_net));
        int32_t req = is_same_net ? rules.same_net : rules.diff_net;
        if (rules.prl_spacing > 0 && cutParallelLength(p.rect, q.rect) > rules.prl) {
          req = std::max(req, rules.prl_spacing);
        }
        if (rectsTouchOrOverlap(p.rect, q.rect) || (req > 0 && euclidDistLessThan(p.rect, q.rect, req))) {
          return false;
        }
      }
    }

    // 2. ADJACENTCUTS (both direct around proposed cuts and neighbor-triggered on existing cuts).
    if (!rules.adjacent_rules.empty()) {
      auto buildUniqueCuts = [](const std::vector<SceneCut>& src) {
        std::vector<SceneCut> uniq;
        std::set<CutRectKey> seen;
        for (const SceneCut& sc : src) {
          if (seen.insert(sc.key).second) {
            uniq.push_back(sc);
          }
        }
        return uniq;
      };
      const std::vector<SceneCut> after_uniq = buildUniqueCuts(after);
      const std::vector<SceneCut> before_uniq = buildUniqueCuts(before);

      for (const CutAdjacentRule& ar : rules.adjacent_rules) {
        auto violatesAdj = [&](const SceneCut& center, const std::vector<SceneCut>& pool) -> bool {
          int32_t cnt = 0;
          bool has_close = false;
          for (const SceneCut& nb : pool) {
            if (nb.key == center.key) {
              continue;
            }
            if (euclidDistLessThan(center.rect, nb.rect, ar.cut_within)) {
              ++cnt;
              if (euclidDistLessThan(center.rect, nb.rect, ar.cut_spacing)) {
                has_close = true;
              }
            }
          }
          return has_close && cnt >= ar.adjacent_cuts;
        };

        for (const SceneCut& sc : after_uniq) {
          bool near_proposed = sc.is_proposed;
          if (!near_proposed) {
            for (const CutShapeBox& pc : proposed_cuts) {
              if (pc.cut_order == co && euclidDistLessThan(sc.rect, pc.rect, ar.cut_within)) {
                near_proposed = true;
                break;
              }
            }
          }
          if (!near_proposed) {
            continue;
          }
          if (!violatesAdj(sc, after_uniq)) {
            continue;
          }
          if (sc.is_proposed) {
            return false;
          }
          if (!violatesAdj(sc, before_uniq)) {
            return false;
          }
        }
      }
    }

    // 3. ARRAYSPACING.
    if (!rules.array_rules.empty()) {
      for (const CutArraySpacingRule& arr : rules.array_rules) {
        if (arr.array_cuts < 2 || arr.array_spacing <= 0) {
          continue;
        }
        const size_t n = after.size();
        std::vector<bool> visited(n, false);
        for (size_t seed = 0; seed < n; ++seed) {
          if (visited[seed] || !after[seed].is_proposed) {
            continue;
          }
          std::vector<size_t> comp;
          std::queue<size_t> q;
          visited[seed] = true;
          q.push(seed);
          while (!q.empty()) {
            const size_t u = q.front();
            q.pop();
            comp.push_back(u);
            for (size_t v = 0; v < n; ++v) {
              if (!visited[v] && euclidDistAtMost(after[u].rect, after[v].rect, arr.cut_spacing)) {
                visited[v] = true;
                q.push(v);
              }
            }
          }
          if (comp.size() < static_cast<size_t>(arr.array_cuts)) {
            continue;
          }
          for (size_t ci = 0; ci < comp.size(); ++ci) {
            for (size_t cj = ci + 1; cj < comp.size(); ++cj) {
              const SceneCut& ca = after[comp[ci]];
              const SceneCut& cb = after[comp[cj]];
              if (euclidDistLessThan(ca.rect, cb.rect, arr.array_spacing)) {
                return false;
              }
            }
          }
        }
      }
    }

    // 4. DifferentLayerCutSpacing (respecting STACK on same net).
    for (const DiffLayerCutSpacingRule& dr : rules.diff_layer_rules) {
      auto other_it = after_by_order.find(dr.other_cut_order);
      if (other_it == after_by_order.end()) {
        continue;
      }
      for (const SceneCut& p : after) {
        if (!p.is_proposed) {
          continue;
        }
        for (const SceneCut& q : other_it->second) {
          const bool is_same_net =
              (q.is_proposed || q.is_peer_same_net || (target_net != nullptr && q.net == target_net));
          if (is_same_net) {
            if (!dr.stack && rectsHavePositiveOverlap(p.rect, q.rect)) {
              return false;
            }
            continue;
          }
          if (rectsTouchOrOverlap(p.rect, q.rect)) {
            return false;
          }
          const int32_t req = (cutParallelLength(p.rect, q.rect) > dr.prl) ? dr.prl_spacing : dr.spacing;
          if (req > 0 && euclidDistLessThan(p.rect, q.rect, req)) {
            return false;
          }
        }
      }
    }
  }

  // 5. MAXVIASTACK across consecutive cut layers.
  if (_max_via_stack.max_cuts > 0 && static_cast<int32_t>(stack_orders.size()) > _max_via_stack.max_cuts) {
    const size_t ns = stack_orders.size();
    std::vector<std::vector<idb::IdbRect>> same_net_cuts(ns);
    std::unordered_map<int, size_t> order_to_sidx;
    for (size_t s = 0; s < ns; ++s) {
      order_to_sidx[stack_orders[s]] = s;
      for (const SceneCut& sc : after_by_order[stack_orders[s]]) {
        if (sc.is_proposed || sc.is_peer_same_net || (target_net != nullptr && sc.net == target_net)) {
          same_net_cuts[s].push_back(sc.rect);
        }
      }
    }

    auto countChainDir = [&](size_t start_s, int dir, const idb::IdbRect& seed_rect) -> int32_t {
      int32_t depth = 0;
      std::vector<idb::IdbRect> frontier{seed_rect};
      int cur = static_cast<int>(start_s) + dir;
      while (cur >= 0 && cur < static_cast<int>(ns) && !frontier.empty()) {
        std::vector<idb::IdbRect> next_frontier;
        for (const idb::IdbRect& f : frontier) {
          for (const idb::IdbRect& cand : same_net_cuts[static_cast<size_t>(cur)]) {
            if (rectsHavePositiveOverlap(f, cand)) {
              next_frontier.push_back(cand);
            }
          }
        }
        if (next_frontier.empty()) {
          break;
        }
        ++depth;
        frontier.swap(next_frontier);
        cur += dir;
      }
      return depth;
    };

    for (const CutShapeBox& pc : proposed_cuts) {
      auto sit = order_to_sidx.find(pc.cut_order);
      if (sit == order_to_sidx.end()) {
        continue;
      }
      const size_t s = sit->second;
      const int32_t total_stack = 1 + countChainDir(s, -1, pc.rect) + countChainDir(s, 1, pc.rect);
      if (total_stack > _max_via_stack.max_cuts) {
        return false;
      }
    }
  }

  return true;
}

bool LocalStubRouter::checkNetMetalClear(int lidx, const std::vector<idb::IdbRect>& new_rects, idb::IdbNet* target_net,
                                          std::vector<idb::IdbRect>* out_conflicts) const
{
  if (new_rects.empty()) {
    return true;
  }
  const std::vector<idb::IdbRect> existing = collectTouchingTargetNetRects(lidx, new_rects, target_net);
  std::vector<idb::IdbRect> combined = existing;
  combined.insert(combined.end(), new_rects.begin(), new_rects.end());
  std::vector<idb::IdbRect> merged = mergeMaximalRects(combined);
  if (merged.empty()) {
    merged = new_rects;
  }

  const int32_t margin = _layers[lidx].max_spacing + 2 * _layers[lidx].width;
  int32_t qlx = merged[0].get_low_x(), qly = merged[0].get_low_y();
  int32_t qhx = merged[0].get_high_x(), qhy = merged[0].get_high_y();
  for (const idb::IdbRect& mr : merged) {
    qlx = std::min(qlx, mr.get_low_x());
    qly = std::min(qly, mr.get_low_y());
    qhx = std::max(qhx, mr.get_high_x());
    qhy = std::max(qhy, mr.get_high_y());
  }
  const std::vector<RoutingObstacleBox> foreign = queryMergedRoutingObstacles(
      lidx, idb::IdbRect(qlx - margin, qly - margin, qhx + margin, qhy + margin), target_net, nullptr);

  bool ok = true;
  for (const idb::IdbRect& mr : merged) {
    bool overlaps_new = false;
    for (const idb::IdbRect& nr : new_rects) {
      if (rectsHavePositiveOverlap(mr, nr)) {
        overlaps_new = true;
        break;
      }
    }
    if (!overlaps_new) {
      continue;
    }
    bool inside_existing = false;
    for (const idb::IdbRect& er : existing) {
      if (rectContainedIn(mr, er)) {
        inside_existing = true;
        break;
      }
    }
    if (inside_existing) {
      continue;
    }
    for (const RoutingObstacleBox& fob : foreign) {
      if (!rectClearOfObstacle(mr, fob.rect, requiredSpacing(lidx, mr, fob.rect, fob.min_spacing, fob.effective_width))) {
        if (out_conflicts == nullptr) {
          return false;
        }
        ok = false;
        out_conflicts->push_back(fob.rect);
      }
    }
  }

  if (!_layers[lidx].eol_rules.empty()) {
    namespace gtl = boost::polygon;
    auto makeSet = [](const std::vector<idb::IdbRect>& rects) {
      gtl::polygon_90_set_data<int64_t> ps;
      for (const idb::IdbRect& r : rects) {
        const int32_t lx = std::min(r.get_low_x(), r.get_high_x());
        const int32_t ly = std::min(r.get_low_y(), r.get_high_y());
        const int32_t hx = std::max(r.get_low_x(), r.get_high_x());
        const int32_t hy = std::max(r.get_low_y(), r.get_high_y());
        if (lx < hx && ly < hy) {
          ps.insert(gtl::rectangle_data<int64_t>(lx, ly, hx, hy));
        }
      }
      return ps;
    };
    const std::vector<idb::IdbRect> nearby_existing = collectNearbyTargetNetRects(lidx, new_rects, margin, target_net);
    std::vector<idb::IdbRect> nearby_combined = nearby_existing;
    nearby_combined.insert(nearby_combined.end(), new_rects.begin(), new_rects.end());
    const auto existing_ps = makeSet(nearby_existing);
    const auto merged_ps = makeSet(nearby_combined);
    gtl::polygon_90_set_data<int64_t> foreign_ps;
    for (const RoutingObstacleBox& fob : foreign) {
      const int32_t lx = std::min(fob.rect.get_low_x(), fob.rect.get_high_x());
      const int32_t ly = std::min(fob.rect.get_low_y(), fob.rect.get_high_y());
      const int32_t hx = std::max(fob.rect.get_low_x(), fob.rect.get_high_x());
      const int32_t hy = std::max(fob.rect.get_low_y(), fob.rect.get_high_y());
      if (lx < hx && ly < hy) {
        foreign_ps.insert(gtl::rectangle_data<int64_t>(lx, ly, hx, hy));
      }
    }
    const std::vector<DrcPolygon> existing_polys = extractPolygonRings(existing_ps);
    const std::vector<DrcPolygon> merged_polys = extractPolygonRings(merged_ps);
    if (!checkEndOfLineSpacing(existing_polys, merged_polys, foreign_ps, new_rects, _layers[lidx].eol_rules)) {
      if (out_conflicts == nullptr) {
        return false;
      }
      ok = false;
      for (const RoutingObstacleBox& fob : foreign) {
        for (const idb::IdbRect& nr : new_rects) {
          if (!rectClearOfObstacle(nr, fob.rect, margin)) {
            out_conflicts->push_back(fob.rect);
            break;
          }
        }
      }
    }
  }
  return ok;
}

bool LocalStubRouter::checkNetMinArea(int lidx, const std::vector<idb::IdbRect>& new_rects, idb::IdbNet* target_net,
                                      std::vector<MinAreaDeficit>* out_deficits) const
{
  if (lidx < 0 || lidx >= static_cast<int>(_layers.size()) ||
      (_layers[lidx].min_area <= 0 && _layers[lidx].lef58_areas.empty()) || new_rects.empty()) {
    return true;
  }
  namespace gtl = boost::polygon;
  using GtlRect = gtl::rectangle_data<int64_t>;

  const std::vector<idb::IdbRect> existing = collectTouchingTargetNetRects(lidx, new_rects, target_net);
  gtl::polygon_90_set_data<int64_t> polyset;
  auto insertRect = [&](const idb::IdbRect& r) {
    const int32_t lx = std::min(r.get_low_x(), r.get_high_x());
    const int32_t ly = std::min(r.get_low_y(), r.get_high_y());
    const int32_t hx = std::max(r.get_low_x(), r.get_high_x());
    const int32_t hy = std::max(r.get_low_y(), r.get_high_y());
    if (lx < hx && ly < hy) {
      polyset.insert(GtlRect(lx, ly, hx, hy));
    }
  };
  for (const idb::IdbRect& er : existing) {
    insertRect(er);
  }
  for (const idb::IdbRect& nr : new_rects) {
    insertRect(nr);
  }

  const RoutingLayerInfo& info = _layers[lidx];
  const std::vector<DrcPolygon> polys = extractPolygonRings(polyset);
  bool ok = true;
  for (const DrcPolygon& dp : polys) {
    bool touches_new = false;
    for (const idb::IdbRect& pr : dp.max_rects) {
      for (const idb::IdbRect& nr : new_rects) {
        if (rectsHavePositiveOverlap(pr, nr)) {
          touches_new = true;
          break;
        }
      }
      if (touches_new) {
        break;
      }
    }
    if (!touches_new) {
      continue;
    }
    int64_t deficit = 0;
    if (!checkLef58MinArea(dp, info.min_area, info.lef58_areas, &deficit)) {
      if (out_deficits == nullptr) {
        return false;
      }
      out_deficits->push_back({lidx, dp.bbox, dp.area, dp.area + deficit});
      ok = false;
    }
  }
  return ok;
}

bool LocalStubRouter::validateCandidateCell(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom) const
{
  if (!rectInsideDie(geom.cell_bbox)) {
    return false;
  }
  if (!isCoordOnGrid(geom.inst_x, _mfg_grid) || !isCoordOnGrid(geom.inst_y, _mfg_grid)) {
    return false;
  }

  std::map<int, std::vector<idb::IdbRect>> sig_by_layer;
  for (const LayerShapeBox& sh : geom.all_signal_metal_shapes) {
    if (!rectInsideDie(sh.rect)) {
      return false;
    }
    if (sh.layer_idx >= 0 && sh.layer_idx < static_cast<int>(_layers.size())) {
      sig_by_layer[sh.layer_idx].push_back(sh.rect);
    }
  }
  for (const auto& [lidx, new_rects] : sig_by_layer) {
    if (!checkNetMetalClear(lidx, new_rects, target_net, nullptr) ||
        !checkNetPolygonDrcWithNearby(lidx, new_rects, target_net, nullptr)) {
      return false;
    }
  }

  for (const PGShapeBox& pg : geom.pg_metal_shapes) {
    if (!rectInsideDie(pg.rect)) {
      return false;
    }
    if (pg.layer_idx < 0 || pg.layer_idx >= static_cast<int>(_layers.size())) {
      continue;
    }
    const int32_t margin = _layers[pg.layer_idx].max_spacing + 2 * _layers[pg.layer_idx].width;
    const idb::IdbRect qrect(pg.rect.get_low_x() - margin, pg.rect.get_low_y() - margin,
                             pg.rect.get_high_x() + margin, pg.rect.get_high_y() + margin);
    for (const RoutingObstacleBox& fob : queryMergedRoutingObstacles(pg.layer_idx, qrect, nullptr, &pg)) {
      if (!rectClearOfObstacle(pg.rect, fob.rect,
                               requiredSpacing(pg.layer_idx, pg.rect, fob.rect, fob.min_spacing, fob.effective_width))) {
        return false;
      }
    }
  }

  for (const LayerShapeBox& obs : geom.obs_metal_shapes) {
    if (!rectInsideDie(obs.rect)) {
      return false;
    }
    if (obs.layer_idx < 0 || obs.layer_idx >= static_cast<int>(_layers.size())) {
      continue;
    }
    const int32_t margin = std::max(_layers[obs.layer_idx].max_spacing, obs.min_spacing) + 2 * _layers[obs.layer_idx].width;
    const idb::IdbRect qrect(obs.rect.get_low_x() - margin, obs.rect.get_low_y() - margin,
                             obs.rect.get_high_x() + margin, obs.rect.get_high_y() + margin);
    for (const RoutingObstacleBox& fob : queryMergedRoutingObstacles(obs.layer_idx, qrect, nullptr, nullptr)) {
      if (!rectClearOfObstacle(
              obs.rect, fob.rect,
              requiredSpacing(obs.layer_idx, obs.rect, fob.rect, std::max(obs.min_spacing, fob.min_spacing),
                              fob.effective_width))) {
        return false;
      }
    }
  }

  for (const CutShapeBox& cs : geom.all_signal_cut_shapes) {
    if (!rectInsideDie(cs.rect)) {
      return false;
    }
  }
  if (!geom.all_signal_cut_shapes.empty() &&
      !checkCutSetValid(geom.all_signal_cut_shapes, target_net, nullptr, nullptr, nullptr)) {
    return false;
  }
  for (const PGCutShapeBox& cs : geom.pg_cut_shapes) {
    if (!rectInsideDie(cs.rect)) {
      return false;
    }
    std::vector<CutShapeBox> one{{cs.cut_order, cs.rect}};
    if (!checkCutSetValid(one, nullptr, &cs, nullptr, nullptr)) {
      return false;
    }
  }
  for (const CutShapeBox& cs : geom.obs_cut_shapes) {
    if (!rectInsideDie(cs.rect)) {
      return false;
    }
  }
  if (!geom.obs_cut_shapes.empty() && !checkCutSetValid(geom.obs_cut_shapes, nullptr, nullptr, nullptr, nullptr)) {
    return false;
  }
  return true;
}

bool LocalStubRouter::validateProposedRoute(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom,
                                            const RoutePlan& plan,
                                            std::vector<std::vector<idb::IdbRect>>* out_prl_conflicts) const
{
  return validateProposedRouteImpl(target_net, geom, plan, out_prl_conflicts, nullptr, nullptr);
}

bool LocalStubRouter::validateProposedRouteImpl(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom,
                                                const RoutePlan& plan,
                                                std::vector<std::vector<idb::IdbRect>>* out_prl_conflicts,
                                                std::vector<std::vector<idb::IdbRect>>* out_same_net_conflicts,
                                                std::vector<MinAreaDeficit>* out_min_area_deficits) const
{
  if (out_min_area_deficits != nullptr) {
    out_min_area_deficits->clear();
  }
  if (!plan.found) {
    return false;
  }
  struct PendingRouteCut
  {
    int cut_order = -1;
    int via_group_id = 0;
    idb::IdbRect rect;
  };
  const size_t nlayers = _layers.size();
  std::vector<std::vector<idb::IdbRect>> new_stub_metal_by_layer(nlayers);
  std::vector<std::vector<idb::IdbRect>> new_all_metal_by_layer(nlayers);
  std::vector<PendingRouteCut> new_cuts;

  for (const LayerShapeBox& sh : geom.all_signal_metal_shapes) {
    if (sh.layer_idx >= 0 && sh.layer_idx < static_cast<int>(nlayers)) {
      new_all_metal_by_layer[sh.layer_idx].push_back(sh.rect);
    }
  }

  int next_via_group_id = 1;
  for (const StubMove& mv : plan.moves) {
    if (mv.is_via) {
      const ViaOption* vo = viaBetween(mv.layer_idx, mv.next_layer_idx);
      if (!vo || !vo->tmpl) {
        return false;
      }
      const int via_group_id = next_via_group_id++;
      for (const idb::IdbRect& br : vo->bottom_rects) {
        const idb::IdbRect placed = shiftedRect(br, mv.x1, mv.y1);
        if (!rectInsideDie(placed) || !rectOnMfgGrid(placed)) {
          return false;
        }
        new_stub_metal_by_layer[mv.layer_idx].push_back(placed);
        new_all_metal_by_layer[mv.layer_idx].push_back(placed);
      }
      for (const idb::IdbRect& tr : vo->top_rects) {
        const idb::IdbRect placed = shiftedRect(tr, mv.x1, mv.y1);
        if (!rectInsideDie(placed) || !rectOnMfgGrid(placed)) {
          return false;
        }
        new_stub_metal_by_layer[mv.next_layer_idx].push_back(placed);
        new_all_metal_by_layer[mv.next_layer_idx].push_back(placed);
      }
      if (vo->cut_layer != nullptr) {
        const int cut_order = static_cast<int>(vo->cut_layer->get_order());
        for (const idb::IdbRect& cr : vo->cut_rects) {
          const idb::IdbRect placed = shiftedRect(cr, mv.x1, mv.y1);
          if (!rectInsideDie(placed) || !rectOnMfgGrid(placed)) {
            return false;
          }
          new_cuts.push_back({cut_order, via_group_id, placed});
        }
      }
    } else {
      if (mv.layer_idx < 0 || mv.layer_idx >= static_cast<int>(nlayers) || !_layers[mv.layer_idx].routing_width_legal) {
        return false;
      }
      const idb::IdbRect wbox = wireBox(mv.layer_idx, mv.x1, mv.y1, mv.x2, mv.y2);
      if (!rectInsideDie(wbox) || !rectOnMfgGrid(wbox)) {
        return false;
      }
      new_stub_metal_by_layer[mv.layer_idx].push_back(wbox);
      new_all_metal_by_layer[mv.layer_idx].push_back(wbox);
    }
  }

  if (out_prl_conflicts != nullptr) {
    out_prl_conflicts->assign(nlayers, {});
  }
  if (out_same_net_conflicts != nullptr) {
    out_same_net_conflicts->assign(nlayers, {});
  }

  bool prl_ok = true;
  bool same_net_ok = true;
  for (int lidx = 0; lidx < static_cast<int>(nlayers); ++lidx) {
    const auto& new_all = new_all_metal_by_layer[lidx];
    if (new_all.empty()) {
      continue;
    }

    // Keep newly routed metal clear of the candidate cell's own PG pins and OBS.
    const auto& new_stub = new_stub_metal_by_layer[lidx];
    if (!new_stub.empty()) {
      std::vector<idb::IdbRect> merged_stub = mergeMaximalRects(new_stub);
      if (merged_stub.empty()) {
        merged_stub = new_stub;
      }
      for (const idb::IdbRect& sr : merged_stub) {
        for (const LayerShapeBox& iob : geom.internal_obs) {
          if (iob.layer_idx == lidx &&
              !rectClearOfObstacle(sr, iob.rect, requiredSpacing(lidx, sr, iob.rect, iob.min_spacing))) {
            return false;
          }
        }
      }
    }

    std::vector<idb::IdbRect> layer_conflicts;
    if (!checkNetMetalClear(lidx, new_all, target_net, out_prl_conflicts ? &layer_conflicts : nullptr)) {
      if (out_prl_conflicts == nullptr) {
        return false;
      }
      prl_ok = false;
      auto& dst = (*out_prl_conflicts)[lidx];
      dst.insert(dst.end(), layer_conflicts.begin(), layer_conflicts.end());
    }

    std::vector<idb::IdbRect> layer_sn_conflicts;
    if (!checkNetPolygonDrcWithNearby(lidx, new_all, target_net,
                                      out_same_net_conflicts ? &layer_sn_conflicts : nullptr)) {
      if (out_same_net_conflicts == nullptr) {
        return false;
      }
      same_net_ok = false;
      auto& dst = (*out_same_net_conflicts)[lidx];
      dst.insert(dst.end(), layer_sn_conflicts.begin(), layer_sn_conflicts.end());
    }
  }

  if (!new_cuts.empty()) {
    std::vector<CutShapeBox> route_cuts;
    route_cuts.reserve(new_cuts.size());
    for (const PendingRouteCut& cs : new_cuts) {
      route_cuts.push_back({cs.cut_order, cs.rect});
    }
    std::vector<CutShapeBox> foreign_cell_cuts = geom.obs_cut_shapes;
    for (const PGCutShapeBox& pgc : geom.pg_cut_shapes) {
      foreign_cell_cuts.push_back({pgc.cut_order, pgc.rect});
    }
    if (!checkCutSetValid(route_cuts, target_net, nullptr, &geom.all_signal_cut_shapes, &foreign_cell_cuts)) {
      return false;
    }
  }

  if (!prl_ok || !same_net_ok) {
    return false;
  }

  bool area_ok = true;
  for (int lidx = 0; lidx < static_cast<int>(nlayers); ++lidx) {
    const auto& new_all = new_all_metal_by_layer[lidx];
    if (new_all.empty()) {
      continue;
    }
    if (!checkNetMinArea(lidx, new_all, target_net, out_min_area_deficits)) {
      if (out_min_area_deficits == nullptr) {
        return false;
      }
      area_ok = false;
    }
  }
  return area_ok;
}

std::optional<RoutePlan> LocalStubRouter::tryPatchMinArea(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom,
                                                          const RoutePlan& base_plan,
                                                          const std::vector<MinAreaDeficit>& initial_deficits) const
{
  RoutePlan current = base_plan;
  std::vector<MinAreaDeficit> deficits = initial_deficits;
  const size_t max_iters = initial_deficits.size() + 4;
  for (size_t iter = 0; iter < max_iters && !deficits.empty(); ++iter) {
    const MinAreaDeficit d = deficits.front();
    const int lidx = d.layer_idx;
    if (lidx < 0 || lidx >= static_cast<int>(_layers.size())) {
      return std::nullopt;
    }
    const RoutingLayerInfo& info = _layers[lidx];

    std::vector<std::pair<int32_t, int32_t>> anchors;
    auto addAnchor = [&](int32_t x, int32_t y) {
      if (!pointInsideRect(x, y, d.poly_bbox) || !coordOnLayerMfgGrid(lidx, x) || !coordOnLayerMfgGrid(lidx, y)) {
        return;
      }
      for (const auto& ex : anchors) {
        if (ex.first == x && ex.second == y) {
          return;
        }
      }
      anchors.emplace_back(x, y);
    };
    for (const StubMove& mv : current.moves) {
      if (mv.is_via && (mv.layer_idx == lidx || mv.next_layer_idx == lidx)) {
        addAnchor(mv.x1, mv.y1);
      } else if (!mv.is_via && mv.layer_idx == lidx) {
        addAnchor(mv.x1, mv.y1);
        addAnchor(mv.x2, mv.y2);
      }
    }
    if (anchors.empty()) {
      return std::nullopt;
    }

    const int64_t deficit_area = std::max<int64_t>(1, d.min_area - d.cur_area);
    const int32_t need_len = static_cast<int32_t>(
        std::min<int64_t>(std::numeric_limits<int32_t>::max() / 2, ceilDiv(deficit_area, info.width)));
    const int32_t half_need = static_cast<int32_t>(ceilDiv(need_len, 2));

    const int32_t ext_lo = info.has_custom_wire_ext ? info.end_ext : info.half_low;
    const int32_t ext_hi = info.has_custom_wire_ext ? info.end_ext : info.half_high;
    bool progressed = false;
    for (const auto& [ax, ay] : anchors) {
      const StubMove plus_y{false, lidx, lidx, ax, ay, ax, snapCoordUp(lidx, d.poly_bbox.get_high_y() + need_len - ext_hi)};
      const StubMove minus_y{false, lidx, lidx, ax, ay, ax, snapCoordDown(lidx, d.poly_bbox.get_low_y() - need_len + ext_lo)};
      const StubMove two_y{false, lidx, lidx, ax,
                           snapCoordDown(lidx, d.poly_bbox.get_low_y() - half_need + ext_lo), ax,
                           snapCoordUp(lidx, d.poly_bbox.get_high_y() + half_need - ext_hi)};
      const StubMove plus_x{false, lidx, lidx, ax, ay, snapCoordUp(lidx, d.poly_bbox.get_high_x() + need_len - ext_hi), ay};
      const StubMove minus_x{false, lidx, lidx, ax, ay, snapCoordDown(lidx, d.poly_bbox.get_low_x() - need_len + ext_lo), ay};
      const StubMove two_x{false, lidx, lidx,
                           snapCoordDown(lidx, d.poly_bbox.get_low_x() - half_need + ext_lo), ay,
                           snapCoordUp(lidx, d.poly_bbox.get_high_x() + half_need - ext_hi), ay};

      std::vector<StubMove> candidates;
      if (info.is_vertical && !info.is_horizontal) {
        candidates = {plus_y, minus_y, two_y, plus_x, minus_x, two_x};
      } else {
        candidates = {plus_x, minus_x, two_x, plus_y, minus_y, two_y};
      }

      for (const StubMove& patch_mv : candidates) {
        if (patch_mv.x1 == patch_mv.x2 && patch_mv.y1 == patch_mv.y2) {
          continue;
        }
        const idb::IdbRect wbox = wireBox(lidx, patch_mv.x1, patch_mv.y1, patch_mv.x2, patch_mv.y2);
        if (!rectInsideDie(wbox) || !rectOnMfgGrid(wbox)) {
          continue;
        }
        RoutePlan trial = current;
        trial.moves.push_back(patch_mv);
        std::vector<MinAreaDeficit> next_deficits;
        if (validateProposedRouteImpl(target_net, geom, trial, nullptr, nullptr, &next_deficits)) {
          return trial;
        }
        if (!next_deficits.empty() && next_deficits.size() < deficits.size()) {
          current = std::move(trial);
          deficits = std::move(next_deficits);
          progressed = true;
          break;
        }
      }
      if (progressed) {
        break;
      }
    }
    if (!progressed) {
      return std::nullopt;
    }
  }
  if (validateProposedRouteImpl(target_net, geom, current, nullptr, nullptr, nullptr)) {
    return current;
  }
  return std::nullopt;
}

std::vector<ecc::geometry::ShapeId> LocalStubRouter::addCommittedSpecialNetShapes(
    idb::IdbSpecialNet* snet, const std::vector<LayerShapeBox>& metal_shapes, const std::vector<CutShapeBox>& cut_shapes)
{
  std::vector<ecc::geometry::ShapeId> ids;
  if (!snet) {
    return ids;
  }
  auto it = _special_net_owner_id.find(snet);
  if (it == _special_net_owner_id.end()) {
    const ecc::geometry::OwnerId snet_id = static_cast<ecc::geometry::OwnerId>(_special_net_owner_id.size());
    it = _special_net_owner_id.emplace(snet, snet_id).first;
    _owner_to_special_net[{ecc::geometry::OwnerType::kSpecialWireSegment, snet_id}] = snet;
    _owner_to_special_net[{ecc::geometry::OwnerType::kVia,
                           makeDerivedOwnerId(ecc::geometry::OwnerType::kSpecialWireSegment, snet_id)}] = snet;
  }
  const ecc::geometry::OwnerId snet_owner_id = it->second;

  for (const LayerShapeBox& sh : metal_shapes) {
    if (sh.layer_idx < 0 || sh.layer_idx >= static_cast<int>(_layers.size())) {
      continue;
    }
    ecc::geometry::OwnerRef owner;
    owner.type = ecc::geometry::OwnerType::kSpecialWireSegment;
    owner.owner_id = snet_owner_id;
    owner.path0 = _next_commit_path++;
    const ecc::geometry::ShapeId sid =
        _store.add_rect(_layers[sh.layer_idx].geom_layer_id, rect32FromIdb(sh.rect), owner);
    if (sid != 0) {
      ids.push_back(sid);
    }
  }
  for (const CutShapeBox& cs : cut_shapes) {
    const ecc::geometry::LayerId cut_lid = geomCutLayerIdForOrder(cs.cut_order);
    if (cut_lid == 0) {
      continue;
    }
    ecc::geometry::OwnerRef owner;
    owner.type = ecc::geometry::OwnerType::kVia;
    owner.owner_id = makeDerivedOwnerId(ecc::geometry::OwnerType::kSpecialWireSegment, snet_owner_id);
    owner.path0 = _next_commit_path++;
    const ecc::geometry::ShapeId sid = _store.add_rect(cut_lid, rect32FromIdb(cs.rect), owner);
    if (sid != 0) {
      ids.push_back(sid);
    }
  }
  return ids;
}

std::vector<ecc::geometry::ShapeId> LocalStubRouter::addCommittedCutObstacles(
    const std::vector<CutShapeBox>& cut_obs_shapes)
{
  std::vector<ecc::geometry::ShapeId> ids;
  for (const CutShapeBox& obs : cut_obs_shapes) {
    const ecc::geometry::LayerId cut_lid = geomCutLayerIdForOrder(obs.cut_order);
    if (cut_lid == 0) {
      continue;
    }
    ecc::geometry::OwnerRef owner;
    owner.type = ecc::geometry::OwnerType::kObs;
    owner.owner_id = _next_commit_path;
    owner.path0 = _next_commit_path++;
    const ecc::geometry::ShapeId sid = _store.add_rect(cut_lid, rect32FromIdb(obs.rect), owner);
    if (sid != 0) {
      ids.push_back(sid);
    }
  }
  return ids;
}

std::vector<ecc::geometry::ShapeId> LocalStubRouter::addCommittedNetShapes(
    idb::IdbNet* net, const std::vector<LayerShapeBox>& metal_shapes,
    const std::vector<std::pair<ecc::geometry::LayerId, idb::IdbRect>>& cut_shapes)
{
  std::vector<ecc::geometry::ShapeId> ids;
  if (!net) {
    return ids;
  }
  auto it = _net_owner_id.find(net);
  if (it == _net_owner_id.end()) {
    const ecc::geometry::OwnerId net_id =
        net->get_id() != 0 ? static_cast<ecc::geometry::OwnerId>(net->get_id())
                           : static_cast<ecc::geometry::OwnerId>(_net_owner_id.size() + 1);
    it = _net_owner_id.emplace(net, net_id).first;
    auto addNetOwner = [&](ecc::geometry::OwnerType owner_type, ecc::geometry::OwnerId owner_id) {
      const ecc::geometry::OwnerIndexKey key{owner_type, owner_id};
      if (_owner_to_net.emplace(key, net).second) {
        _net_owners[net].push_back(key);
      }
    };
    addNetOwner(ecc::geometry::OwnerType::kNetWireSegment, net_id);
    addNetOwner(ecc::geometry::OwnerType::kVia, makeDerivedOwnerId(ecc::geometry::OwnerType::kNetWireSegment, net_id));
  }
  const ecc::geometry::OwnerId net_owner_id = it->second;

  for (const LayerShapeBox& sh : metal_shapes) {
    if (sh.layer_idx < 0 || sh.layer_idx >= static_cast<int>(_layers.size())) {
      continue;
    }
    _merged_net_cache.erase({net, sh.layer_idx});
    ecc::geometry::OwnerRef owner;
    owner.type = ecc::geometry::OwnerType::kNetWireSegment;
    owner.owner_id = net_owner_id;
    owner.path0 = _next_commit_path++;
    const ecc::geometry::ShapeId sid = _store.add_rect(_layers[sh.layer_idx].geom_layer_id, rect32FromIdb(sh.rect), owner);
    if (sid != 0) {
      ids.push_back(sid);
    }
  }
  for (const auto& [cut_lid, rect] : cut_shapes) {
    if (cut_lid == 0) {
      continue;
    }
    ecc::geometry::OwnerRef owner;
    owner.type = ecc::geometry::OwnerType::kVia;
    owner.owner_id = makeDerivedOwnerId(ecc::geometry::OwnerType::kNetWireSegment, net_owner_id);
    owner.path0 = _next_commit_path++;
    const ecc::geometry::ShapeId sid = _store.add_rect(cut_lid, rect32FromIdb(rect), owner);
    if (sid != 0) {
      ids.push_back(sid);
    }
  }
  return ids;
}

std::vector<ecc::geometry::ShapeId> LocalStubRouter::addCommittedObstacles(const std::vector<LayerShapeBox>& obs_shapes)
{
  std::vector<ecc::geometry::ShapeId> ids;
  for (const LayerShapeBox& obs : obs_shapes) {
    if (obs.layer_idx < 0 || obs.layer_idx >= static_cast<int>(_layers.size())) {
      continue;
    }
    ecc::geometry::OwnerRef owner;
    owner.type = ecc::geometry::OwnerType::kObs;
    owner.owner_id = _next_commit_path;
    owner.path0 = _next_commit_path++;
    const ecc::geometry::ShapeId sid =
        _store.add_rect(_layers[obs.layer_idx].geom_layer_id, rect32FromIdb(obs.rect), owner);
    if (sid != 0) {
      if (obs.min_spacing > 0) {
        _committed_obs_spacing[sid] = obs.min_spacing;
      }
      ids.push_back(sid);
    }
  }
  return ids;
}

void LocalStubRouter::removeCommittedShapes(const std::vector<ecc::geometry::ShapeId>& shape_ids)
{
  for (ecc::geometry::ShapeId sid : shape_ids) {
    if (sid == 0) {
      continue;
    }
    _committed_obs_spacing.erase(sid);
    const ecc::geometry::ShapeRecord* rec = _store.find_shape(sid);
    const ecc::geometry::OwnerRef owner = _store.owner_of(sid);
    if (auto net_it = _owner_to_net.find({owner.type, owner.owner_id});
        net_it != _owner_to_net.end() && net_it->second != nullptr) {
      idb::IdbNet* net = net_it->second;
      if (rec != nullptr) {
        for (int l = 0; l < static_cast<int>(_layers.size()); ++l) {
          if (_layers[l].geom_layer_id == rec->layer_id) {
            _merged_net_cache.erase({net, l});
            break;
          }
        }
      } else {
        for (int l = 0; l < static_cast<int>(_layers.size()); ++l) {
          _merged_net_cache.erase({net, l});
        }
      }
    }
    _store.delete_shape(sid);
  }
}

RoutePlan LocalStubRouter::routeStub(idb::IdbNet* target_net, int max_route_order,
                                     const CandidateDiodeGeometry& geom,
                                     const std::vector<LayerShapeBox>& goal_shapes,
                                     int32_t search_radius_dbu) const
{
  RoutePlan failed;
  const int max_layer_idx = maxLayerIdxForOrder(max_route_order);
  if (max_layer_idx < 0 || geom.start_shapes.empty() || goal_shapes.empty()) {
    return failed;
  }

  std::vector<LayerShapeBox> filtered_starts;
  for (const LayerShapeBox& ss : geom.start_shapes) {
    if (ss.layer_idx >= 0 && ss.layer_idx <= max_layer_idx) {
      filtered_starts.push_back(ss);
    }
  }
  if (filtered_starts.empty()) {
    return failed;
  }

  const int lidx0 = filtered_starts[0].layer_idx;
  const idb::IdbRect& srect0 = filtered_starts[0].rect;
  const int32_t mid_x0 = (srect0.get_low_x() + srect0.get_high_x()) / 2;
  const int32_t mid_y0 = (srect0.get_low_y() + srect0.get_high_y()) / 2;
  auto snapAnchor = [&](int32_t target, int32_t lo, int32_t hi) -> int32_t {
    const int32_t dn = snapCoordDown(lidx0, target);
    const int32_t up = snapCoordUp(lidx0, target);
    const bool dn_ok = (dn >= lo && dn <= hi && coordOnLayerMfgGrid(lidx0, dn));
    const bool up_ok = (up >= lo && up <= hi && coordOnLayerMfgGrid(lidx0, up));
    if (dn_ok && up_ok) {
      return (std::abs(static_cast<int64_t>(target) - dn) <= std::abs(static_cast<int64_t>(up) - target)) ? dn : up;
    }
    if (dn_ok) {
      return dn;
    }
    if (up_ok) {
      return up;
    }
    return target;
  };
  const int32_t sx0 = snapAnchor(mid_x0, srect0.get_low_x(), srect0.get_high_x());
  const int32_t sy0 = snapAnchor(mid_y0, srect0.get_low_y(), srect0.get_high_y());

  std::map<int, std::vector<idb::IdbRect>> goals_by_layer;
  for (const LayerShapeBox& gs : goal_shapes) {
    if (gs.layer_idx >= 0 && gs.layer_idx <= max_layer_idx) {
      goals_by_layer[gs.layer_idx].push_back(gs.rect);
    }
  }
  std::vector<std::pair<int64_t, LayerShapeBox>> ranked_goals;
  for (const auto& [lidx, raw_rects] : goals_by_layer) {
    std::vector<idb::IdbRect> merged_goals = mergeMaximalRects(raw_rects);
    if (merged_goals.empty()) {
      merged_goals = raw_rects;
    }
    for (const idb::IdbRect& mr : merged_goals) {
      const int32_t cx = std::clamp(sx0, mr.get_low_x(), mr.get_high_x());
      const int32_t cy = std::clamp(sy0, mr.get_low_y(), mr.get_high_y());
      const int64_t dist = static_cast<int64_t>(std::abs(cx - sx0)) + std::abs(cy - sy0);
      ranked_goals.push_back({dist, {lidx, mr}});
    }
  }
  if (ranked_goals.empty()) {
    return failed;
  }
  std::sort(ranked_goals.begin(), ranked_goals.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first < b.first;
    }
    return std::make_tuple(a.second.layer_idx, a.second.rect.get_low_x(), a.second.rect.get_low_y(),
                           a.second.rect.get_high_x(), a.second.rect.get_high_y()) <
           std::make_tuple(b.second.layer_idx, b.second.rect.get_low_x(), b.second.rect.get_low_y(),
                           b.second.rect.get_high_x(), b.second.rect.get_high_y());
  });
  if (ranked_goals.size() > 6) {
    ranked_goals.resize(6);
  }

  const int num_active_layers = max_layer_idx + 1;
  int32_t min_px = _layers[0].pitch_x;
  int32_t min_py = _layers[0].pitch_y;
  for (int l = 0; l < num_active_layers; ++l) {
    min_px = std::min(min_px, _layers[l].pitch_x);
    min_py = std::min(min_py, _layers[l].pitch_y);
  }
  const int32_t pad = std::max<int32_t>(std::max(min_px, min_py) * 4, search_radius_dbu / 4);

  int32_t win_lx = sx0, win_hx = sx0, win_ly = sy0, win_hy = sy0;
  for (const LayerShapeBox& ss : filtered_starts) {
    win_lx = std::min(win_lx, ss.rect.get_low_x());
    win_hx = std::max(win_hx, ss.rect.get_high_x());
    win_ly = std::min(win_ly, ss.rect.get_low_y());
    win_hy = std::max(win_hy, ss.rect.get_high_y());
  }
  for (const auto& [d, gs] : ranked_goals) {
    (void) d;
    const int32_t cx = std::clamp(sx0, gs.rect.get_low_x(), gs.rect.get_high_x());
    const int32_t cy = std::clamp(sy0, gs.rect.get_low_y(), gs.rect.get_high_y());
    win_lx = std::min(win_lx, cx);
    win_hx = std::max(win_hx, cx);
    win_ly = std::min(win_ly, cy);
    win_hy = std::max(win_hy, cy);
  }
  win_lx -= pad;
  win_hx += pad;
  win_ly -= pad;
  win_hy += pad;
  if (_has_die) {
    win_lx = std::max(win_lx, _die_bbox.get_low_x());
    win_hx = std::min(win_hx, _die_bbox.get_high_x());
    win_ly = std::max(win_ly, _die_bbox.get_low_y());
    win_hy = std::min(win_hy, _die_bbox.get_high_y());
    if (win_lx > win_hx || win_ly > win_hy) {
      return failed;
    }
  }
  const idb::IdbRect win_rect(win_lx, win_ly, win_hx, win_hy);

  // R-tree query of routing and cut obstacles in the local routing window (merged per foreign owner).
  std::vector<std::vector<RoutingObstacleBox>> obs_by_layer(num_active_layers);
  for (int l = 0; l < num_active_layers; ++l) {
    const int32_t margin = _layers[l].half_high + _layers[l].max_spacing + 2 * _layers[l].width;
    const idb::IdbRect qrect(win_lx - margin, win_ly - margin, win_hx + margin, win_hy + margin);
    obs_by_layer[l] = queryMergedRoutingObstacles(l, qrect, target_net, nullptr);
  }
  for (const LayerShapeBox& ts : geom.internal_obs) {
    if (ts.layer_idx >= 0 && ts.layer_idx < num_active_layers) {
      const int32_t margin =
          _layers[ts.layer_idx].half_high + std::max(_layers[ts.layer_idx].max_spacing, ts.min_spacing) +
          2 * _layers[ts.layer_idx].width;
      if (!rectClearOfObstacle(ts.rect, win_rect, margin)) {
        obs_by_layer[ts.layer_idx].push_back({ts.rect, ts.min_spacing, 0, false});
      }
    }
  }

  std::vector<std::vector<CutObstacle>> cut_obs_by_low_idx(num_active_layers);
  for (int l = 0; l + 1 < num_active_layers; ++l) {
    const ViaOption* vo = viaBetween(l, l + 1);
    if (!vo || vo->cut_geom_layer_id == 0 || vo->cut_rects.empty()) {
      continue;
    }
    const int32_t diff_sp = std::max<int32_t>(0, vo->cut_spacing);
    const int32_t same_sp = std::max<int32_t>(0, vo->same_net_cut_spacing);
    int32_t max_sp = std::max(diff_sp, same_sp);
    if (vo->cut_layer != nullptr) {
      if (auto it = _order_to_cut_spacing.find(static_cast<int>(vo->cut_layer->get_order()));
          it != _order_to_cut_spacing.end()) {
        max_sp = std::max(max_sp, it->second.max_spacing);
      }
    }
    int32_t cut_ext = max_sp;
    for (const idb::IdbRect& cr : vo->cut_rects) {
      cut_ext = std::max({cut_ext, std::abs(cr.get_low_x()), std::abs(cr.get_high_x()),
                          std::abs(cr.get_low_y()), std::abs(cr.get_high_y())});
    }
    const int32_t margin = cut_ext + max_sp;
    const ecc::geometry::Rect32 query_box{win_lx - margin, win_ly - margin, win_hx + margin, win_hy + margin};
    for (ecc::geometry::ShapeId sid : _store.query_intersect(vo->cut_geom_layer_id, query_box)) {
      const ecc::geometry::ShapeRecord* rec = _store.find_shape(sid);
      if (!rec || rec->state != ecc::geometry::ShapeState::kAlive || rec->kind != ecc::geometry::ShapeKind::kRect) {
        continue;
      }
      const ecc::geometry::OwnerRef owner = _store.owner_of(sid);
      if (!isPhysicalObstacleOwner(owner.type)) {
        continue;
      }
      const ecc::geometry::OwnerIndexKey okey{owner.type, owner.owner_id};
      const auto net_it = _owner_to_net.find(okey);
      const bool is_same_net = (target_net != nullptr && net_it != _owner_to_net.end() && net_it->second == target_net);
      cut_obs_by_low_idx[l].push_back({idbRectFromRect32(rec->bbox), is_same_net ? same_sp : diff_sp});
    }
    if (vo->cut_layer != nullptr) {
      const int cut_order = static_cast<int>(vo->cut_layer->get_order());
      for (const CutShapeBox& sc : geom.all_signal_cut_shapes) {
        if (sc.cut_order == cut_order && !rectClearOfObstacle(sc.rect, win_rect, margin)) {
          cut_obs_by_low_idx[l].push_back({sc.rect, same_sp});
        }
      }
      for (const PGCutShapeBox& pgc : geom.pg_cut_shapes) {
        if (pgc.cut_order == cut_order && !rectClearOfObstacle(pgc.rect, win_rect, margin)) {
          cut_obs_by_low_idx[l].push_back({pgc.rect, diff_sp});
        }
      }
      for (const CutShapeBox& obc : geom.obs_cut_shapes) {
        if (obc.cut_order == cut_order && !rectClearOfObstacle(obc.rect, win_rect, margin)) {
          cut_obs_by_low_idx[l].push_back({obc.rect, diff_sp});
        }
      }
    }
  }

  StubMazeProblem problem;
  problem.router = this;
  problem.target_net = target_net;
  problem.max_layer_idx = max_layer_idx;
  problem.sx0 = sx0;
  problem.sy0 = sy0;
  problem.win_rect = win_rect;
  problem.filtered_starts = &filtered_starts;
  problem.ranked_goals = &ranked_goals;
  problem.obs_by_layer = &obs_by_layer;
  problem.cut_obs_by_low_idx = &cut_obs_by_low_idx;
  problem.base_cuts = &geom.all_signal_cut_shapes;

  RoutePlan plan = runStubMazeSearch(problem);
  if (!plan.found) {
    return failed;
  }
  std::vector<std::vector<idb::IdbRect>> prl_conflicts;
  std::vector<std::vector<idb::IdbRect>> same_net_conflicts;
  std::vector<MinAreaDeficit> area_deficits;
  if (validateProposedRouteImpl(target_net, geom, plan, &prl_conflicts, &same_net_conflicts, &area_deficits)) {
    return plan;
  }
  bool has_prl_conflicts = false;
  for (int l = 0; l < num_active_layers && l < static_cast<int>(prl_conflicts.size()); ++l) {
    if (!prl_conflicts[l].empty()) {
      has_prl_conflicts = true;
      break;
    }
  }
  bool has_same_net_conflicts = false;
  for (int l = 0; l < num_active_layers && l < static_cast<int>(same_net_conflicts.size()); ++l) {
    if (!same_net_conflicts[l].empty()) {
      has_same_net_conflicts = true;
      break;
    }
  }
  if (!has_prl_conflicts && !has_same_net_conflicts) {
    if (!area_deficits.empty()) {
      if (auto patched = tryPatchMinArea(target_net, geom, plan, area_deficits); patched.has_value()) {
        return *patched;
      }
    }
    return failed;
  }

  problem.force_max_spacing_obs = has_prl_conflicts ? &prl_conflicts : nullptr;
  problem.force_avoid_obs = has_same_net_conflicts ? &same_net_conflicts : nullptr;
  RoutePlan rerouted = runStubMazeSearch(problem);
  if (rerouted.found) {
    std::vector<MinAreaDeficit> rerouted_deficits;
    if (validateProposedRouteImpl(target_net, geom, rerouted, nullptr, nullptr, &rerouted_deficits)) {
      return rerouted;
    }
    if (!rerouted_deficits.empty()) {
      if (auto patched = tryPatchMinArea(target_net, geom, rerouted, rerouted_deficits); patched.has_value()) {
        return *patched;
      }
    }
  }
  return failed;
}

}  // namespace imj
