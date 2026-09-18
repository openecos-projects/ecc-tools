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
#include "RuleValidator.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace idrc {

namespace {
// ECC_PRL_PROF instrumentation: seed/candidate counts and R-tree probe time share.
struct PrlProf
{
  std::atomic<int64_t> seed_num{0};
  std::atomic<int64_t> cand_num{0};
  std::atomic<int64_t> query_ns{0};
  std::atomic<int64_t> build_ns{0};  // ECC_PRL_GRID: per-(cluster, layer) grid construction
  std::atomic<int64_t> rule_ns{0};   // whole verifyParallelRunLengthSpacing
  std::atomic<int64_t> kept_pairs{0};  // pairs surviving ECC_PRL_HALF canonicalization
};

PrlProf& prlProf()
{
  static PrlProf p;
  return p;
}

bool prlProfEnabled()
{
  static const bool enabled = std::getenv("ECC_PRL_PROF") != nullptr;
  return enabled;
}

struct PrlProfReg
{
  ~PrlProfReg()
  {
    if (!prlProfEnabled()) {
      return;
    }
    PrlProf& p = prlProf();
    std::fprintf(stderr, "[ECC_PRL_PROF] seeds=%ld candidates=%ld query_ms=%.0f avg_cand_per_seed=%.1f\n", p.seed_num.load(), p.cand_num.load(),
                 p.query_ns.load() / 1e6, p.seed_num.load() ? static_cast<double>(p.cand_num.load()) / p.seed_num.load() : 0.0);
    std::fprintf(stderr, "[ECC_PRL_PROF] grid_build_ms=%.0f rule_ms=%.0f kept_pairs=%ld\n", p.build_ns.load() / 1e6, p.rule_ns.load() / 1e6, p.kept_pairs.load());
  }
};

// ECC_PRL_GRID experiment: batch candidate generation via a per-(cluster, layer) uniform grid
// over max_rect_pool, replacing per-seed R-tree probes. The grid query returns exactly the
// closed-interval bbox-intersection set of bgi::intersects (cell gather + stamp dedup + exact
// 4-edge test), so the per-seed candidate set is identical; ordering within the list differs.
struct PrlGrid
{
  int32_t cell = 0;
  int32_t min_x = 0, min_y = 0;
  int32_t nx = 0, ny = 0;
  std::vector<int32_t> cell_begin;  // CSR offsets, size nx*ny+1
  std::vector<int32_t> items;       // pool ids
  std::vector<int32_t> stamp;       // per-pool-id dedup stamp
  int32_t cur_stamp = 0;
};

bool prlGridEnabled()
{
  static const bool enabled = std::getenv("ECC_PRL_GRID") != nullptr;
  return enabled;
}

// ECC_PRL_HALF experiment: the spacing join is symmetric (bbox enlargement by the same
// per-layer spacing both ways; getSpacingRect/getEuclideanDistance/getParallelLength are
// argument-order invariant), so each unordered maxrect pair is generated twice and exact
// duplicates are removed later by sort+unique. Keeping only the env_id > seed_id direction
// halves candidate enumeration. Pairs involving net -1 (never a seed) are always kept.
bool prlHalfEnabled()
{
  static const bool enabled = std::getenv("ECC_PRL_HALF") != nullptr;
  return enabled;
}

void buildPrlGrid(PrlGrid& g, const RVLayerData& rv_layer_data, int32_t check_spacing)
{
  const std::vector<MaxRectData>& pool = rv_layer_data.max_rect_pool;
  const int32_t n = static_cast<int32_t>(pool.size());
  g.cell = std::max(check_spacing, 400);
  if (n == 0) {
    g.nx = g.ny = 0;
    return;
  }
  int32_t min_x = INT32_MAX, min_y = INT32_MAX, max_x = INT32_MIN, max_y = INT32_MIN;
  for (const MaxRectData& data : pool) {
    min_x = std::min(min_x, gtl::xl(data.rect));
    min_y = std::min(min_y, gtl::yl(data.rect));
    max_x = std::max(max_x, gtl::xh(data.rect));
    max_y = std::max(max_y, gtl::yh(data.rect));
  }
  g.min_x = min_x;
  g.min_y = min_y;
  g.nx = (max_x - min_x) / g.cell + 1;
  g.ny = (max_y - min_y) / g.cell + 1;
  const int32_t cell_num = g.nx * g.ny;
  g.cell_begin.assign(cell_num + 1, 0);
  g.stamp.assign(n, -1);
  g.cur_stamp = 0;
  auto cells_of = [&](const GTLRectInt& r, int32_t& cx0, int32_t& cy0, int32_t& cx1, int32_t& cy1) {
    cx0 = std::clamp((gtl::xl(r) - g.min_x) / g.cell, 0, g.nx - 1);
    cy0 = std::clamp((gtl::yl(r) - g.min_y) / g.cell, 0, g.ny - 1);
    cx1 = std::clamp((gtl::xh(r) - g.min_x) / g.cell, 0, g.nx - 1);
    cy1 = std::clamp((gtl::yh(r) - g.min_y) / g.cell, 0, g.ny - 1);
  };
  for (int32_t i = 0; i < n; i++) {
    int32_t cx0, cy0, cx1, cy1;
    cells_of(pool[i].rect, cx0, cy0, cx1, cy1);
    for (int32_t cy = cy0; cy <= cy1; cy++) {
      for (int32_t cx = cx0; cx <= cx1; cx++) {
        g.cell_begin[cy * g.nx + cx + 1]++;
      }
    }
  }
  for (int32_t c = 0; c < cell_num; c++) {
    g.cell_begin[c + 1] += g.cell_begin[c];
  }
  g.items.resize(g.cell_begin[cell_num]);
  std::vector<int32_t> cursor(g.cell_begin.begin(), g.cell_begin.end() - 1);
  for (int32_t i = 0; i < n; i++) {
    int32_t cx0, cy0, cx1, cy1;
    cells_of(pool[i].rect, cx0, cy0, cx1, cy1);
    for (int32_t cy = cy0; cy <= cy1; cy++) {
      for (int32_t cx = cx0; cx <= cx1; cx++) {
        g.items[cursor[cy * g.nx + cx]++] = i;
      }
    }
  }
}

template <typename OutputIt>
void queryPrlGrid(PrlGrid& g, const RVLayerData& rv_layer_data, const GTLRectInt& query_rect, OutputIt out)
{
  if (g.nx == 0 || g.ny == 0) {
    return;
  }
  const std::vector<MaxRectData>& pool = rv_layer_data.max_rect_pool;
  int32_t cx0 = std::clamp((gtl::xl(query_rect) - g.min_x) / g.cell, 0, g.nx - 1);
  int32_t cy0 = std::clamp((gtl::yl(query_rect) - g.min_y) / g.cell, 0, g.ny - 1);
  int32_t cx1 = std::clamp((gtl::xh(query_rect) - g.min_x) / g.cell, 0, g.nx - 1);
  int32_t cy1 = std::clamp((gtl::yh(query_rect) - g.min_y) / g.cell, 0, g.ny - 1);
  const int32_t qxl = gtl::xl(query_rect), qyl = gtl::yl(query_rect), qxh = gtl::xh(query_rect), qyh = gtl::yh(query_rect);
  g.cur_stamp++;
  for (int32_t cy = cy0; cy <= cy1; cy++) {
    for (int32_t cx = cx0; cx <= cx1; cx++) {
      const int32_t cell_id = cy * g.nx + cx;
      for (int32_t p = g.cell_begin[cell_id]; p < g.cell_begin[cell_id + 1]; p++) {
        const int32_t id = g.items[p];
        if (g.stamp[id] == g.cur_stamp) {
          continue;
        }
        g.stamp[id] = g.cur_stamp;
        const GTLRectInt& r = pool[id].rect;
        if (gtl::xl(r) <= qxh && gtl::xh(r) >= qxl && gtl::yl(r) <= qyh && gtl::yh(r) >= qyl) {
          *out++ = {r, id};
        }
      }
    }
  }
}
}  // namespace

