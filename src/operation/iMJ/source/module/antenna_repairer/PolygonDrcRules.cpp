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
#include "PolygonDrcRules.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace gtl = boost::polygon;
using namespace boost::polygon::operators;

namespace imj {

namespace {

inline int32_t ringIdx(int32_t idx, int32_t n)
{
  const int32_t r = idx % n;
  return r < 0 ? r + n : r;
}

inline gtl::rectangle_data<int64_t> toGtlRect(const idb::IdbRect& r)
{
  return gtl::rectangle_data<int64_t>(r.get_low_x(), r.get_low_y(), r.get_high_x(), r.get_high_y());
}

inline idb::IdbRect toIdbRect(const gtl::rectangle_data<int64_t>& r)
{
  return idb::IdbRect(static_cast<int32_t>(gtl::xl(r)), static_cast<int32_t>(gtl::yl(r)),
                      static_cast<int32_t>(gtl::xh(r)), static_cast<int32_t>(gtl::yh(r)));
}

inline bool rectsOpenOverlap(const idb::IdbRect& a, const idb::IdbRect& b)
{
  return a.get_low_x() < b.get_high_x() && a.get_high_x() > b.get_low_x() && a.get_low_y() < b.get_high_y() &&
         a.get_high_y() > b.get_low_y();
}

inline bool rectsClosedOverlap(const idb::IdbRect& a, const idb::IdbRect& b)
{
  return a.get_low_x() <= b.get_high_x() && a.get_high_x() >= b.get_low_x() && a.get_low_y() <= b.get_high_y() &&
         a.get_high_y() >= b.get_low_y();
}

inline bool rectContains(const idb::IdbRect& outer, const idb::IdbRect& inner)
{
  return inner.get_low_x() >= outer.get_low_x() && inner.get_high_x() <= outer.get_high_x() &&
         inner.get_low_y() >= outer.get_low_y() && inner.get_high_y() <= outer.get_high_y();
}

inline int64_t rectEuclidDistSq(const idb::IdbRect& a, const idb::IdbRect& b)
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

inline bool distLessThan(const idb::IdbRect& a, const idb::IdbRect& b, int32_t req)
{
  if (req <= 0) {
    return false;
  }
  const int64_t r64 = req;
  return rectEuclidDistSq(a, b) < r64 * r64;
}

inline idb::IdbRect edgeAsRect(const DrcEdge& e)
{
  return idb::IdbRect(std::min(e.x1, e.x2), std::min(e.y1, e.y2), std::max(e.x1, e.x2), std::max(e.y1, e.y2));
}

inline idb::IdbRect computeGapRect(const idb::IdbRect& a, const idb::IdbRect& b)
{
  int32_t lx = 0;
  int32_t hx = 0;
  if (a.get_high_x() <= b.get_low_x()) {
    lx = a.get_high_x();
    hx = b.get_low_x();
  } else if (b.get_high_x() <= a.get_low_x()) {
    lx = b.get_high_x();
    hx = a.get_low_x();
  } else {
    lx = std::max(a.get_low_x(), b.get_low_x());
    hx = std::min(a.get_high_x(), b.get_high_x());
  }
  int32_t ly = 0;
  int32_t hy = 0;
  if (a.get_high_y() <= b.get_low_y()) {
    ly = a.get_high_y();
    hy = b.get_low_y();
  } else if (b.get_high_y() <= a.get_low_y()) {
    ly = b.get_high_y();
    hy = a.get_low_y();
  } else {
    ly = std::max(a.get_low_y(), b.get_low_y());
    hy = std::min(a.get_high_y(), b.get_high_y());
  }
  return idb::IdbRect(lx, ly, hx, hy);
}

DrcRing buildCleanRing(std::vector<DrcPoint> pts, bool is_hole)
{
  DrcRing ring;
  ring.is_hole = is_hole;
  if (pts.size() < 3) {
    return ring;
  }

  bool changed = true;
  while (changed && pts.size() >= 3) {
    changed = false;
    std::vector<DrcPoint> next;
    next.reserve(pts.size());
    const int32_t n = static_cast<int32_t>(pts.size());
    for (int32_t i = 0; i < n; ++i) {
      const DrcPoint& prev = pts[ringIdx(i - 1, n)];
      const DrcPoint& curr = pts[i];
      const DrcPoint& succ = pts[ringIdx(i + 1, n)];
      if (curr.x == prev.x && curr.y == prev.y) {
        changed = true;
        continue;
      }
      const int64_t dx1 = static_cast<int64_t>(curr.x) - prev.x;
      const int64_t dy1 = static_cast<int64_t>(curr.y) - prev.y;
      const int64_t dx2 = static_cast<int64_t>(succ.x) - curr.x;
      const int64_t dy2 = static_cast<int64_t>(succ.y) - curr.y;
      if ((dx1 == 0 && dx2 == 0) || (dy1 == 0 && dy2 == 0)) {
        changed = true;
        continue;
      }
      next.push_back(curr);
    }
    pts.swap(next);
  }

  if (pts.size() < 4) {
    return ring;
  }

  int64_t signed_area2 = 0;
  const size_t n = pts.size();
  for (size_t i = 0; i < n; ++i) {
    const DrcPoint& a = pts[i];
    const DrcPoint& b = pts[(i + 1) % n];
    signed_area2 += static_cast<int64_t>(a.x) * b.y - static_cast<int64_t>(b.x) * a.y;
  }
  const bool want_ccw = !is_hole;
  if ((want_ccw && signed_area2 < 0) || (!want_ccw && signed_area2 > 0)) {
    std::reverse(pts.begin(), pts.end());
  }

  ring.vertices = std::move(pts);
  const int32_t vn = static_cast<int32_t>(ring.vertices.size());
  ring.edges.resize(static_cast<size_t>(vn));
  for (int32_t i = 0; i < vn; ++i) {
    const DrcPoint& a = ring.vertices[i];
    const DrcPoint& b = ring.vertices[ringIdx(i + 1, vn)];
    const int64_t dx = static_cast<int64_t>(b.x) - a.x;
    const int64_t dy = static_cast<int64_t>(b.y) - a.y;
    DrcEdge& e = ring.edges[static_cast<size_t>(i)];
    e.x1 = a.x;
    e.y1 = a.y;
    e.x2 = b.x;
    e.y2 = b.y;
    e.nx = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    e.ny = dx > 0 ? -1 : (dx < 0 ? 1 : 0);
    e.len = std::abs(dx) + std::abs(dy);
  }
  for (int32_t i = 0; i < vn; ++i) {
    const DrcEdge& ep = ring.edges[static_cast<size_t>(ringIdx(i - 1, vn))];
    DrcEdge& ec = ring.edges[static_cast<size_t>(i)];
    const DrcEdge& en = ring.edges[static_cast<size_t>(ringIdx(i + 1, vn))];
    const int64_t cross_start =
        static_cast<int64_t>(ep.x2 - ep.x1) * (ec.y2 - ec.y1) - static_cast<int64_t>(ep.y2 - ep.y1) * (ec.x2 - ec.x1);
    const int64_t cross_end =
        static_cast<int64_t>(ec.x2 - ec.x1) * (en.y2 - en.y1) - static_cast<int64_t>(ec.y2 - ec.y1) * (en.x2 - en.x1);
    ec.convex_start = cross_start > 0;
    ec.convex_end = cross_end > 0;
  }
  return ring;
}

template <typename PtRange>
std::vector<DrcPoint> toDrcPoints(PtRange begin_it, PtRange end_it)
{
  std::vector<DrcPoint> pts;
  for (auto it = begin_it; it != end_it; ++it) {
    pts.push_back({static_cast<int32_t>(gtl::x(*it)), static_cast<int32_t>(gtl::y(*it))});
  }
  return pts;
}

bool touchesAnyRect(const idb::IdbRect& a, const std::vector<idb::IdbRect>& rects)
{
  for (const auto& r : rects) {
    if (rectsClosedOverlap(a, r)) {
      return true;
    }
  }
  return false;
}

bool boxContainsAny(const std::vector<idb::IdbRect>& rects, const idb::IdbRect& box)
{
  for (const auto& r : rects) {
    if (rectContains(r, box)) {
      return true;
    }
  }
  return false;
}

bool boxPresentExactly(const std::vector<idb::IdbRect>& rects, const idb::IdbRect& box)
{
  for (const auto& r : rects) {
    if (r.get_low_x() == box.get_low_x() && r.get_low_y() == box.get_low_y() && r.get_high_x() == box.get_high_x() &&
        r.get_high_y() == box.get_high_y()) {
      return true;
    }
  }
  return false;
}

idb::IdbRect expandAlongNormal(const idb::IdbRect& base, int8_t nx, int8_t ny, int32_t dist)
{
  const int32_t lx = base.get_low_x() + (nx < 0 ? -dist : 0);
  const int32_t ly = base.get_low_y() + (ny < 0 ? -dist : 0);
  const int32_t hx = base.get_high_x() + (nx > 0 ? dist : 0);
  const int32_t hy = base.get_high_y() + (ny > 0 ? dist : 0);
  return idb::IdbRect(lx, ly, hx, hy);
}

idb::IdbRect buildParallelTriggerBox(const DrcEdge& eol, const DrcEdge& adj, int32_t par_space, int32_t par_within,
                                     int32_t eol_within)
{
  const bool horiz_eol = (eol.ny != 0);
  if (horiz_eol) {
    const int32_t ey = eol.y1;
    const int32_t y_back = ey - static_cast<int32_t>(eol.ny) * par_within;
    const int32_t y_fwd = ey + static_cast<int32_t>(eol.ny) * eol_within;
    const int32_t ax = adj.x1;
    const int32_t ax_out = ax + static_cast<int32_t>(adj.nx) * par_space;
    return idb::IdbRect(std::min(ax, ax_out), std::min(y_back, y_fwd), std::max(ax, ax_out), std::max(y_back, y_fwd));
  }
  const int32_t ex = eol.x1;
  const int32_t x_back = ex - static_cast<int32_t>(eol.nx) * par_within;
  const int32_t x_fwd = ex + static_cast<int32_t>(eol.nx) * eol_within;
  const int32_t ay = adj.y1;
  const int32_t ay_out = ay + static_cast<int32_t>(adj.ny) * par_space;
  return idb::IdbRect(std::min(x_back, x_fwd), std::min(ay, ay_out), std::max(x_back, x_fwd), std::max(ay, ay_out));
}

bool isEolEdgeRule(const DrcEdge& e, const EolSpacingRule& rule)
{
  if (!e.convex_start || !e.convex_end) {
    return false;
  }
  if (rule.exact_width) {
    return e.len == rule.eol_width;
  }
  return e.len < rule.eol_width;
}

struct EolSceneRect
{
  idb::IdbRect rect;
  int poly_id = -1;
};

struct EolSceneEdge
{
  DrcEdge edge;
  idb::IdbRect edge_rect;
  int poly_id = -1;
};

struct EolScene
{
  std::vector<EolSceneRect> solid_rects;
  std::vector<EolSceneEdge> edges;
  std::vector<std::vector<bool>> poly_has_opposing_eol;  // unused directly; we test ETE per edge
};

bool hasParallelTrigger(const EolSceneEdge& eol_se, const DrcEdge& adj, const EolSpacingRule& rule,
                        const std::vector<EolSceneEdge>& all_edges)
{
  if (rule.has_min_length && adj.len < rule.min_length) {
    return false;
  }
  const int32_t par_space =
      rule.subtract_eol_width ? std::max<int32_t>(0, rule.par_space - static_cast<int32_t>(eol_se.edge.len)) : rule.par_space;
  const idb::IdbRect par_box = buildParallelTriggerBox(eol_se.edge, adj, par_space, rule.par_within, rule.eol_within);
  const int8_t need_nx = static_cast<int8_t>(-adj.nx);
  const int8_t need_ny = static_cast<int8_t>(-adj.ny);
  const bool box_horiz_sep = (adj.nx != 0);
  for (const auto& candidate : all_edges) {
    if (candidate.edge.nx != need_nx || candidate.edge.ny != need_ny) {
      continue;
    }
    if (candidate.poly_id == eol_se.poly_id && rectsClosedOverlap(candidate.edge_rect, eol_se.edge_rect)) {
      continue;
    }
    // Opposing parallel edge lies along the normal axis strictly overlapping the span inside par_box.
    const idb::IdbRect& er = candidate.edge_rect;
    if (box_horiz_sep) {
      const bool x_in = er.get_low_x() > par_box.get_low_x() && er.get_high_x() < par_box.get_high_x();
      const bool y_ov = er.get_low_y() < par_box.get_high_y() && er.get_high_y() > par_box.get_low_y();
      if (x_in && y_ov) {
        return true;
      }
    } else {
      const bool y_in = er.get_low_y() > par_box.get_low_y() && er.get_high_y() < par_box.get_high_y();
      const bool x_ov = er.get_low_x() < par_box.get_high_x() && er.get_high_x() > par_box.get_low_x();
      if (y_in && x_ov) {
        return true;
      }
    }
  }
  return false;
}

bool isEolEdgeViolatedInScene(const DrcPolygon& poly, const DrcRing& ring, int32_t edge_idx, int poly_id,
                              const EolSpacingRule& rule, const std::vector<EolSceneRect>& solid_rects,
                              const std::vector<EolSceneEdge>& all_edges)
{
  const int32_t vn = static_cast<int32_t>(ring.edges.size());
  const DrcEdge& e = ring.edges[static_cast<size_t>(edge_idx)];
  if (!isEolEdgeRule(e, rule)) {
    return false;
  }

  EolSceneEdge self_se{e, edgeAsRect(e), poly_id};
  if (rule.has_parallel_edge) {
    const DrcEdge& pre_edge = ring.edges[static_cast<size_t>(ringIdx(edge_idx - 1, vn))];
    const DrcEdge& post_edge = ring.edges[static_cast<size_t>(ringIdx(edge_idx + 1, vn))];
    const bool pre_ok = hasParallelTrigger(self_se, pre_edge, rule, all_edges);
    const bool post_ok = hasParallelTrigger(self_se, post_edge, rule, all_edges);
    if (rule.two_edges ? !(pre_ok && post_ok) : !(pre_ok || post_ok)) {
      return false;
    }
  }

  const int32_t max_sp = std::max(rule.spacing, rule.has_end_to_end ? rule.end_to_end_space : 0);
  if (max_sp <= 0) {
    return false;
  }

  const bool horiz_eol = (e.ny != 0);
  auto makeCheckRect = [&](int32_t sp) -> std::optional<idb::IdbRect> {
    if (sp <= 0) {
      return std::nullopt;
    }
    const idb::IdbRect r = expandAlongNormal(self_se.edge_rect, e.nx, e.ny, sp);
    const int32_t lx = horiz_eol ? r.get_low_x() - rule.eol_within : r.get_low_x();
    const int32_t ly = horiz_eol ? r.get_low_y() : r.get_low_y() - rule.eol_within;
    const int32_t hx = horiz_eol ? r.get_high_x() + rule.eol_within : r.get_high_x();
    const int32_t hy = horiz_eol ? r.get_high_y() : r.get_high_y() + rule.eol_within;
    if (lx >= hx || ly >= hy) {
      return std::nullopt;
    }
    return idb::IdbRect(lx, ly, hx, hy);
  };

  const std::optional<idb::IdbRect> eol_win = makeCheckRect(rule.spacing);
  const std::optional<idb::IdbRect> ete_win = rule.has_end_to_end ? makeCheckRect(rule.end_to_end_space) : eol_win;
  (void)poly;

  for (const auto& sr : solid_rects) {
    if (eol_win.has_value() && rectsOpenOverlap(*eol_win, sr.rect) &&
        distLessThan(self_se.edge_rect, sr.rect, rule.spacing)) {
      return true;
    }
    if (rule.has_end_to_end && ete_win.has_value() && rectsOpenOverlap(*ete_win, sr.rect) &&
        distLessThan(self_se.edge_rect, sr.rect, rule.end_to_end_space)) {
      for (const auto& other_e : all_edges) {
        if (other_e.poly_id != sr.poly_id) {
          continue;
        }
        if (other_e.edge.nx != -e.nx || other_e.edge.ny != -e.ny) {
          continue;
        }
        if (!isEolEdgeRule(other_e.edge, rule)) {
          continue;
        }
        if (rectsOpenOverlap(*ete_win, other_e.edge_rect) &&
            distLessThan(self_se.edge_rect, other_e.edge_rect, rule.end_to_end_space)) {
          return true;
        }
      }
    }
  }
  return false;
}

EolScene buildEolScene(const std::vector<DrcPolygon>& group_a, const std::vector<DrcPolygon>& group_b)
{
  EolScene scene;
  int pid = 0;
  for (const auto* grp : {&group_a, &group_b}) {
    for (const auto& poly : *grp) {
      for (const auto& mr : poly.max_rects) {
        scene.solid_rects.push_back({mr, pid});
      }
      for (const auto& ring : poly.rings) {
        for (const auto& e : ring.edges) {
          scene.edges.push_back({e, edgeAsRect(e), pid});
        }
      }
      ++pid;
    }
  }
  return scene;
}

struct PolygonFeatureViols
{
  gtl::polygon_90_set_data<int64_t> max_width;
  gtl::polygon_90_set_data<int64_t> min_width;
  std::vector<idb::IdbRect> min_enclosed_holes;
  std::vector<idb::IdbRect> notch_viols;
  std::vector<idb::IdbRect> min_step_viols;
};

int32_t maxWallWidthAround(const DrcPolygon& poly, const idb::IdbRect& edge_or_hole_box)
{
  int32_t best = 0;
  for (const auto& mr : poly.max_rects) {
    if (rectsClosedOverlap(mr, edge_or_hole_box)) {
      best = std::max(best, std::min(mr.get_width(), mr.get_height()));
    }
  }
  return best;
}

bool minStepTypeMatches(int type, const DrcEdge& e)
{
  switch (type) {
    case 1:  // INSIDECORNER
      return !e.convex_start && !e.convex_end;
    case 2:  // OUTSIDECORNER
      return e.convex_start && e.convex_end;
    case 3:  // STEP
      return e.convex_start != e.convex_end;
    default:
      return true;
  }
}

void collectPolygonFeatureViols(const std::vector<DrcPolygon>& polys, const RoutingLayerInfo& info,
                                const std::vector<idb::IdbRect>* filter_touch_rects, PolygonFeatureViols& out)
{
  if (info.max_width > 0) {
    for (const auto& poly : polys) {
      if (filter_touch_rects && !touchesAnyRect(poly.bbox, *filter_touch_rects)) {
        continue;
      }
      for (const auto& mr : poly.max_rects) {
        if (std::min(mr.get_width(), mr.get_height()) > info.max_width) {
          out.max_width += toGtlRect(mr);
        }
      }
    }
  }

  if (info.min_width > 0) {
    std::vector<gtl::rectangle_data<int64_t>> slices;
    for (const auto& poly : polys) {
      if (filter_touch_rects && !touchesAnyRect(poly.bbox, *filter_touch_rects)) {
        continue;
      }
      gtl::polygon_90_set_data<int64_t> single_ps;
      for (const auto& mr : poly.max_rects) {
        single_ps += toGtlRect(mr);
      }
      for (const auto orient : {gtl::HORIZONTAL, gtl::VERTICAL}) {
        slices.clear();
        gtl::get_rectangles(slices, single_ps, orient);
        for (const auto& sr : slices) {
          const idb::IdbRect ir = toIdbRect(sr);
          const int32_t span = (orient == gtl::HORIZONTAL) ? ir.get_width() : ir.get_height();
          if (span < info.min_width) {
            out.min_width += sr;
          }
        }
      }
    }
  }

  for (const auto& poly : polys) {
    if (filter_touch_rects && !touchesAnyRect(poly.bbox, *filter_touch_rects)) {
      continue;
    }

    if (!info.min_enclosed_areas.empty()) {
      for (const auto& ring : poly.rings) {
        if (!ring.is_hole || ring.vertices.size() < 4) {
          continue;
        }
        int32_t lx = std::numeric_limits<int32_t>::max();
        int32_t ly = std::numeric_limits<int32_t>::max();
        int32_t hx = std::numeric_limits<int32_t>::min();
        int32_t hy = std::numeric_limits<int32_t>::min();
        int64_t hole_area2 = 0;
        const size_t vn = ring.vertices.size();
        for (size_t i = 0; i < vn; ++i) {
          const auto& p = ring.vertices[i];
          const auto& q = ring.vertices[(i + 1) % vn];
          lx = std::min(lx, p.x);
          ly = std::min(ly, p.y);
          hx = std::max(hx, p.x);
          hy = std::max(hy, p.y);
          hole_area2 += static_cast<int64_t>(p.x) * q.y - static_cast<int64_t>(q.x) * p.y;
        }
        const int64_t hole_area = std::abs(hole_area2) / 2;
        const idb::IdbRect hbox(lx, ly, hx, hy);
        const int32_t wall_w = maxWallWidthAround(poly, hbox);
        for (const auto& rule : info.min_enclosed_areas) {
          if (rule.width > 0 && wall_w < rule.width) {
            continue;
          }
          if (hole_area < rule.area) {
            out.min_enclosed_holes.push_back(hbox);
            break;
          }
        }
      }
    }

    if (info.notch_rule.valid) {
      const NotchSpacingRule& nr = info.notch_rule;
      for (const auto& ring : poly.rings) {
        const int32_t n = static_cast<int32_t>(ring.edges.size());
        if (n < 4) {
          continue;
        }
        for (int32_t i = 0; i < n; ++i) {
          const DrcEdge& ei = ring.edges[static_cast<size_t>(i)];
          const DrcEdge& ei1 = ring.edges[static_cast<size_t>(ringIdx(i + 1, n))];
          const DrcEdge& ei2 = ring.edges[static_cast<size_t>(ringIdx(i + 2, n))];
          // Two-concave U-notch: prev convex, i->i+1 concave, i+1->i+2 concave, next convex.
          if (ei.convex_start && !ei.convex_end && !ei1.convex_end && ei2.convex_end) {
            if ((ei.len < nr.notch_length || ei2.len < nr.notch_length) && ei1.len < nr.min_spacing) {
              const idb::IdbRect short_r = (ei.len < ei2.len) ? edgeAsRect(ei) : edgeAsRect(ei2);
              const idb::IdbRect sp_r = edgeAsRect(ei1);
              out.notch_viols.emplace_back(
                  std::min(short_r.get_low_x(), sp_r.get_low_x()), std::min(short_r.get_low_y(), sp_r.get_low_y()),
                  std::max(short_r.get_high_x(), sp_r.get_high_x()), std::max(short_r.get_high_y(), sp_r.get_high_y()));
            }
          }
          // Three-concave C-notch (CONCAVEENDS).
          if (nr.concave_ends && n >= 5) {
            const DrcEdge& ei3 = ring.edges[static_cast<size_t>(ringIdx(i + 3, n))];
            if (ei.convex_start && !ei.convex_end && !ei1.convex_end && !ei2.convex_end && ei3.convex_end) {
              const DrcEdge* triplets[2][3] = {{&ei2, &ei1, &ei}, {&ei1, &ei2, &ei3}};
              for (const auto& t : triplets) {
                const DrcEdge* concave_e = t[0];
                const DrcEdge* spacing_e = t[1];
                const DrcEdge* side_e = t[2];
                if (concave_e->len < nr.notch_length && side_e->len >= nr.notch_length && spacing_e->len < nr.min_spacing) {
                  const bool wide_adj =
                      (nr.concave_length < maxWallWidthAround(poly, edgeAsRect(*concave_e))) ||
                      (nr.concave_length < maxWallWidthAround(poly, edgeAsRect(*side_e)));
                  if (!wide_adj) {
                    const idb::IdbRect cr = edgeAsRect(*concave_e);
                    const idb::IdbRect sr = edgeAsRect(*spacing_e);
                    out.notch_viols.emplace_back(
                        std::min(cr.get_low_x(), sr.get_low_x()), std::min(cr.get_low_y(), sr.get_low_y()),
                        std::max(cr.get_high_x(), sr.get_high_x()), std::max(cr.get_high_y(), sr.get_high_y()));
                  }
                }
              }
            }
          }
        }
      }
    }

    if (info.min_step_rule.valid && info.min_step_rule.min_step_length > 0) {
      const MinStepRule& ms = info.min_step_rule;
      for (const auto& ring : poly.rings) {
        const int32_t n = static_cast<int32_t>(ring.edges.size());
        if (n < 4) {
          continue;
        }
        if (!ms.has_min_adj_length) {
          const int32_t eff_max_edges = ms.max_edges >= 0 ? ms.max_edges : 0;
          auto emitRunViol = [&](int32_t start_i, int32_t short_cnt) {
            int32_t lx = std::numeric_limits<int32_t>::max();
            int32_t ly = std::numeric_limits<int32_t>::max();
            int32_t hx = std::numeric_limits<int32_t>::min();
            int32_t hy = std::numeric_limits<int32_t>::min();
            for (int32_t s = 0; s < short_cnt; ++s) {
              const DrcEdge& es = ring.edges[static_cast<size_t>(ringIdx(start_i + s, n))];
              lx = std::min({lx, es.x1, es.x2});
              ly = std::min({ly, es.y1, es.y2});
              hx = std::max({hx, es.x1, es.x2});
              hy = std::max({hy, es.y1, es.y2});
            }
            out.min_step_viols.emplace_back(lx, ly, hx, hy);
          };

          bool all_short = true;
          int64_t all_sum = 0;
          for (int32_t i = 0; i < n; ++i) {
            const DrcEdge& ei = ring.edges[static_cast<size_t>(i)];
            if (ei.len >= ms.min_step_length || !minStepTypeMatches(ms.type, ei)) {
              all_short = false;
              break;
            }
            all_sum += ei.len;
          }
          if (all_short) {
            const bool edge_viol = (n > eff_max_edges);
            const bool sum_viol = ms.has_length_sum ? (all_sum > ms.length_sum) : false;
            if (edge_viol || sum_viol) {
              emitRunViol(0, n);
            }
            continue;
          }

          for (int32_t i = 0; i < n; ++i) {
            const DrcEdge& ei = ring.edges[static_cast<size_t>(i)];
            if (ei.len >= ms.min_step_length || !minStepTypeMatches(ms.type, ei)) {
              continue;
            }
            const DrcEdge& prev = ring.edges[static_cast<size_t>(ringIdx(i - 1, n))];
            if (prev.len < ms.min_step_length && minStepTypeMatches(ms.type, prev)) {
              continue;
            }
            int32_t short_cnt = 1;
            int64_t len_sum = ei.len;
            for (int32_t j = 1; j < n; ++j) {
              const DrcEdge& ej = ring.edges[static_cast<size_t>(ringIdx(i + j, n))];
              if (ej.len >= ms.min_step_length || !minStepTypeMatches(ms.type, ej)) {
                break;
              }
              ++short_cnt;
              len_sum += ej.len;
            }
            const bool edge_viol = (short_cnt > eff_max_edges);
            const bool sum_viol = ms.has_length_sum ? (len_sum > ms.length_sum) : false;
            if (edge_viol || sum_viol) {
              emitRunViol(i, short_cnt);
            }
          }
        } else {
          for (int32_t i = 0; i < n; ++i) {
            const DrcEdge& ei = ring.edges[static_cast<size_t>(i)];
            const DrcEdge& post = ring.edges[static_cast<size_t>(ringIdx(i + 1, n))];
            if (ms.convex_corner && !ei.convex_end) {
              continue;
            }
            const bool hit = (ei.len < ms.min_step_length && post.len < ms.min_adj_length) ||
                             (ei.len < ms.min_adj_length && post.len < ms.min_step_length);
            if (hit) {
              const idb::IdbRect r1 = edgeAsRect(ei);
              const idb::IdbRect r2 = edgeAsRect(post);
              out.min_step_viols.emplace_back(std::min(r1.get_low_x(), r2.get_low_x()),
                                              std::min(r1.get_low_y(), r2.get_low_y()),
                                              std::max(r1.get_high_x(), r2.get_high_x()),
                                              std::max(r1.get_high_y(), r2.get_high_y()));
            }
          }
        }
      }
    }
  }
}

struct SameNetSpacingViol
{
  idb::IdbRect gap;
  idb::IdbRect rect_a;
  idb::IdbRect rect_b;
};

std::vector<SameNetSpacingViol> collectSameNetSpacingViols(
    const std::vector<DrcPolygon>& polys, const std::vector<idb::IdbRect>* filter_touch_rects,
    const std::function<int32_t(const idb::IdbRect&, const idb::IdbRect&)>& spacing_fn)
{
  std::vector<idb::IdbRect> all_max_rects;
  std::vector<int> rect_poly_id;
  for (size_t pi = 0; pi < polys.size(); ++pi) {
    for (const auto& mr : polys[pi].max_rects) {
      all_max_rects.push_back(mr);
      rect_poly_id.push_back(static_cast<int>(pi));
    }
  }

  std::vector<SameNetSpacingViol> viols;
  const size_t n = all_max_rects.size();
  for (size_t i = 0; i < n; ++i) {
    const idb::IdbRect& a = all_max_rects[i];
    const bool a_touched =
        !filter_touch_rects || touchesAnyRect(polys[static_cast<size_t>(rect_poly_id[i])].bbox, *filter_touch_rects);
    for (size_t j = i + 1; j < n; ++j) {
      const idb::IdbRect& b = all_max_rects[j];
      const bool b_touched =
          !filter_touch_rects || touchesAnyRect(polys[static_cast<size_t>(rect_poly_id[j])].bbox, *filter_touch_rects);
      if (!a_touched && !b_touched) {
        continue;
      }
      if (rectsClosedOverlap(a, b)) {
        continue;
      }
      const int32_t req = spacing_fn(a, b);
      if (!distLessThan(a, b, req)) {
        continue;
      }

      idb::IdbRect gap = computeGapRect(a, b);
      gtl::polygon_90_set_data<int64_t> gap_ps;
      gap_ps += toGtlRect(gap);

      const int32_t gw = gap.get_high_x() - gap.get_low_x();
      const int32_t gh = gap.get_high_y() - gap.get_low_y();
      const int64_t gap_area = static_cast<int64_t>(gw) * gh;

      bool total_inside = false;
      bool zero_area_inside = false;
      bool west_in = false;
      bool east_in = false;
      bool south_in = false;
      bool north_in = false;

      const idb::IdbRect west_edge(gap.get_low_x(), gap.get_low_y(), gap.get_low_x(), gap.get_high_y());
      const idb::IdbRect east_edge(gap.get_high_x(), gap.get_low_y(), gap.get_high_x(), gap.get_high_y());
      const idb::IdbRect south_edge(gap.get_low_x(), gap.get_low_y(), gap.get_high_x(), gap.get_low_y());
      const idb::IdbRect north_edge(gap.get_low_x(), gap.get_high_y(), gap.get_high_x(), gap.get_high_y());

      for (size_t k = 0; k < n; ++k) {
        const idb::IdbRect& m = all_max_rects[k];
        if (rectsOpenOverlap(m, gap)) {
          gap_ps -= toGtlRect(m);
        }
        if (gtl::empty(gap_ps)) {
          total_inside = true;
          break;
        }
        if (rectContains(m, gap)) {
          zero_area_inside = true;
        }
        west_in = west_in || rectContains(m, west_edge);
        east_in = east_in || rectContains(m, east_edge);
        south_in = south_in || rectContains(m, south_edge);
        north_in = north_in || rectContains(m, north_edge);
      }

      const bool hor = west_in && east_in;
      const bool ver = north_in && south_in;
      const bool any_orient = west_in || east_in || south_in || north_in;

      if (gap_area == 0) {
        if (zero_area_inside) {
          continue;
        }
        viols.push_back({gap, a, b});
      } else if (total_inside || (any_orient && !hor && !ver)) {
        continue;
      } else {
        gtl::rectangle_data<int64_t> rem_bbox;
        gap_ps.extents(rem_bbox);
        viols.push_back({toIdbRect(rem_bbox), a, b});
      }
    }
  }
  return viols;
}

}  // namespace

std::vector<DrcPolygon> extractPolygonRings(const gtl::polygon_90_set_data<int64_t>& poly_set)
{
  std::vector<DrcPolygon> out;
  if (gtl::empty(poly_set)) {
    return out;
  }
  std::vector<gtl::polygon_90_with_holes_data<int64_t>> gtl_polys;
  poly_set.get(gtl_polys);
  out.reserve(gtl_polys.size());

  for (const auto& p : gtl_polys) {
    DrcPolygon dp;
    dp.area = gtl::area(p);
    gtl::rectangle_data<int64_t> eb;
    gtl::extents(eb, p);
    dp.bbox = toIdbRect(eb);

    DrcRing outer = buildCleanRing(toDrcPoints(gtl::begin_points(p), gtl::end_points(p)), false);
    if (!outer.edges.empty()) {
      dp.rings.push_back(std::move(outer));
    }
    for (auto hit = gtl::begin_holes(p); hit != gtl::end_holes(p); ++hit) {
      DrcRing hole = buildCleanRing(toDrcPoints(gtl::begin_points(*hit), gtl::end_points(*hit)), true);
      if (!hole.edges.empty()) {
        dp.rings.push_back(std::move(hole));
      }
    }

    gtl::polygon_90_set_data<int64_t> single_ps;
    single_ps += p;
    std::vector<gtl::rectangle_data<int64_t>> mrs;
    gtl::get_max_rectangles(mrs, single_ps);
    dp.max_rects.reserve(mrs.size());
    for (const auto& r : mrs) {
      dp.max_rects.push_back(toIdbRect(r));
    }

    out.push_back(std::move(dp));
  }
  return out;
}

bool checkLef58MinArea(const DrcPolygon& poly, int64_t classic_min_area,
                       const std::vector<Lef58MinAreaRule>& lef58_areas, int64_t* out_deficit)
{
  if (out_deficit) {
    *out_deficit = 0;
  }
  if (classic_min_area > 0 && poly.area < classic_min_area) {
    if (out_deficit) {
      *out_deficit = classic_min_area - poly.area;
    }
    return false;
  }

  for (const Lef58MinAreaRule& rule : lef58_areas) {
    if (rule.min_area <= 0 || poly.area >= rule.min_area) {
      continue;
    }
    bool exempted = false;
    if (!rule.except_min_sizes.empty()) {
      for (const auto& mr : poly.max_rects) {
        const int32_t w_min = std::min(mr.get_width(), mr.get_height());
        const int32_t w_max = std::max(mr.get_width(), mr.get_height());
        for (const auto& [ew, el] : rule.except_min_sizes) {
          const int32_t rmin = std::min(ew, el);
          const int32_t rmax = std::max(ew, el);
          if (w_min >= rmin && w_max >= rmax) {
            exempted = true;
            break;
          }
        }
        if (exempted) {
          break;
        }
      }
    }
    if (!exempted && rule.except_max_edge_length >= 0) {
      if (rule.except_min_edge_length >= 0) {
        for (const auto& ring : poly.rings) {
          for (const auto& e : ring.edges) {
            if (e.len >= rule.except_min_edge_length && e.len <= rule.except_max_edge_length) {
              exempted = true;
              break;
            }
          }
          if (exempted) {
            break;
          }
        }
      } else {
        bool all_short = !poly.rings.empty();
        for (const auto& ring : poly.rings) {
          for (const auto& e : ring.edges) {
            if (e.len > rule.except_max_edge_length) {
              all_short = false;
              break;
            }
          }
          if (!all_short) {
            break;
          }
        }
        exempted = all_short;
      }
    }
    if (!exempted) {
      if (out_deficit) {
        *out_deficit = rule.min_area - poly.area;
      }
      return false;
    }
  }
  return true;
}

bool checkEndOfLineSpacing(const std::vector<DrcPolygon>& existing_polys, const std::vector<DrcPolygon>& merged_polys,
                           const gtl::polygon_90_set_data<int64_t>& merged_foreign_obs,
                           const std::vector<idb::IdbRect>& new_shapes, const std::vector<EolSpacingRule>& eol_rules)
{
  if (eol_rules.empty()) {
    return true;
  }

  const std::vector<DrcPolygon> foreign_polys = extractPolygonRings(merged_foreign_obs);
  const EolScene after_scene = buildEolScene(merged_polys, foreign_polys);
  const EolScene before_scene = buildEolScene(existing_polys, foreign_polys);

  int32_t max_reach = 0;
  for (const auto& rule : eol_rules) {
    max_reach = std::max(max_reach, std::max({rule.spacing, rule.has_end_to_end ? rule.end_to_end_space : 0,
                                              rule.has_parallel_edge ? rule.par_space : 0}) +
                                        std::max(rule.eol_within, rule.par_within));
  }
  std::vector<idb::IdbRect> active_boxes;
  active_boxes.reserve(new_shapes.size());
  for (const auto& r : new_shapes) {
    active_boxes.emplace_back(r.get_low_x() - max_reach, r.get_low_y() - max_reach, r.get_high_x() + max_reach,
                              r.get_high_y() + max_reach);
  }

  // Build map for quick check whether an EOL edge was already violated before adding new_shapes.
  auto findSameEdgeBefore = [&](const std::vector<DrcPolygon>& before_group, int before_pid_offset, const DrcEdge& e,
                                const EolSpacingRule& rule) -> bool {
    int pid = before_pid_offset;
    for (const auto& bp : before_group) {
      if (!rectsClosedOverlap(bp.bbox, edgeAsRect(e))) {
        ++pid;
        continue;
      }
      for (const auto& br : bp.rings) {
        const int32_t bn = static_cast<int32_t>(br.edges.size());
        for (int32_t bi = 0; bi < bn; ++bi) {
          const DrcEdge& be = br.edges[static_cast<size_t>(bi)];
          if (be.x1 == e.x1 && be.y1 == e.y1 && be.x2 == e.x2 && be.y2 == e.y2 && be.nx == e.nx && be.ny == e.ny) {
            if (isEolEdgeViolatedInScene(bp, br, bi, pid, rule, before_scene.solid_rects, before_scene.edges)) {
              return true;
            }
          }
        }
      }
      ++pid;
    }
    return false;
  };

  // Direction 1: EOL edges on merged target-net polygons.
  int pid_merged = 0;
  for (const auto& mp : merged_polys) {
    if (touchesAnyRect(mp.bbox, active_boxes)) {
      for (const auto& mr : mp.rings) {
        const int32_t mn = static_cast<int32_t>(mr.edges.size());
        for (int32_t mi = 0; mi < mn; ++mi) {
          const DrcEdge& me = mr.edges[static_cast<size_t>(mi)];
          if (!touchesAnyRect(edgeAsRect(me), active_boxes)) {
            continue;
          }
          for (const auto& rule : eol_rules) {
            if (!isEolEdgeViolatedInScene(mp, mr, mi, pid_merged, rule, after_scene.solid_rects, after_scene.edges)) {
              continue;
            }
            if (!findSameEdgeBefore(existing_polys, 0, me, rule)) {
              return false;
            }
          }
        }
      }
    }
    ++pid_merged;
  }

  // Direction 2: EOL edges on foreign obstacle polygons near new_shapes.
  const int before_foreign_offset = static_cast<int>(existing_polys.size());
  int pid_foreign = static_cast<int>(merged_polys.size());
  for (const auto& fp : foreign_polys) {
    if (touchesAnyRect(fp.bbox, active_boxes)) {
      for (const auto& fr : fp.rings) {
        const int32_t fn = static_cast<int32_t>(fr.edges.size());
        for (int32_t fi = 0; fi < fn; ++fi) {
          const DrcEdge& fe = fr.edges[static_cast<size_t>(fi)];
          if (!touchesAnyRect(edgeAsRect(fe), active_boxes)) {
            continue;
          }
          for (const auto& rule : eol_rules) {
            if (!isEolEdgeViolatedInScene(fp, fr, fi, pid_foreign, rule, after_scene.solid_rects, after_scene.edges)) {
              continue;
            }
            if (!findSameEdgeBefore(foreign_polys, before_foreign_offset, fe, rule)) {
              return false;
            }
          }
        }
      }
    }
    ++pid_foreign;
  }

  return true;
}

bool checkMergedNetPolygonDrc(const gtl::polygon_90_set_data<int64_t>& existing_set,
                              const gtl::polygon_90_set_data<int64_t>& merged_set,
                              const std::vector<idb::IdbRect>& new_shapes, const RoutingLayerInfo& layer_info,
                              const std::function<int32_t(const idb::IdbRect&, const idb::IdbRect&)>& spacing_fn,
                              std::vector<idb::IdbRect>* out_same_net_conflicts)
{
  (void)existing_set;
  (void)merged_set;
  if (new_shapes.empty() || gtl::empty(merged_set)) {
    return true;
  }

  const std::vector<DrcPolygon> merged_polys = extractPolygonRings(merged_set);
  const std::vector<DrcPolygon> existing_polys = extractPolygonRings(existing_set);

  PolygonFeatureViols after_viols;
  collectPolygonFeatureViols(merged_polys, layer_info, &new_shapes, after_viols);

  const bool has_any_feature_viol = !gtl::empty(after_viols.max_width) || !gtl::empty(after_viols.min_width) ||
                                    !after_viols.min_enclosed_holes.empty() || !after_viols.notch_viols.empty() ||
                                    !after_viols.min_step_viols.empty();

  if (has_any_feature_viol) {
    PolygonFeatureViols before_viols;
    collectPolygonFeatureViols(existing_polys, layer_info, nullptr, before_viols);

    if (!gtl::empty(after_viols.max_width)) {
      gtl::polygon_90_set_data<int64_t> diff = after_viols.max_width;
      diff -= before_viols.max_width;
      if (!gtl::empty(diff)) {
        return false;
      }
    }
    if (!gtl::empty(after_viols.min_width)) {
      gtl::polygon_90_set_data<int64_t> diff = after_viols.min_width;
      diff -= before_viols.min_width;
      if (!gtl::empty(diff)) {
        return false;
      }
    }
    for (const auto& h : after_viols.min_enclosed_holes) {
      if (!boxPresentExactly(before_viols.min_enclosed_holes, h)) {
        return false;
      }
    }
    for (const auto& nv : after_viols.notch_viols) {
      if (!boxContainsAny(before_viols.notch_viols, nv)) {
        return false;
      }
    }
    for (const auto& mv : after_viols.min_step_viols) {
      if (!boxContainsAny(before_viols.min_step_viols, mv)) {
        return false;
      }
    }
  }

  const std::vector<SameNetSpacingViol> after_same_net = collectSameNetSpacingViols(merged_polys, &new_shapes, spacing_fn);
  if (!after_same_net.empty()) {
    const std::vector<SameNetSpacingViol> before_same_net = collectSameNetSpacingViols(existing_polys, nullptr, spacing_fn);
    std::vector<idb::IdbRect> before_gaps;
    before_gaps.reserve(before_same_net.size());
    for (const auto& bv : before_same_net) {
      before_gaps.push_back(bv.gap);
    }
    bool has_same_net_viol = false;
    for (const auto& sv : after_same_net) {
      if (!boxContainsAny(before_gaps, sv.gap)) {
        has_same_net_viol = true;
        if (out_same_net_conflicts == nullptr) {
          return false;
        }
        const bool a_new = touchesAnyRect(sv.rect_a, new_shapes);
        const bool b_new = touchesAnyRect(sv.rect_b, new_shapes);
        if (a_new && !b_new) {
          out_same_net_conflicts->push_back(sv.rect_b);
        } else if (b_new && !a_new) {
          out_same_net_conflicts->push_back(sv.rect_a);
        }
      }
    }
    if (has_same_net_viol) {
      return false;
    }
  }

  return true;
}

}  // namespace imj
