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
#include "StubMazeSearch.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <queue>
#include <tuple>

#include "IdbTrackGrid.h"
#include "LocalStubRouter.hpp"

namespace imj {
namespace {

void collectTrackAxis(int64_t start, int64_t pitch, int64_t ntracks, int32_t win_lo, int32_t win_hi,
                      std::vector<int32_t>& extra)
{
  if (pitch <= 0) {
    return;
  }
  const int64_t k_lo = std::max<int64_t>(0, ceilDiv(static_cast<int64_t>(win_lo) - start, pitch));
  const int64_t hi_by_win = floorDiv(static_cast<int64_t>(win_hi) - start, pitch);
  const int64_t k_hi = (ntracks > 0) ? std::min<int64_t>(ntracks - 1, hi_by_win) : hi_by_win;
  if (k_hi < k_lo) {
    return;
  }
  const int64_t step = std::max<int64_t>(1, (k_hi - k_lo + 1) / 24);
  for (int64_t k = k_lo; k <= k_hi; k += step) {
    const int64_t coord = start + k * pitch;
    if (coord >= std::numeric_limits<int32_t>::min() && coord <= std::numeric_limits<int32_t>::max()) {
      extra.push_back(static_cast<int32_t>(coord));
    }
  }
}

std::vector<int32_t> compactAxis(std::vector<int32_t> must, std::vector<int32_t> extra, int32_t lo, int32_t hi,
                                 size_t max_count)
{
  auto dedup = [&](std::vector<int32_t>& v) {
    v.erase(std::remove_if(v.begin(), v.end(), [&](int32_t c) { return c < lo || c > hi; }), v.end());
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };
  dedup(must);
  dedup(extra);
  if (must.size() >= max_count) {
    return must;
  }
  std::vector<int32_t> merged = std::move(must);
  const size_t budget = max_count - merged.size();
  if (extra.size() <= budget) {
    merged.insert(merged.end(), extra.begin(), extra.end());
  } else {
    for (size_t i = 0; i < budget; ++i) {
      merged.push_back(extra[(i * extra.size()) / budget]);
    }
  }
  dedup(merged);
  return merged;
}

idb::IdbRect insetAccessRect(const idb::IdbRect& r, const RoutingLayerInfo& info, const LocalStubRouter* router,
                             int lidx)
{
  int32_t lx = r.get_low_x() + info.half_low;
  int32_t hx = r.get_high_x() - info.half_high;
  if (lx > hx || router->snapCoordUp(lidx, lx) > router->snapCoordDown(lidx, hx)) {
    lx = r.get_low_x();
    hx = r.get_high_x();
  }
  int32_t ly = r.get_low_y() + info.half_low;
  int32_t hy = r.get_high_y() - info.half_high;
  if (ly > hy || router->snapCoordUp(lidx, ly) > router->snapCoordDown(lidx, hy)) {
    ly = r.get_low_y();
    hy = r.get_high_y();
  }
  return idb::IdbRect(lx, ly, hx, hy);
}

void buildSearchAxes(const StubMazeProblem& p, std::vector<int32_t>& xs, std::vector<int32_t>& ys)
{
  const auto& layers = p.router->layers();
  const int num_active_layers = p.max_layer_idx + 1;
  const int32_t win_lx = p.win_rect.get_low_x();
  const int32_t win_hx = p.win_rect.get_high_x();
  const int32_t win_ly = p.win_rect.get_low_y();
  const int32_t win_hy = p.win_rect.get_high_y();

  std::vector<int32_t> must_xs;
  std::vector<int32_t> must_ys;
  std::vector<int32_t> extra_xs;
  std::vector<int32_t> extra_ys;

  auto addGridAccess = [&](int lidx, int32_t target, int32_t lo, int32_t hi, std::vector<int32_t>& dst) {
    const int32_t clamped = std::clamp(target, lo, hi);
    const int32_t dn = p.router->snapCoordDown(lidx, clamped);
    const int32_t up = p.router->snapCoordUp(lidx, clamped);
    if (dn >= lo && dn <= hi && p.router->coordOnLayerMfgGrid(lidx, dn)) {
      dst.push_back(dn);
    }
    if (up != dn && up >= lo && up <= hi && p.router->coordOnLayerMfgGrid(lidx, up)) {
      dst.push_back(up);
    }
  };

  auto addNearestGrid = [&](int lidx, int32_t target, int32_t lo, int32_t hi, std::vector<int32_t>& dst) {
    if (target < lo || target > hi) {
      return;
    }
    const int32_t dn = p.router->snapCoordDown(lidx, target);
    const int32_t up = p.router->snapCoordUp(lidx, target);
    const bool dn_ok = (dn >= lo && dn <= hi && p.router->coordOnLayerMfgGrid(lidx, dn));
    const bool up_ok = (up >= lo && up <= hi && p.router->coordOnLayerMfgGrid(lidx, up));
    if (dn_ok && up_ok) {
      const int64_t dd = std::abs(static_cast<int64_t>(target) - dn);
      const int64_t du = std::abs(static_cast<int64_t>(up) - target);
      dst.push_back(dd <= du ? dn : up);
    } else if (dn_ok) {
      dst.push_back(dn);
    } else if (up_ok) {
      dst.push_back(up);
    }
  };

  auto addInsetAxes = [&](int lidx, const idb::IdbRect& box) {
    const auto& info = layers[lidx];
    const int32_t ext_lo = info.has_custom_wire_ext ? info.end_ext : info.half_low;
    const int32_t ext_hi = info.has_custom_wire_ext ? info.end_ext : info.half_high;
    for (int32_t cx : {box.get_low_x() + info.half_low, box.get_high_x() - info.half_high,
                       box.get_low_x() + ext_lo, box.get_high_x() - ext_hi}) {
      addNearestGrid(lidx, cx, box.get_low_x(), box.get_high_x(), extra_xs);
    }
    for (int32_t cy : {box.get_low_y() + info.half_low, box.get_high_y() - info.half_high,
                       box.get_low_y() + ext_lo, box.get_high_y() - ext_hi}) {
      addNearestGrid(lidx, cy, box.get_low_y(), box.get_high_y(), extra_ys);
    }
  };

  for (const LayerShapeBox& ss : *p.filtered_starts) {
    const idb::IdbRect acc = insetAccessRect(ss.rect, layers[ss.layer_idx], p.router, ss.layer_idx);
    const int32_t mx = (acc.get_low_x() + acc.get_high_x()) / 2;
    const int32_t my = (acc.get_low_y() + acc.get_high_y()) / 2;
    addGridAccess(ss.layer_idx, mx, acc.get_low_x(), acc.get_high_x(), must_xs);
    addGridAccess(ss.layer_idx, my, acc.get_low_y(), acc.get_high_y(), must_ys);
    addInsetAxes(ss.layer_idx, ss.rect);
  }
  for (const auto& [d, gs] : *p.ranked_goals) {
    (void) d;
    const idb::IdbRect acc = insetAccessRect(gs.rect, layers[gs.layer_idx], p.router, gs.layer_idx);
    addGridAccess(gs.layer_idx, p.sx0, acc.get_low_x(), acc.get_high_x(), must_xs);
    addGridAccess(gs.layer_idx, p.sy0, acc.get_low_y(), acc.get_high_y(), must_ys);
    addGridAccess(gs.layer_idx, (acc.get_low_x() + acc.get_high_x()) / 2, acc.get_low_x(),
                  acc.get_high_x(), must_xs);
    addGridAccess(gs.layer_idx, (acc.get_low_y() + acc.get_high_y()) / 2, acc.get_low_y(),
                  acc.get_high_y(), must_ys);
    addInsetAxes(gs.layer_idx, gs.rect);
  }

  for (int l = 0; l < num_active_layers; ++l) {
    const auto& info = layers[l];
    for (idb::IdbTrackGrid* tg : info.layer->get_track_grid_list()) {
      if (!tg || !tg->get_track() || tg->get_track()->get_pitch() == 0) {
        continue;
      }
      const int64_t start = static_cast<int64_t>(tg->get_track()->get_start());
      const int64_t pitch = static_cast<int64_t>(tg->get_track()->get_pitch());
      const int64_t ntracks = static_cast<int64_t>(tg->get_track_num());
      std::vector<int32_t> raw_tracks;
      if (tg->get_track()->is_track_direction_x()) {
        collectTrackAxis(start, pitch, ntracks, win_lx, win_hx, raw_tracks);
        for (int32_t c : raw_tracks) {
          if (p.router->coordOnLayerMfgGrid(l, c)) {
            extra_xs.push_back(c);
          }
        }
      } else if (tg->get_track()->is_track_direction_y()) {
        collectTrackAxis(start, pitch, ntracks, win_ly, win_hy, raw_tracks);
        for (int32_t c : raw_tracks) {
          if (p.router->coordOnLayerMfgGrid(l, c)) {
            extra_ys.push_back(c);
          }
        }
      }
    }

    const int32_t gx0 = !must_xs.empty() ? must_xs[std::min<size_t>(1, must_xs.size() - 1)] : p.sx0;
    const int32_t gy0 = !must_ys.empty() ? must_ys[std::min<size_t>(1, must_ys.size() - 1)] : p.sy0;
    for (int k = -4; k <= 4; ++k) {
      for (int32_t bx : {p.sx0, gx0}) {
        addNearestGrid(l, bx + k * info.pitch_x, win_lx, win_hx, extra_xs);
      }
      for (int32_t by : {p.sy0, gy0}) {
        addNearestGrid(l, by + k * info.pitch_y, win_ly, win_hy, extra_ys);
      }
    }
    int64_t req_area = info.min_area;
    for (const auto& la : info.lef58_areas) {
      req_area = std::max(req_area, la.min_area);
    }
    if (req_area > 0 && info.width > 0) {
      const int64_t hop_len = ceilDiv(req_area, info.width);
      if (hop_len <= win_hx - win_lx) {
        const int32_t h = static_cast<int32_t>(hop_len);
        for (int32_t bx : {p.sx0, gx0}) {
          addNearestGrid(l, bx - h, win_lx, win_hx, extra_xs);
          addNearestGrid(l, bx + h, win_lx, win_hx, extra_xs);
        }
      }
      if (hop_len <= win_hy - win_ly) {
        const int32_t h = static_cast<int32_t>(hop_len);
        for (int32_t by : {p.sy0, gy0}) {
          addNearestGrid(l, by - h, win_ly, win_hy, extra_ys);
          addNearestGrid(l, by + h, win_ly, win_hy, extra_ys);
        }
      }
    }

    if (p.router->hasDie()) {
      const idb::IdbRect& die = p.router->dieBBox();
      const int32_t low_x = p.router->snapCoordUp(l, die.get_low_x() + info.half_low);
      const int32_t high_x = p.router->snapCoordDown(l, die.get_high_x() - info.half_high);
      for (int32_t cx : {low_x, high_x}) {
        if (cx >= win_lx && cx <= win_hx && p.router->coordOnLayerMfgGrid(l, cx)) {
          extra_xs.push_back(cx);
        }
      }
      const int32_t low_y = p.router->snapCoordUp(l, die.get_low_y() + info.half_low);
      const int32_t high_y = p.router->snapCoordDown(l, die.get_high_y() - info.half_high);
      for (int32_t cy : {low_y, high_y}) {
        if (cy >= win_ly && cy <= win_hy && p.router->coordOnLayerMfgGrid(l, cy)) {
          extra_ys.push_back(cy);
        }
      }
    }

    auto addClearanceAxes = [&](const idb::IdbRect& ob, int32_t sp, bool to_must) {
      auto& tx = to_must ? must_xs : extra_xs;
      auto& ty = to_must ? must_ys : extra_ys;
      const int32_t cx_lo = p.router->snapCoordDown(l, ob.get_low_x() - (info.half_high + sp));
      const int32_t cx_hi = p.router->snapCoordUp(l, ob.get_high_x() + (info.half_low + sp));
      for (int32_t cx : {cx_lo, cx_hi}) {
        if (cx >= win_lx && cx <= win_hx && p.router->coordOnLayerMfgGrid(l, cx)) {
          tx.push_back(cx);
        }
      }
      const int32_t cy_lo = p.router->snapCoordDown(l, ob.get_low_y() - (info.half_high + sp));
      const int32_t cy_hi = p.router->snapCoordUp(l, ob.get_high_y() + (info.half_low + sp));
      for (int32_t cy : {cy_lo, cy_hi}) {
        if (cy >= win_ly && cy <= win_hy && p.router->coordOnLayerMfgGrid(l, cy)) {
          ty.push_back(cy);
        }
      }
    };

    if (p.force_max_spacing_obs != nullptr && l < static_cast<int>(p.force_max_spacing_obs->size())) {
      for (const idb::IdbRect& fob : (*p.force_max_spacing_obs)[l]) {
        addClearanceAxes(fob, info.max_spacing, true);
      }
    }
    if (p.force_avoid_obs != nullptr && l < static_cast<int>(p.force_avoid_obs->size())) {
      for (const idb::IdbRect& aob : (*p.force_avoid_obs)[l]) {
        addClearanceAxes(aob, info.min_spacing, true);
      }
    }

    int obs_added = 0;
    for (const RoutingObstacleBox& ob : (*p.obs_by_layer)[l]) {
      const int32_t req_max = std::max(info.max_spacing, ob.min_spacing);
      if (rectClearOfObstacle(ob.rect, p.win_rect, info.half_high + req_max + 1)) {
        continue;
      }
      if (++obs_added > 24) {
        break;
      }
      addClearanceAxes(ob.rect, req_max, false);
      const int32_t req_min = std::max(info.min_spacing, ob.min_spacing);
      if (req_min < req_max) {
        addClearanceAxes(ob.rect, req_min, false);
      }
    }

    if (l < p.max_layer_idx) {
      if (const ViaOption* vo = p.router->viaBetween(l, l + 1); vo != nullptr && !vo->cut_rects.empty()) {
        int32_t cut_min_lx = std::numeric_limits<int32_t>::max();
        int32_t cut_max_hx = std::numeric_limits<int32_t>::min();
        int32_t cut_min_ly = std::numeric_limits<int32_t>::max();
        int32_t cut_max_hy = std::numeric_limits<int32_t>::min();
        for (const idb::IdbRect& cr : vo->cut_rects) {
          cut_min_lx = std::min(cut_min_lx, std::min(cr.get_low_x(), cr.get_high_x()));
          cut_max_hx = std::max(cut_max_hx, std::max(cr.get_low_x(), cr.get_high_x()));
          cut_min_ly = std::min(cut_min_ly, std::min(cr.get_low_y(), cr.get_high_y()));
          cut_max_hy = std::max(cut_max_hy, std::max(cr.get_low_y(), cr.get_high_y()));
        }
        int32_t cut_ext = 0;
        for (const idb::IdbRect& cr : vo->cut_rects) {
          cut_ext = std::max({cut_ext, std::abs(cr.get_low_x()), std::abs(cr.get_high_x()),
                              std::abs(cr.get_low_y()), std::abs(cr.get_high_y())});
        }
        int cut_obs_added = 0;
        for (const CutObstacle& cob : (*p.cut_obs_by_low_idx)[l]) {
          if (cob.spacing <= 0 || rectClearOfObstacle(cob.rect, p.win_rect, cob.spacing + cut_ext)) {
            continue;
          }
          if (++cut_obs_added > 16) {
            break;
          }
          const int32_t cx_lo = p.router->snapCoordDown(l, cob.rect.get_low_x() - cob.spacing - cut_max_hx);
          const int32_t cx_hi = p.router->snapCoordUp(l, cob.rect.get_high_x() + cob.spacing - cut_min_lx);
          for (int32_t cx : {cx_lo, cx_hi}) {
            if (cx >= win_lx && cx <= win_hx && p.router->coordOnLayerMfgGrid(l, cx)) {
              extra_xs.push_back(cx);
            }
          }
          const int32_t cy_lo = p.router->snapCoordDown(l, cob.rect.get_low_y() - cob.spacing - cut_max_hy);
          const int32_t cy_hi = p.router->snapCoordUp(l, cob.rect.get_high_y() + cob.spacing - cut_min_ly);
          for (int32_t cy : {cy_lo, cy_hi}) {
            if (cy >= win_ly && cy <= win_hy && p.router->coordOnLayerMfgGrid(l, cy)) {
              extra_ys.push_back(cy);
            }
          }
        }
        if (vo->same_net_cut_spacing > 0) {
          const int32_t xspan = vo->same_net_cut_spacing + cut_max_hx - cut_min_lx;
          for (int32_t cx : {p.router->snapCoordDown(l, p.sx0 - xspan), p.router->snapCoordUp(l, p.sx0 + xspan)}) {
            if (cx >= win_lx && cx <= win_hx && p.router->coordOnLayerMfgGrid(l, cx)) {
              extra_xs.push_back(cx);
            }
          }
          const int32_t yspan = vo->same_net_cut_spacing + cut_max_hy - cut_min_ly;
          for (int32_t cy : {p.router->snapCoordDown(l, p.sy0 - yspan), p.router->snapCoordUp(l, p.sy0 + yspan)}) {
            if (cy >= win_ly && cy <= win_hy && p.router->coordOnLayerMfgGrid(l, cy)) {
              extra_ys.push_back(cy);
            }
          }
        }
      }
    }
  }

  xs = compactAxis(std::move(must_xs), std::move(extra_xs), win_lx, win_hx, 64);
  ys = compactAxis(std::move(must_ys), std::move(extra_ys), win_ly, win_hy, 64);
}

RoutePlan reconstructPlan(const std::vector<int>& chain, const std::vector<int32_t>& xs,
                          const std::vector<int32_t>& ys)
{
  const int nx = static_cast<int>(xs.size());
  const int ny = static_cast<int>(ys.size());
  auto decode = [&](int id) {
    const int ix = id % nx;
    const int r = id / nx;
    return std::tuple<int32_t, int32_t, int>{xs[ix], ys[r % ny], r / ny};
  };

  RoutePlan plan;
  plan.found = true;
  for (size_t i = 0; i + 1 < chain.size(); ++i) {
    const auto [ax, ay, al] = decode(chain[i]);
    const auto [bx, by, bl] = decode(chain[i + 1]);
    if (al != bl) {
      plan.moves.push_back({true, std::min(al, bl), std::max(al, bl), ax, ay, ax, ay});
      continue;
    }
    if (ax == bx && ay == by) {
      continue;
    }
    if (!plan.moves.empty() && !plan.moves.back().is_via && plan.moves.back().layer_idx == al) {
      StubMove& prev = plan.moves.back();
      if (prev.x1 == prev.x2 && prev.x2 == ax && ax == bx) {
        prev.y2 = by;
        continue;
      }
      if (prev.y1 == prev.y2 && prev.y2 == ay && ay == by) {
        prev.x2 = bx;
        continue;
      }
    }
    plan.moves.push_back({false, al, al, ax, ay, bx, by});
  }
  return plan;
}

}  // namespace

RoutePlan runStubMazeSearch(const StubMazeProblem& p)
{
  RoutePlan failed;
  if (!p.router || p.max_layer_idx < 0 || !p.filtered_starts || !p.ranked_goals ||
      !p.obs_by_layer || !p.cut_obs_by_low_idx) {
    return failed;
  }

  std::vector<int32_t> xs;
  std::vector<int32_t> ys;
  buildSearchAxes(p, xs, ys);
  const int nx = static_cast<int>(xs.size());
  const int ny = static_cast<int>(ys.size());
  if (nx == 0 || ny == 0) {
    return failed;
  }

  const auto& layers = p.router->layers();
  const int num_active_layers = p.max_layer_idx + 1;
  int32_t min_px = layers[0].pitch_x;
  int32_t min_py = layers[0].pitch_y;
  for (int l = 0; l < num_active_layers; ++l) {
    min_px = std::min(min_px, layers[l].pitch_x);
    min_py = std::min(min_py, layers[l].pitch_y);
  }

  const int total = num_active_layers * nx * ny;
  auto nodeId = [&](int ix, int iy, int l) -> int { return (l * ny + iy) * nx + ix; };
  std::vector<int64_t> g_cost(total, std::numeric_limits<int64_t>::max() / 4);
  std::vector<int> parent(total, -1);
  std::vector<bool> closed(total, false);

  std::vector<LayerShapeBox> start_access;
  start_access.reserve(p.filtered_starts->size());
  for (const LayerShapeBox& ss : *p.filtered_starts) {
    start_access.push_back({ss.layer_idx, insetAccessRect(ss.rect, layers[ss.layer_idx], p.router, ss.layer_idx),
                            ss.min_spacing});
  }
  std::vector<LayerShapeBox> goal_access;
  goal_access.reserve(p.ranked_goals->size());
  for (const auto& [d, gs] : *p.ranked_goals) {
    (void) d;
    goal_access.push_back({gs.layer_idx, insetAccessRect(gs.rect, layers[gs.layer_idx], p.router, gs.layer_idx),
                           gs.min_spacing});
  }

  auto isAvoidSkippedAtPoint = [&](int lidx, const idb::IdbRect& aob, int32_t px, int32_t py) -> bool {
    for (const LayerShapeBox& ss : *p.filtered_starts) {
      if (ss.layer_idx == lidx && !rectClearOfObstacle(aob, ss.rect, 0) && pointInsideRect(px, py, ss.rect)) {
        return true;
      }
    }
    for (const auto& [d, gs] : *p.ranked_goals) {
      (void)d;
      if (gs.layer_idx == lidx && !rectClearOfObstacle(aob, gs.rect, 0) && pointInsideRect(px, py, gs.rect)) {
        return true;
      }
    }
    return false;
  };

  auto viaValid = [&](int from_id, int low_idx, int32_t vx, int32_t vy) -> bool {
    if (low_idx < 0 || low_idx + 1 > p.max_layer_idx) {
      return false;
    }
    const ViaOption* vo = p.router->viaBetween(low_idx, low_idx + 1);
    if (!vo) {
      return false;
    }
    for (const idb::IdbRect& br : vo->bottom_rects) {
      const idb::IdbRect placed = shiftedRect(br, vx, vy);
      if (!p.router->rectInsideDie(placed) || !p.router->rectOnMfgGrid(placed)) {
        return false;
      }
      for (const RoutingObstacleBox& ob : (*p.obs_by_layer)[low_idx]) {
        if (!rectClearOfObstacle(
                placed, ob.rect,
                p.router->requiredSpacing(low_idx, placed, ob.rect, ob.min_spacing, ob.effective_width))) {
          return false;
        }
      }
      if (p.force_avoid_obs != nullptr && low_idx < static_cast<int>(p.force_avoid_obs->size())) {
        for (const idb::IdbRect& aob : (*p.force_avoid_obs)[low_idx]) {
          if (!isAvoidSkippedAtPoint(low_idx, aob, vx, vy) &&
              !rectClearOfObstacle(placed, aob, p.router->requiredSpacing(low_idx, placed, aob))) {
            return false;
          }
        }
      }
    }
    for (const idb::IdbRect& tr : vo->top_rects) {
      const idb::IdbRect placed = shiftedRect(tr, vx, vy);
      if (!p.router->rectInsideDie(placed) || !p.router->rectOnMfgGrid(placed)) {
        return false;
      }
      for (const RoutingObstacleBox& ob : (*p.obs_by_layer)[low_idx + 1]) {
        if (!rectClearOfObstacle(
                placed, ob.rect,
                p.router->requiredSpacing(low_idx + 1, placed, ob.rect, ob.min_spacing, ob.effective_width))) {
          return false;
        }
      }
      if (p.force_avoid_obs != nullptr && low_idx + 1 < static_cast<int>(p.force_avoid_obs->size())) {
        for (const idb::IdbRect& aob : (*p.force_avoid_obs)[low_idx + 1]) {
          if (!isAvoidSkippedAtPoint(low_idx + 1, aob, vx, vy) &&
              !rectClearOfObstacle(placed, aob, p.router->requiredSpacing(low_idx + 1, placed, aob))) {
            return false;
          }
        }
      }
    }
    for (const idb::IdbRect& cr : vo->cut_rects) {
      const idb::IdbRect placed = shiftedRect(cr, vx, vy);
      if (!p.router->rectInsideDie(placed) || !p.router->rectOnMfgGrid(placed)) {
        return false;
      }
      for (const CutObstacle& cob : (*p.cut_obs_by_low_idx)[low_idx]) {
        if (!rectClearOfObstacle(placed, cob.rect, cob.spacing)) {
          return false;
        }
      }
    }
    std::vector<CutShapeBox> ancestor_cuts;
    if (p.base_cuts != nullptr) {
      ancestor_cuts.insert(ancestor_cuts.end(), p.base_cuts->begin(), p.base_cuts->end());
    }
    for (int cur = from_id; cur >= 0;) {
      const int prev = parent[cur];
      if (prev < 0) {
        break;
      }
      const int cl = (cur / nx) / ny;
      const int pl = (prev / nx) / ny;
      if (cl != pl) {
        const int alo = std::min(cl, pl);
        const ViaOption* avo = p.router->viaBetween(alo, alo + 1);
        if (avo != nullptr && avo->cut_layer != nullptr) {
          const int cix = cur % nx;
          const int ciy = (cur / nx) % ny;
          const int32_t pvx = xs[cix];
          const int32_t pvy = ys[ciy];
          const int cut_order = static_cast<int>(avo->cut_layer->get_order());
          for (const idb::IdbRect& cr : avo->cut_rects) {
            ancestor_cuts.push_back({cut_order, shiftedRect(cr, pvx, pvy)});
          }
        }
      }
      cur = prev;
    }
    return p.router->checkMazeViaCutsValid(low_idx, vx, vy, ancestor_cuts, p.target_net);
  };

  auto isGoalCell = [&](int ix, int iy, int l) -> bool {
    const int32_t px = xs[ix];
    const int32_t py = ys[iy];
    if (!p.router->coordOnLayerMfgGrid(l, px) || !p.router->coordOnLayerMfgGrid(l, py)) {
      return false;
    }
    for (const LayerShapeBox& gs : goal_access) {
      if (gs.layer_idx == l && pointInsideRect(px, py, gs.rect)) {
        return true;
      }
    }
    return false;
  };

  auto heuristic = [&](int ix, int iy) -> int64_t {
    int64_t best = std::numeric_limits<int64_t>::max() / 4;
    const int32_t px = xs[ix];
    const int32_t py = ys[iy];
    for (const LayerShapeBox& gs : goal_access) {
      const int32_t cx = std::clamp(px, gs.rect.get_low_x(), gs.rect.get_high_x());
      const int32_t cy = std::clamp(py, gs.rect.get_low_y(), gs.rect.get_high_y());
      best = std::min<int64_t>(best, static_cast<int64_t>(std::abs(px - cx)) + std::abs(py - cy));
    }
    return best;
  };

  auto segValid = [&](int from_id, int lidx, int32_t x1, int32_t y1, int32_t x2, int32_t y2) -> bool {
    if (!layers[lidx].routing_width_legal) {
      return false;
    }
    const idb::IdbRect wbox = p.router->wireBox(lidx, x1, y1, x2, y2);
    if (!p.router->rectInsideDie(wbox) || !p.router->rectOnMfgGrid(wbox)) {
      return false;
    }
    const bool is_horiz = (y1 == y2);
    int32_t run_x1 = std::min(x1, x2);
    int32_t run_x2 = std::max(x1, x2);
    int32_t run_y1 = std::min(y1, y2);
    int32_t run_y2 = std::max(y1, y2);
    for (int cur = from_id; cur >= 0;) {
      int rem = cur;
      const int cix = rem % nx;
      rem /= nx;
      const int ciy = rem % ny;
      const int cl = rem / ny;
      if (cl != lidx) {
        break;
      }
      const int32_t cx = xs[cix];
      const int32_t cy = ys[ciy];
      if ((is_horiz && cy != y1) || (!is_horiz && cx != x1)) {
        break;
      }
      run_x1 = std::min(run_x1, cx);
      run_x2 = std::max(run_x2, cx);
      run_y1 = std::min(run_y1, cy);
      run_y2 = std::max(run_y2, cy);
      cur = parent[cur];
    }
    const idb::IdbRect run_box = p.router->wireBox(lidx, run_x1, run_y1, run_x2, run_y2);
    for (const RoutingObstacleBox& ob : (*p.obs_by_layer)[lidx]) {
      if (!rectClearOfObstacle(
              run_box, ob.rect,
              p.router->requiredSpacing(lidx, run_box, ob.rect, ob.min_spacing, ob.effective_width))) {
        return false;
      }
    }
    if (p.force_max_spacing_obs != nullptr && lidx < static_cast<int>(p.force_max_spacing_obs->size())) {
      for (const idb::IdbRect& fob : (*p.force_max_spacing_obs)[lidx]) {
        const int64_t par = is_horiz
            ? std::max<int64_t>(0, static_cast<int64_t>(std::min(run_box.get_high_x(), fob.get_high_x())) -
                                       std::max(run_box.get_low_x(), fob.get_low_x()))
            : std::max<int64_t>(0, static_cast<int64_t>(std::min(run_box.get_high_y(), fob.get_high_y())) -
                                       std::max(run_box.get_low_y(), fob.get_low_y()));
        if (par > 0 && !rectClearOfObstacle(run_box, fob, layers[lidx].max_spacing)) {
          return false;
        }
      }
    }
    if (p.force_avoid_obs != nullptr && lidx < static_cast<int>(p.force_avoid_obs->size())) {
      for (const idb::IdbRect& aob : (*p.force_avoid_obs)[lidx]) {
        if (isAvoidSkippedAtPoint(lidx, aob, x1, y1) || isAvoidSkippedAtPoint(lidx, aob, x2, y2)) {
          continue;
        }
        if (!rectClearOfObstacle(run_box, aob, p.router->requiredSpacing(lidx, run_box, aob))) {
          return false;
        }
      }
    }
    return true;
  };

  struct PQItem
  {
    int64_t f;
    int64_t g;
    int id;
    bool operator>(const PQItem& o) const { return f > o.f; }
  };
  std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

  for (const LayerShapeBox& ss : start_access) {
    for (int ix = 0; ix < nx; ++ix) {
      if (xs[ix] < ss.rect.get_low_x() || xs[ix] > ss.rect.get_high_x() ||
          !p.router->coordOnLayerMfgGrid(ss.layer_idx, xs[ix])) {
        continue;
      }
      for (int iy = 0; iy < ny; ++iy) {
        if (ys[iy] < ss.rect.get_low_y() || ys[iy] > ss.rect.get_high_y() ||
            !p.router->coordOnLayerMfgGrid(ss.layer_idx, ys[iy])) {
          continue;
        }
        const int sid = nodeId(ix, iy, ss.layer_idx);
        if (g_cost[sid] > 0) {
          g_cost[sid] = 0;
          pq.push({heuristic(ix, iy), 0, sid});
        }
      }
    }
  }

  auto nextGridIndex = [&](const std::vector<int32_t>& axis, int start_i, int dir, int lidx) -> int {
    const int n = static_cast<int>(axis.size());
    for (int i = start_i + dir; i >= 0 && i < n; i += dir) {
      if (p.router->coordOnLayerMfgGrid(lidx, axis[i])) {
        return i;
      }
    }
    return -1;
  };

  const int64_t via_penalty = std::max<int64_t>(6 * std::max(min_px, min_py), 4000);
  int goal_id = -1;

  while (!pq.empty()) {
    const PQItem top = pq.top();
    pq.pop();
    if (closed[top.id] || top.g > g_cost[top.id]) {
      continue;
    }
    closed[top.id] = true;

    int rem = top.id;
    const int ix = rem % nx;
    rem /= nx;
    const int iy = rem % ny;
    const int l = rem / ny;

    if (isGoalCell(ix, iy, l)) {
      goal_id = top.id;
      break;
    }

    const int32_t x = xs[ix];
    const int32_t y = ys[iy];
    const bool pref_h = layers[l].is_horizontal;
    const bool pref_v = layers[l].is_vertical;

    auto relax = [&](int nx_i, int ny_i, int nl, int64_t step_cost) {
      const int nid = nodeId(nx_i, ny_i, nl);
      if (closed[nid]) {
        return;
      }
      const int64_t ng = top.g + step_cost;
      if (ng < g_cost[nid]) {
        g_cost[nid] = ng;
        parent[nid] = top.id;
        pq.push({ng + heuristic(nx_i, ny_i), ng, nid});
      }
    };

    if (const int prev_ix = nextGridIndex(xs, ix, -1, l); prev_ix >= 0 && segValid(top.id, l, x, y, xs[prev_ix], y)) {
      const int64_t dx = x - xs[prev_ix];
      relax(prev_ix, iy, l, pref_h ? dx : (dx * 6) / 5);
    }
    if (const int next_ix = nextGridIndex(xs, ix, 1, l); next_ix >= 0 && segValid(top.id, l, x, y, xs[next_ix], y)) {
      const int64_t dx = xs[next_ix] - x;
      relax(next_ix, iy, l, pref_h ? dx : (dx * 6) / 5);
    }
    if (const int prev_iy = nextGridIndex(ys, iy, -1, l); prev_iy >= 0 && segValid(top.id, l, x, y, x, ys[prev_iy])) {
      const int64_t dy = y - ys[prev_iy];
      relax(ix, prev_iy, l, pref_v ? dy : (dy * 6) / 5);
    }
    if (const int next_iy = nextGridIndex(ys, iy, 1, l); next_iy >= 0 && segValid(top.id, l, x, y, x, ys[next_iy])) {
      const int64_t dy = ys[next_iy] - y;
      relax(ix, next_iy, l, pref_v ? dy : (dy * 6) / 5);
    }
    if (l + 1 <= p.max_layer_idx && viaValid(top.id, l, x, y)) {
      relax(ix, iy, l + 1, via_penalty);
    }
    if (l > 0 && viaValid(top.id, l - 1, x, y)) {
      relax(ix, iy, l - 1, via_penalty);
    }
  }

  if (goal_id < 0) {
    return failed;
  }

  std::vector<int> chain;
  for (int cur = goal_id; cur >= 0; cur = parent[cur]) {
    chain.push_back(cur);
  }
  std::reverse(chain.begin(), chain.end());
  return reconstructPlan(chain, xs, ys);
}

}  // namespace imj
