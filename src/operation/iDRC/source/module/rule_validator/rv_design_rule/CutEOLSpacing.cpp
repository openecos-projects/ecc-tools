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

#include <array>
#include <set>

namespace idrc {

namespace {

bool isGlobalEolBoundary(const RVLayerData& rv_layer_data, int32_t boundary_id);
void addCutEolViolation(std::set<std::array<int32_t, 8>>& violation_key_set, RVCluster& rv_cluster,
                        int32_t violation_routing_layer_idx, const PlanarRect& violation_rect, int32_t required_size,
                        int32_t cut_net_idx, int32_t env_net_idx);

}  // namespace

void RuleValidator::verifyCutEOLSpacing(RVCluster& rv_cluster)
{
  const auto orientations = {Orientation::kEast, Orientation::kSouth, Orientation::kWest, Orientation::kNorth};
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  const auto& layer_data = rv_cluster.get_layer_data();
  std::set<std::array<int32_t, 8>> cut_eol_violation_key_set;

  // Build global EOL edges only for polygons reached by active cuts.
  std::map<int32_t, std::map<int32_t, std::map<PlanarRect, int32_t, CmpPlanarRectByXASC>>> layer_net_eol_edge_map;
  std::map<int32_t, std::vector<int32_t>> layer_polygon_checked_eol_width_map;

  for (const auto& [cut_layer_idx, cut_layer_data] : layer_data) {
    if (cut_layer_data.cut_pool.empty()) {
      continue;
    }

    const std::vector<int32_t>& routing_layer_idx_list = DRCDM.getAdjacentRoutingLayerIdxList(cut_layer_idx);
    int32_t routing_layer_idx = *std::max_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    int32_t violation_routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    auto routing_layer_data_it = layer_data.find(routing_layer_idx);
    if (routing_layer_data_it == layer_data.end()) {
      continue;
    }
    const RVLayerData& routing_layer_data = routing_layer_data_it->second;

    CutEOLSpacingRule& cut_eol_spacing_rule = cut_layer_list[cut_layer_idx].get_cut_eol_spacing_rule();
    int32_t eol_spacing = cut_eol_spacing_rule.eol_spacing;
    int32_t eol_prl = cut_eol_spacing_rule.eol_prl;
    int32_t eol_prl_spacing = cut_eol_spacing_rule.eol_prl_spacing;
    int32_t eol_width = cut_eol_spacing_rule.eol_width;
    int32_t smaller_overhang = cut_eol_spacing_rule.smaller_overhang;
    int32_t equal_overhang = cut_eol_spacing_rule.equal_overhang;
    int32_t side_ext = cut_eol_spacing_rule.side_ext;
    int32_t backward_ext = cut_eol_spacing_rule.backward_ext;
    int32_t span_length = cut_eol_spacing_rule.span_length;
    int32_t abs_eol_prl = std::abs(eol_prl);
    int32_t cut_search_spacing = std::max({eol_spacing, eol_prl_spacing, abs_eol_prl});
    std::vector<int32_t>& polygon_checked_eol_width_list = layer_polygon_checked_eol_width_map[routing_layer_idx];
    if (polygon_checked_eol_width_list.empty()) {
      polygon_checked_eol_width_list.assign(routing_layer_data.polygon_pool.size(), -1);
    }

    for (const CutData& cut_data : cut_layer_data.getCuts()) {
      if (cut_data.net_idx == -1) {
        continue;
      }

      int32_t cut_net_idx = cut_data.net_idx;
      PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_data.rect);

      // pre-filter out negative position
      PlanarRect vertical_prl_region = DRCUTIL.getEnlargedRect(cut_rect, abs_eol_prl, eol_prl_spacing);
      PlanarRect horizontal_prl_region = DRCUTIL.getEnlargedRect(cut_rect, eol_prl_spacing, abs_eol_prl);
      std::vector<CutData> cut_hit_list;
      cut_layer_data.queryCuts(DRCUTIL.convertToGTLRectInt(DRCUTIL.getEnlargedRect(cut_rect, cut_search_spacing)),
                               std::back_inserter(cut_hit_list));
      std::vector<CutData> overlap_cut_list;
      overlap_cut_list.reserve(cut_hit_list.size());
      for (const CutData& overlap_cut_data : cut_hit_list) {
        PlanarRect env_cut_rect = DRCUTIL.convertToPlanarRect(overlap_cut_data.rect);
        if (cut_rect == env_cut_rect || (cut_net_idx == -1 && overlap_cut_data.net_idx == -1)) {
          continue;
        }
        bool within_eol_spacing = DRCUTIL.getEuclideanDistance(cut_rect, env_cut_rect) < eol_spacing;
        bool within_prl_region = DRCUTIL.isOpenOverlap(vertical_prl_region, env_cut_rect)
                                 || DRCUTIL.isOpenOverlap(horizontal_prl_region, env_cut_rect);
        bool within_eol_prl_spacing
            = within_prl_region && DRCUTIL.getProjectionDistance(cut_rect, env_cut_rect) < eol_prl_spacing;
        if (within_eol_spacing || within_eol_prl_spacing) {
          overlap_cut_list.push_back(overlap_cut_data);
        }
      }
      if (overlap_cut_list.empty()) {
        continue;
      }

      // for each via, get overlaped metal
      std::vector<std::pair<GTLRectInt, int32_t>> overlaped_rect;
      routing_layer_data.queryMaxRects(cut_data.rect, std::back_inserter(overlaped_rect));

      for (const auto& [gtl_rect, max_rect_id] : overlaped_rect) {
        if (!DRCUTIL.isOpenOverlap(DRCUTIL.convertToPlanarRect(gtl_rect), cut_rect)) {
          continue;
        }
        int32_t polygon_id = routing_layer_data.getMaxRect(max_rect_id).polygon_id;
        int32_t& checked_eol_width = polygon_checked_eol_width_list[polygon_id];
        if (checked_eol_width >= eol_width) {
          continue;
        }

        const PolygonData& polygon_data = routing_layer_data.getPolygon(polygon_id);
        for (const BoundaryData& boundary_data : routing_layer_data.getBoundaries(polygon_data)) {
          if (boundary_data.edge_length < checked_eol_width || boundary_data.edge_length >= eol_width) {
            continue;
          }
          int32_t boundary_id = routing_layer_data.getBoundaryId(boundary_data);
          if (!isGlobalEolBoundary(routing_layer_data, boundary_id)) {
            continue;
          }
          layer_net_eol_edge_map[routing_layer_idx][polygon_data.net_id][DRCUTIL.convertToPlanarRect(boundary_data.edge)]
              = boundary_data.edge_length;
        }
        checked_eol_width = eol_width;
      }

      std::map<Orientation, int32_t> orient_overhang_map
          = {{Orientation::kEast, 0}, {Orientation::kSouth, 0}, {Orientation::kWest, 0}, {Orientation::kNorth, 0}};
      for (const auto& [gtl_rect, max_rect_id] : overlaped_rect) {
        (void) max_rect_id;
        PlanarRect span_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
        if (!DRCUTIL.isOpenOverlap(span_rect, cut_rect))
          continue;

        for (auto orient : orientations) {
          if (DRCUTIL.isInside(span_rect, DRCUTIL.getRect(cut_rect.getOrientEdge(orient)))) {
            auto& current_max = orient_overhang_map[orient];
            current_max = std::max(current_max, DRCUTIL.getOrientEdgeDistance(cut_rect, span_rect, orient));
          }
        }
      }
      std::vector<std::pair<Orientation, Orientation>> check_ortho_orients;
      for (auto check_orient : orientations) {
        if (orient_overhang_map[check_orient] >= smaller_overhang) {
          continue;
        }
        auto ortho_orients = DRCUTIL.getOrthogonalOrientationList(check_orient);
        for (auto ortho_orient : ortho_orients) {
          if (orient_overhang_map[ortho_orient] == equal_overhang) {
            check_ortho_orients.push_back({check_orient, ortho_orient});
          }
        }
      }

      if (check_ortho_orients.empty()) {
        continue;
      }

      for (auto& [check_orient, ortho_orient] : check_ortho_orients) {
        Direction direction = (check_orient == Orientation::kNorth || check_orient == Orientation::kSouth) ? Direction::kVertical : Direction::kHorizontal;
        for (const auto& [gtl_rect, max_rect_id] : overlaped_rect) {
          int32_t routing_net_idx = routing_layer_data.getNetIdxByMaxRectId(max_rect_id);
          PlanarRect routing_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          PlanarRect check_edge = DRCUTIL.getRect(routing_rect.getOrientEdge(check_orient));
          if (!DRCUTIL.isInside(routing_rect, cut_rect)) {
            continue;
          }

          std::map<Orientation, int32_t> routing_rect_overhangs;
          for (auto orient : orientations) {
            routing_rect_overhangs[orient] = DRCUTIL.getOrientEdgeDistance(routing_rect, cut_rect, orient);
          }
          if (routing_rect_overhangs[check_orient] >= smaller_overhang || routing_rect_overhangs[ortho_orient] != equal_overhang) {
            continue;
          }

          // 与routing rect相交的矩形，计算span length
          int32_t ll_span = 0, ur_span = 0;
          std::vector<std::pair<GTLRectInt, int32_t>> rect_overlap_bg_list;
          routing_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(routing_rect), std::back_inserter(rect_overlap_bg_list));
          GTLPolySetInt all_neighboors;
          for (const auto& [gtl_overlap_env, overlap_max_rect_id] : rect_overlap_bg_list) {
            (void) overlap_max_rect_id;
            all_neighboors += gtl_overlap_env;
            PlanarRect overlap_env = DRCUTIL.convertToPlanarRect(gtl_overlap_env);
            if (!DRCUTIL.isOpenOverlap(overlap_env, cut_rect)) {
              continue;
            }
            if (DRCUTIL.isInside(DRCUTIL.getRect(overlap_env.getOrientEdge(check_orient)), check_edge)) {
              check_edge = DRCUTIL.getRect(overlap_env.getOrientEdge(check_orient));
            }
            if (direction == Direction::kVertical) {
              ll_span = std::max(ll_span, cut_rect.get_ll_x() - overlap_env.get_ll_x());
              ur_span = std::max(ur_span, overlap_env.get_ur_x() - cut_rect.get_ur_x());
            } else if (direction == Direction::kHorizontal) {
              ll_span = std::max(ll_span, cut_rect.get_ll_y() - overlap_env.get_ll_y());
              ur_span = std::max(ur_span, overlap_env.get_ur_y() - cut_rect.get_ur_y());
            }
          }

          // has_wide_span 或者 eol， 进一步检查
          int32_t other_back_side_span;
          if (ortho_orient == Orientation::kWest || ortho_orient == Orientation::kSouth) {
            other_back_side_span = ur_span;
          } else {
            other_back_side_span = ll_span;
          }
          // if (back_side_span + routing_rect.getWidth() >= span_length) { continue; }
          bool has_ll_span = (ll_span + routing_rect.getWidth()) >= span_length;
          bool has_ur_span = (ur_span + routing_rect.getWidth() >= span_length);
          bool has_wide_span = has_ll_span || has_ur_span;
          bool is_eol = false;
          if (DRCUTIL.exist(layer_net_eol_edge_map[routing_layer_idx][routing_net_idx], check_edge)) {
            if (layer_net_eol_edge_map[routing_layer_idx][routing_net_idx][check_edge] < eol_width) {
              is_eol = true;
            }
          }
          if (!has_wide_span && !is_eol) {
            continue;
          }

          int32_t ll_ext = has_ll_span ? span_length : side_ext;
          int32_t ur_ext = has_ur_span ? span_length : side_ext;

          PlanarRect back_side, other_back_side;

          // ll 表示west或者south， ur表示east north
          PlanarRect ur_rect, ll_rect;
          if (check_orient == Orientation::kEast) {
            ur_rect = PlanarRect{routing_rect.get_ur_x() - backward_ext, routing_rect.get_ur_y(), routing_rect.get_ur_x(), routing_rect.get_ur_y() + ur_ext};
            ll_rect = PlanarRect{routing_rect.get_ur_x() - backward_ext, routing_rect.get_ll_y() - ll_ext, routing_rect.get_ur_x(), routing_rect.get_ll_y()};
          } else if (check_orient == Orientation::kSouth) {
            ur_rect = PlanarRect{routing_rect.get_ur_x(), routing_rect.get_ll_y(), routing_rect.get_ur_x() + ur_ext, routing_rect.get_ll_y() + backward_ext};
            ll_rect = PlanarRect{routing_rect.get_ll_x() - ll_ext, routing_rect.get_ll_y(), routing_rect.get_ll_x(), routing_rect.get_ll_y() + backward_ext};
          } else if (check_orient == Orientation::kWest) {
            ll_rect = PlanarRect{routing_rect.get_ll_x(), routing_rect.get_ll_y() - ll_ext, routing_rect.get_ll_x() + backward_ext, routing_rect.get_ll_y()};
            ur_rect = PlanarRect{routing_rect.get_ll_x(), routing_rect.get_ur_y(), routing_rect.get_ll_x() + backward_ext, routing_rect.get_ur_y() + ur_ext};
          } else if (check_orient == Orientation::kNorth) {
            ll_rect = {routing_rect.get_ll_x() - ll_ext, routing_rect.get_ur_y() - backward_ext, routing_rect.get_ll_x(), routing_rect.get_ur_y()};
            ur_rect = {routing_rect.get_ur_x(), routing_rect.get_ur_y() - backward_ext, routing_rect.get_ur_x() + ur_ext, routing_rect.get_ur_y()};
          }
          if (ortho_orient == Orientation::kEast || ortho_orient == Orientation::kNorth) {
            back_side = ur_rect;
            other_back_side = ll_rect;
          } else {
            back_side = ll_rect;
            other_back_side = ur_rect;
          }

          std::vector<std::pair<GTLRectInt, int32_t>> window_overlap_rect_list;
          routing_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(back_side), std::back_inserter(window_overlap_rect_list));
          GTLPolySetInt neighbor_shape;
          neighbor_shape += DRCUTIL.convertToGTLRectInt(back_side);
          neighbor_shape -= all_neighboors;

          for (const auto& [gtl_rect_window, env_max_rect_id] : window_overlap_rect_list) {
            if (DRCUTIL.isClosedOverlap(DRCUTIL.convertToPlanarRect(gtl_rect_window), routing_rect)) {
              neighbor_shape -= gtl_rect_window;
            }
          }
          std::vector<GTLPolyInt> non_overlap_polygons;
          neighbor_shape.get(non_overlap_polygons);
          PlanarRect empty_region;
          for (auto& polygon : non_overlap_polygons) {
            GTLRectInt empty_gtl_rect;
            gtl::extents(empty_gtl_rect, polygon);
            if (DRCUTIL.isClosedOverlap(check_edge, DRCUTIL.convertToPlanarRect(empty_gtl_rect))) {
              empty_region = DRCUTIL.convertToPlanarRect(empty_gtl_rect);
              break;
            }
          }
          empty_region = DRCUTIL.getOverlap(empty_region, back_side);

          bool has_check_overlap = false;
          PlanarRect check_overlap_rect;
          for (const auto& [gtl_env_rect, env_max_rect_id] : window_overlap_rect_list) {
            (void) env_max_rect_id;
            PlanarRect env_rect = DRCUTIL.convertToPlanarRect(gtl_env_rect);
            if (DRCUTIL.isClosedOverlap(routing_rect, env_rect)) {
              continue;
            }
            if (DRCUTIL.isOpenOverlap(empty_region, env_rect)) {
              has_check_overlap = true;
              check_overlap_rect = env_rect;
              break;
            }
          }

          std::vector<std::pair<GTLRectInt, int32_t>> other_window_overlap_rect_list;
          bool has_other_overlap = false;
          PlanarRect other_enlarge_rect = DRCUTIL.getEnlargedRect(routing_rect, DRCUTIL.getOppositeOrientation(ortho_orient), other_back_side_span);
          if (has_check_overlap) {
            routing_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(other_back_side), std::back_inserter(other_window_overlap_rect_list));
            for (const auto& [gtl_env_rect, env_max_rect_id] : other_window_overlap_rect_list) {
              (void) env_max_rect_id;
              PlanarRect env_rect = DRCUTIL.convertToPlanarRect(gtl_env_rect);
              if (DRCUTIL.isClosedOverlap(env_rect, other_enlarge_rect)) {
                continue;
              }
              if (DRCUTIL.isOpenOverlap(other_back_side, env_rect)) {
                has_other_overlap = true;
                break;
              }
            }
          }

          std::vector<Orientation> checking_orient_list = {DRCUTIL.getOppositeOrientation(check_orient), DRCUTIL.getOppositeOrientation(ortho_orient)};
          if (!has_wide_span && is_eol) {
            checking_orient_list.push_back(check_orient);
          }
          if (!has_check_overlap) {
            continue;
          }
          if (has_other_overlap) {
            continue;
          }
          for (auto checking_orient : checking_orient_list) {
            for (const CutData& overlap_cut_data : overlap_cut_list) {
              int32_t env_net_idx = overlap_cut_data.net_idx;
              bool is_netless_env_cut = overlap_cut_data.net_idx == -1;
              PlanarRect env_cut_rect = DRCUTIL.convertToPlanarRect(overlap_cut_data.rect);
              if (cut_rect == env_cut_rect) {
                continue;
              }
              if (checking_orient == check_orient && !is_netless_env_cut) {
                continue;
              }
              if (is_netless_env_cut) {
                if (checking_orient == check_orient) {
                  if (has_wide_span || !is_eol) {
                    continue;
                  }
                } else if (checking_orient == DRCUTIL.getOppositeOrientation(check_orient)) {
                  if (routing_rect_overhangs[checking_orient] < cut_rect.getWidth()) {
                    continue;
                  }
                } else {
                  continue;
                }
                if (checking_orient == check_orient
                    && routing_rect_overhangs[DRCUTIL.getOppositeOrientation(checking_orient)] >= smaller_overhang) {
                  continue;
                }
              }
              PlanarRect rect = DRCUTIL.getRect(cut_rect.getOrientEdge(checking_orient));
              PlanarRect orient_rect = rect;
              if (checking_orient == Orientation::kNorth || checking_orient == Orientation::kSouth) {
                rect = DRCUTIL.getEnlargedRect(rect, std::abs(eol_prl), 0);
                rect = DRCUTIL.getEnlargedPartRect(rect, checking_orient, eol_prl_spacing);
              } else {
                rect = DRCUTIL.getEnlargedRect(rect, 0, std::abs(eol_prl));
                rect = DRCUTIL.getEnlargedPartRect(rect, checking_orient, eol_prl_spacing);
              }
              if (checking_orient == Orientation::kEast) {
                if (orient_rect.get_ll_x() >= env_cut_rect.get_ur_x()) {
                  continue;
                }
              } else if (checking_orient == Orientation::kSouth) {
                if (orient_rect.get_ll_y() <= env_cut_rect.get_ll_y()) {
                  continue;
                }
              } else if (checking_orient == Orientation::kWest) {
                if (orient_rect.get_ll_x() <= env_cut_rect.get_ll_x()) {
                  continue;
                }
              } else {
                if (orient_rect.get_ll_y() >= env_cut_rect.get_ur_y()) {
                  continue;
                }
              }
              bool use_project_distance = DRCUTIL.isOpenOverlap(rect, env_cut_rect);
              int32_t required_size = use_project_distance ? eol_prl_spacing : eol_spacing;

              if (use_project_distance) {
                int32_t projection_distance = DRCUTIL.getProjectionDistance(cut_rect, env_cut_rect);
                if (projection_distance >= required_size) {
                  continue;
                }
              } else {
                int32_t euclidean_distance = DRCUTIL.getEuclideanDistance(orient_rect, env_cut_rect);
                if (euclidean_distance >= required_size) {
                  continue;
                }
              }

              // VIAx的违例输出Mx
              PlanarRect violation_rect;
              if (DRCUTIL.isClosedOverlap(cut_rect, env_cut_rect)) {
                violation_rect = DRCUTIL.getOverlap(cut_rect, env_cut_rect);
              } else {
                violation_rect = DRCUTIL.getSpacingRect(cut_rect, env_cut_rect);
              }
              // 排除env的影响
              // 如果只使用env可以有相同的span length 和相同的 env rect
              {
                std::vector<std::pair<GTLRectInt, int32_t>> window_overlap_rect_list;
                routing_layer_data.queryEnvRects(DRCUTIL.convertToGTLRectInt(violation_rect), std::back_inserter(window_overlap_rect_list));
                bool has_check_edge = false;
                for (const auto& [gtl_env_rect, env_routing_net_idx] : window_overlap_rect_list) {
                  (void) env_routing_net_idx;
                  PlanarRect env_rect = DRCUTIL.convertToPlanarRect(gtl_env_rect);
                  if (DRCUTIL.isClosedOverlap(routing_rect, env_rect)) {
                    for (auto env_orient : orientations) {
                      if (check_edge == DRCUTIL.getRect(env_rect.getOrientEdge(env_orient))) {
                        has_check_edge = true;
                        break;
                      }
                    }
                  }
                }
                if (has_check_edge) {
                  bool has_env_overlap = false;
                  for (const auto& [gtl_env_rect, env_routing_net_idx] : window_overlap_rect_list) {
                    (void) env_routing_net_idx;
                    PlanarRect env_rect = DRCUTIL.convertToPlanarRect(gtl_env_rect);
                    if (DRCUTIL.isOpenOverlap(check_overlap_rect, env_rect) && DRCUTIL.isClosedOverlap(violation_rect, env_rect)) {
                      has_env_overlap = true;
                      break;
                    }
                  }
                  if (has_env_overlap) {
                    continue;
                  }
                }
              }
              addCutEolViolation(cut_eol_violation_key_set, rv_cluster, violation_routing_layer_idx, violation_rect, required_size,
                                 cut_net_idx, env_net_idx);
            }
          }
        }
      }
    }
  }
}