void RuleValidator::verifyParallelRunLengthSpacing(RVCluster& rv_cluster)
{
  static PrlProfReg prl_prof_reg;
  auto prl_rule_begin = std::chrono::steady_clock::now();
  struct RuleTimer
  {
    std::chrono::steady_clock::time_point begin;
    ~RuleTimer()
    {
      if (prlProfEnabled()) {
        prlProf().rule_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count(),
                                    std::memory_order_relaxed);
      }
    }
  } rule_timer{prl_rule_begin};
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  const auto& layer_data = rv_cluster.get_layer_data();
  int64_t kept_pairs_local = 0;

  for (auto& [routing_layer_idx, rv_layer_data] : layer_data) {
    RoutingLayer& routing_layer = routing_layer_list[routing_layer_idx];
    ParallelRunLengthSpacingRule& parallel_run_length_spacing_rule = routing_layer.get_parallel_run_length_spacing_rule();
    std::map<std::set<int32_t>, std::map<int32_t, std::vector<PlanarRect>>> net_required_violation_rect_map;
    std::map<std::set<int32_t>, std::map<int32_t, std::vector<PlanarRect>>> env_net_required_violation_rect_map;
    PrlGrid prl_grid;
    if (prlGridEnabled()) {
      auto grid_build_begin = std::chrono::steady_clock::now();
      int32_t grid_spacing =
          std::max(parallel_run_length_spacing_rule.has_spacing_table ? parallel_run_length_spacing_rule.getMaxSpacing() : 0,
                   parallel_run_length_spacing_rule.has_spacing_list ? parallel_run_length_spacing_rule.getSpacingMaxWidth() : 0);
      buildPrlGrid(prl_grid, rv_layer_data, grid_spacing);
      if (prlProfEnabled()) {
        prlProf().build_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - grid_build_begin).count(),
                                     std::memory_order_relaxed);
      }
    }
    for (auto& [net_idx, routing_net] : rv_layer_data.nets) {
      for (const MaxRectData& max_rect_data : rv_layer_data.getMaxRects(routing_net)) {
        PlanarRect rect = DRCUTIL.convertToPlanarRect(max_rect_data.rect);
        if (net_idx == -1) {
          continue;
        }
        const int32_t seed_max_rect_id = prlHalfEnabled() ? rv_layer_data.getMaxRectId(max_rect_data) : -1;
        bool has_spacing_table = parallel_run_length_spacing_rule.has_spacing_table;
        bool has_spacing_list = parallel_run_length_spacing_rule.has_spacing_list;

        std::vector<std::pair<GTLRectInt, int32_t>> neighbor_rect_id_list;
        {
          int32_t spacing_table_check = has_spacing_table ? parallel_run_length_spacing_rule.getMaxSpacing() : 0;
          int32_t spacing_list_check = has_spacing_list ? parallel_run_length_spacing_rule.getSpacingMaxWidth() : 0;
          int32_t check_spacing = std::max(spacing_table_check, spacing_list_check);

          PlanarRect check_rect = DRCUTIL.getEnlargedRect(rect, check_spacing);
          auto prl_query_begin = std::chrono::steady_clock::now();
          if (prlGridEnabled()) {
            queryPrlGrid(prl_grid, rv_layer_data, DRCUTIL.convertToGTLRectInt(check_rect), std::back_inserter(neighbor_rect_id_list));
          } else {
            rv_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(check_rect), std::back_inserter(neighbor_rect_id_list));
          }
          if (prlProfEnabled()) {
            prlProf().query_ns.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - prl_query_begin).count(),
                                         std::memory_order_relaxed);
            prlProf().seed_num.fetch_add(1, std::memory_order_relaxed);
            prlProf().cand_num.fetch_add(neighbor_rect_id_list.size(), std::memory_order_relaxed);
          }
        }

        for (const auto& [gtl_rect, env_max_rect_id] : neighbor_rect_id_list) {
          PlanarRect env_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          int32_t env_net_idx = rv_layer_data.getNetIdxByMaxRectId(env_max_rect_id);
          if (DRCUTIL.isClosedOverlap(rect, env_rect)) {
            continue;
          }
          if (prlHalfEnabled() && env_net_idx != -1 && env_max_rect_id < seed_max_rect_id) {
            continue;
          }
          if (prlProfEnabled() && prlHalfEnabled()) {
            kept_pairs_local++;
          }

          PlanarRect violation_rect = DRCUTIL.getSpacingRect(rect, env_rect);
          bool is_prl_violation = false, is_spacing_violation = false;
          int32_t real_prl_spacing = 0, real_spacing = 0;
          // prl rules
          if (has_spacing_table) {
            int32_t prl = DRCUTIL.getParallelLength(rect, env_rect);
            real_prl_spacing = parallel_run_length_spacing_rule.getSpacing(std::max(rect.getWidth(), env_rect.getWidth()), prl);
            is_prl_violation = DRCUTIL.getEuclideanDistance(rect, env_rect) < real_prl_spacing;
          }

          // spacing rules
          if (has_spacing_list) {
            real_spacing = parallel_run_length_spacing_rule.getSpacingWithWidth(std::max(rect.getWidth(), env_rect.getWidth()));
            is_spacing_violation = DRCUTIL.getEuclideanDistance(rect, env_rect) < real_spacing;
          }

          if (!is_prl_violation && !is_spacing_violation) {
            continue;
          }

          // sameNet
          if (net_idx == env_net_idx) {
            std::set<Orientation> orient_inside;
            bool total_inside = false;
            // for violation area = 0
            bool zero_area_inside = false;
            GTLPolySetInt violation_ps;
            violation_ps += DRCUTIL.convertToGTLRectInt(violation_rect);
            for (const auto& [gtl_rect, max_rect_id] : neighbor_rect_id_list) {
              PlanarRect violation_env_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
              int32_t violation_env_net_idx = rv_layer_data.getNetIdxByMaxRectId(max_rect_id);
              if (violation_env_net_idx == net_idx) {
                if (DRCUTIL.isOpenOverlap(violation_env_rect, violation_rect)) {
                  violation_ps -= gtl_rect;
                }
                if (gtl::empty(violation_ps)) {
                  total_inside = true;
                }
                if (DRCUTIL.isInside(violation_env_rect, violation_rect)) {
                  zero_area_inside = true;
                }

                for (auto orient : {Orientation::kEast, Orientation::kWest, Orientation::kNorth, Orientation::kSouth}) {
                  if (!DRCUTIL.exist(orient_inside, orient) && DRCUTIL.isInside(violation_env_rect, violation_rect.getOrientEdge(orient))) {
                    orient_inside.insert(orient);
                  }
                }
              }
            }
            bool hor = DRCUTIL.exist(orient_inside, Orientation::kWest) && DRCUTIL.exist(orient_inside, Orientation::kEast);
            bool ver = DRCUTIL.exist(orient_inside, Orientation::kNorth) && DRCUTIL.exist(orient_inside, Orientation::kSouth);

            if (violation_rect.getArea() == 0) {
              if (zero_area_inside) {
                continue;
              }
            } else if ((orient_inside.size() != 0 && !hor && !ver) || total_inside) {
              continue;
            } else {
              GTLRectInt violation_bbox;
              violation_ps.extents(violation_bbox);
              violation_rect = DRCUTIL.convertToPlanarRect(violation_bbox);
            }
          }

          // diffNet
          if (net_idx != env_net_idx && DRCUTIL.getParallelLength(rect, env_rect) > 0) {
            bool total_inside = false;
            GTLPolySetInt violation_ps;
            violation_ps += DRCUTIL.convertToGTLRectInt(violation_rect);
            for (const auto& [gtl_rect, max_rect_id] : neighbor_rect_id_list) {
              PlanarRect violation_env_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
              if (DRCUTIL.isOpenOverlap(violation_env_rect, violation_rect)) {
                violation_ps -= gtl_rect;
              }
            }
            if (gtl::empty(violation_ps)) {
              total_inside = true;
            }
            GTLRectInt violation_bbox;
            violation_ps.extents(violation_bbox);
            PlanarRect new_violation_rect = DRCUTIL.convertToPlanarRect(violation_bbox);
            if (!DRCUTIL.isClosedOverlap(new_violation_rect, rect) || !DRCUTIL.isClosedOverlap(new_violation_rect, env_rect)) {
              total_inside = true;
            }

            if (total_inside) {
              continue;
            }
          }

          if (max_rect_data.isEnv && rv_layer_data.getMaxRect(env_max_rect_id).isEnv) {
            if (is_prl_violation) {
              env_net_required_violation_rect_map[{net_idx, env_net_idx}][real_prl_spacing].push_back(violation_rect);
            }
            if (is_spacing_violation) {
              env_net_required_violation_rect_map[{net_idx, env_net_idx}][real_spacing].push_back(violation_rect);
            }
          } else {
            if (is_prl_violation) {
              net_required_violation_rect_map[{net_idx, env_net_idx}][real_prl_spacing].push_back(violation_rect);
            }
            if (is_spacing_violation) {
              net_required_violation_rect_map[{net_idx, env_net_idx}][real_spacing].push_back(violation_rect);
            }
          }
        }
      }
    }

    std::map<std::set<int32_t>, std::map<int32_t, std::vector<PlanarRect>>> exclude_env;
    for (auto& [violation_net_set, required_violation_rect_map] : net_required_violation_rect_map) {
      for (auto& [required_size, violation_rect_list] : required_violation_rect_map) {
        for (PlanarRect& violation_rect : violation_rect_list) {
          auto& env_violations = env_net_required_violation_rect_map[violation_net_set][required_size];
          bool is_env_inside = false;
          for (auto& env_violation : env_violations) {
            bool closed_inside = (violation_rect.getXSpan() == env_violation.getXSpan()) || (violation_rect.getYSpan() == env_violation.getYSpan());
            if (DRCUTIL.isInside(env_violation, violation_rect) && closed_inside) {
              is_env_inside = true;
              break;
            }
          }
          if (is_env_inside) {
            continue;
          }
          exclude_env[violation_net_set][required_size].push_back(violation_rect);
        }
      }
    }

    for (auto& [violation_net_set, required_violation_rect_map] : exclude_env) {
      for (auto& [required_size, violation_rect_list] : required_violation_rect_map) {
        for (PlanarRect& violation_rect : violation_rect_list) {
          bool is_inside = false;
          for (PlanarRect& other_violation_rect : violation_rect_list) {
            if (other_violation_rect == violation_rect) {
              continue;
            }
            if (DRCUTIL.isInside(other_violation_rect, violation_rect)) {
              is_inside = true;
              break;
            }
          }
          if (!is_inside) {
            Violation violation;
            violation.set_violation_type(ViolationType::kParallelRunLengthSpacing);
            violation.set_is_routing(true);
            violation.set_violation_net_set(violation_net_set);
            violation.set_layer_idx(routing_layer_idx);
            violation.set_rect(violation_rect);
            violation.set_required_size(required_size);
            rv_cluster.get_violation_list().push_back(violation);
          }
        }
      }
    }
  }
  if (prlProfEnabled() && prlHalfEnabled()) {
    prlProf().kept_pairs.fetch_add(kept_pairs_local, std::memory_order_relaxed);
  }
}

}  // namespace idrc