namespace {

bool isGlobalEolBoundary(const RVLayerData& rv_layer_data, int32_t boundary_id)
{
  const BoundaryData& curr_boundary = rv_layer_data.getBoundary(boundary_id);
  const BoundaryData& prev_boundary = rv_layer_data.getPrevBoundary(boundary_id);
  if (!prev_boundary.isConvex || !curr_boundary.isConvex) {
    return false;
  }

  int32_t net_idx = rv_layer_data.getNetIdxByBoundaryId(boundary_id);
  std::vector<std::pair<GTLRectInt, int32_t>> rect_hits;
  rv_layer_data.queryMaxRects(curr_boundary.edge, std::back_inserter(rect_hits));
  for (const auto& [gtl_rect, max_rect_id] : rect_hits) {
    if (rv_layer_data.getNetIdxByMaxRectId(max_rect_id) == net_idx) {
      continue;
    }
    PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);
    PlanarRect edge_rect = DRCUTIL.convertToPlanarRect(curr_boundary.edge);
    if (DRCUTIL.isInside(rect, edge_rect) && DRCUTIL.isOpenOverlap(rect, edge_rect)) {
      return false;
    }
  }
  return true;
}

void addCutEolViolation(std::set<std::array<int32_t, 8>>& violation_key_set, RVCluster& rv_cluster,
                        int32_t violation_routing_layer_idx, const PlanarRect& violation_rect, int32_t required_size,
                        int32_t cut_net_idx, int32_t env_net_idx)
{
  std::array<int32_t, 8> violation_key = {violation_routing_layer_idx,
                                          violation_rect.get_ll_x(),
                                          violation_rect.get_ll_y(),
                                          violation_rect.get_ur_x(),
                                          violation_rect.get_ur_y(),
                                          required_size,
                                          std::min(cut_net_idx, env_net_idx),
                                          std::max(cut_net_idx, env_net_idx)};
  if (violation_key_set.contains(violation_key)) {
    return;
  }
  violation_key_set.insert(violation_key);

  Violation violation;
  violation.set_violation_type(ViolationType::kCutEOLSpacing);
  violation.set_is_routing(true);
  violation.set_violation_net_set({cut_net_idx, env_net_idx});
  violation.set_layer_idx(violation_routing_layer_idx);
  violation.set_rect(violation_rect);
  violation.set_required_size(required_size);
  rv_cluster.get_violation_list().push_back(violation);
}

}  // namespace

}  // namespace idrc
