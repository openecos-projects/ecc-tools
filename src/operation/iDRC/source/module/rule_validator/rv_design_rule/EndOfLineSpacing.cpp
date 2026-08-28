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
#include <boost/pending/disjoint_sets.hpp>

#include "DataManager.hpp"
#include "Orientation.hpp"
#include "PlanarRect.hpp"
#include "RVLayerData.hpp"
#include "RoutingLayer.hpp"
#include "RuleValidator.hpp"
#include "Utility.hpp"

namespace idrc {

namespace {

struct EolSpacingCandidate
{
  GTLRectInt rect;
  int32_t boundary_id = -1;
};

int32_t queryNetIdxByRect(const RVLayerData& rv_layer_data, const PlanarRect& query_rect);
int32_t getBoundaryMinThickness(const RVLayerData& rv_layer_data, std::vector<int32_t>& boundary_min_thickness_cache, int32_t boundary_id);
void buildLayerComponentData(RVLayerData& merged_layer_data, const RVLayerData& source_layer_data);
void collectEolBoundaries(const RVLayerData& merged_layer_data, int32_t max_eol_width,
                         std::vector<int32_t>& eol_boundary_id_list);
bool getPolygonBoundingBox(const RVLayerData& rv_layer_data, const PolygonData& polygon_data, GTLRectInt& bounding_box);
bool polygonsHaveClosedOverlap(const RVLayerData& rv_layer_data, int32_t first_polygon_id, int32_t second_polygon_id);
bool hasRectInside(const RVLayerData& rv_layer_data, const PlanarRect& target_rect);
bool hasRectInsideCached(const RVLayerData& rv_layer_data, const PlanarRect& target_rect,
                         std::map<PlanarRect, bool, CmpPlanarRectByXASC>& contains_cache);

}  // namespace

void RuleValidator::verifyEndOfLineSpacing(RVCluster& rv_cluster)
{
  const auto& layer_data = rv_cluster.get_layer_data();

  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  // Check each routing layer.
  for (const auto& [routing_layer_idx, rv_layer_data] : layer_data) {
    if (rv_layer_data.polygon_pool.empty()) {
      continue;
    }
    const auto& end_of_line_spacing_rule_list = routing_layer_list[routing_layer_idx].get_end_of_line_spacing_rule_list();
    if (end_of_line_spacing_rule_list.empty()) {
      continue;
    }

    // Aggregate rule limits for the current routing layer.
    bool need_cut_shape = false;
    int32_t max_eol_width = 0;
    int32_t max_eol_spacing = 0;
    int32_t max_ete_spacing = 0;
    int32_t max_eol_within = 0;
    for (const EndOfLineSpacingRule& eol_rule : end_of_line_spacing_rule_list) {
      need_cut_shape = need_cut_shape || eol_rule.has_enclose_cut;
      max_eol_width = std::max(max_eol_width, eol_rule.eol_width);
      max_eol_within = std::max(max_eol_within, eol_rule.eol_within);
      max_eol_spacing = std::max(max_eol_spacing, eol_rule.eol_spacing);
      if (eol_rule.has_ete) {
        max_ete_spacing = std::max(max_ete_spacing, eol_rule.ete_spacing);
      }
    }

    RVLayerData merged_layer_data;
    buildLayerComponentData(merged_layer_data, rv_layer_data);
    std::vector<int32_t> eol_boundary_id_list;
    collectEolBoundaries(merged_layer_data, max_eol_width, eol_boundary_id_list);
    if (eol_boundary_id_list.empty()) {
      continue;
    }

    std::vector<uint8_t> is_eol_boundary(merged_layer_data.boundary_pool.size(), 0);
    for (int32_t boundary_id : eol_boundary_id_list) {
      is_eol_boundary[boundary_id] = 1;
    }
    std::vector<int32_t> boundary_min_thickness_cache(merged_layer_data.boundary_pool.size(), -1);

    std::set<Violation, CmpViolation> violation_set;
    // Check each EOL edge with the aggregated layer rule limits.
    for (int32_t eol_boundary_id : eol_boundary_id_list) {
      const auto& eol_boundary = merged_layer_data.getBoundary(eol_boundary_id);
      const auto& pre_boundary = merged_layer_data.getPrevBoundary(eol_boundary_id);
      const auto& post_boundary = merged_layer_data.getNextBoundary(eol_boundary_id);
      PlanarRect eol_edge_rect = DRCUTIL.convertToPlanarRect(eol_boundary.edge);
      // Cache candidate containment checks reused by the rules of this edge.
      std::map<PlanarRect, bool, CmpPlanarRectByXASC> env_containment_cache;
      const bool eol_edge_inside_env = hasRectInside(rv_layer_data, eol_edge_rect);

      Direction direction = DRCUTIL.getDirection(eol_boundary.begin_coord, eol_boundary.end_coord);
      PlanarRect eol_rect = DRCUTIL.getBoundingBox({pre_boundary.begin_coord, pre_boundary.end_coord, post_boundary.begin_coord, post_boundary.end_coord});

      // Build the candidate rectangles checked against this EOL edge.
      std::vector<EolSpacingCandidate> env_checking_candidate_list;

      // enclosed cut rects
      std::vector<CutData> env_cut_list;
      {
        if (need_cut_shape) {
          const std::vector<int32_t>& cut_layer_idx_list = DRCDM.getAdjacentCutLayerIdxList(routing_layer_idx);
          int32_t cut_layer_idx = *std::min_element(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
          auto cut_layer_data_it = layer_data.find(cut_layer_idx);
          if (cut_layer_data_it != layer_data.end()) {
            cut_layer_data_it->second.queryCuts(DRCUTIL.convertToGTLRectInt(eol_rect), std::back_inserter(env_cut_list));
          }
        }

        PlanarRect max_check_rect = eol_edge_rect;
        max_check_rect = DRCUTIL.getEnlargedPartRect(max_check_rect, eol_boundary.orient, std::max(max_eol_spacing, max_ete_spacing));

        if (direction == Direction::kHorizontal) {
          max_check_rect = DRCUTIL.getEnlargedRect(max_check_rect, max_eol_within, 0);
        } else {
          max_check_rect = DRCUTIL.getEnlargedRect(max_check_rect, 0, max_eol_within);
        }
        Orientation oppo_orient = DRCUTIL.getOppositeOrientation(eol_boundary.orient);
        std::vector<std::pair<GTLRectInt, int32_t>> env_rects;
        merged_layer_data.queryBoundaries(DRCUTIL.convertToGTLRectInt(max_check_rect), std::back_inserter(env_rects));
        std::vector<std::pair<GTLRectInt, int32_t>> env_max_rects;
        merged_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(max_check_rect), std::back_inserter(env_max_rects));

        std::vector<EolSpacingCandidate> temp_checking_candidate_list;
        for (const auto& [gtl_rect, idx] : env_max_rects) {
          PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          if (DRCUTIL.isOpenOverlap(rect, max_check_rect)) {
            PlanarRect rect_edge = DRCUTIL.getRect(rect.getOrientEdge(oppo_orient));
            temp_checking_candidate_list.push_back({DRCUTIL.convertToGTLRectInt(rect_edge), -1});
          }
        }

        for (const auto& [gtl_rect, idx] : env_rects) {
          PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          if (DRCUTIL.isOpenOverlap(rect, max_check_rect)) {
            if (eol_boundary.orient == DRCUTIL.getOppositeOrientation(merged_layer_data.getBoundary(idx).orient)) {
              env_checking_candidate_list.push_back({gtl_rect, idx});
            }
          }
        }

        env_checking_candidate_list.reserve(env_checking_candidate_list.size() + temp_checking_candidate_list.size());
        std::vector<PlanarRect> env_checking_rect_list;
        env_checking_rect_list.reserve(env_checking_candidate_list.size() + temp_checking_candidate_list.size());
        for (const EolSpacingCandidate& candidate : env_checking_candidate_list) {
          env_checking_rect_list.emplace_back(DRCUTIL.convertToPlanarRect(candidate.rect));
        }
        for (const EolSpacingCandidate& candidate : temp_checking_candidate_list) {
          PlanarRect temp_rect = DRCUTIL.convertToPlanarRect(candidate.rect);
          bool has_overlap = false;
          for (const auto& env_rect : env_checking_rect_list) {
            if (DRCUTIL.isClosedOverlap(temp_rect, env_rect)) {
              has_overlap = true;
              break;
            }
          }
          if (!has_overlap) {
            env_checking_candidate_list.push_back(candidate);
            env_checking_rect_list.emplace_back(temp_rect);
          }
        }
      }

      if (env_checking_candidate_list.empty()) {
        continue;
      }
      int32_t eol_net_idx = queryNetIdxByRect(rv_layer_data, eol_edge_rect);

      for (const EndOfLineSpacingRule& eol_rule : end_of_line_spacing_rule_list) {
        if (eol_boundary.edge_length >= eol_rule.eol_width) {
          continue;
        }
        if (eol_rule.has_enclose_cut && env_cut_list.empty()) {
          continue;
        }
        bool pre_length_ok = pre_boundary.edge_length >= eol_rule.min_length;
        bool post_length_ok = post_boundary.edge_length >= eol_rule.min_length;
        if (eol_rule.has_par && !eol_rule.has_same_metal) {
          if (eol_rule.has_two_edges) {
            if (!pre_length_ok || !post_length_ok) {
              continue;
            }
          } else if (!pre_length_ok && !post_length_ok) {
            continue;
          }
        }
        if (eol_rule.has_same_metal && eol_net_idx == -1) {
          continue;
        }

        for (const EolSpacingCandidate& candidate : env_checking_candidate_list) {
          PlanarRect env_rect = DRCUTIL.convertToPlanarRect(candidate.rect);
          if (DRCUTIL.isClosedOverlap(env_rect, eol_rect)) {
            continue;
          }

          bool is_ete = false;
          if (candidate.boundary_id >= 0 && candidate.boundary_id < static_cast<int32_t>(is_eol_boundary.size())
              && is_eol_boundary[candidate.boundary_id]) {
            const BoundaryData& env_boundary = merged_layer_data.getBoundary(candidate.boundary_id);
            if (env_boundary.edge_length < eol_rule.eol_width && eol_rule.has_ete) {
              is_ete = true;
            }
          }
          PlanarRect check_rect = DRCUTIL.getEnlargedPartRect(eol_edge_rect, eol_boundary.orient, is_ete ? eol_rule.ete_spacing : eol_rule.eol_spacing);
          if (direction == Direction::kHorizontal) {
            check_rect = DRCUTIL.getEnlargedRect(check_rect, eol_rule.eol_within, 0);
          } else {
            check_rect = DRCUTIL.getEnlargedRect(check_rect, 0, eol_rule.eol_within);
          }
          if (!DRCUTIL.isOpenOverlap(check_rect, env_rect) && !eol_rule.has_same_metal) {
            continue;
          }

          bool pre_par = false, post_par = false;
          int32_t pre_par_idx = -1, post_par_idx = -1;
          if (eol_rule.has_par) {
            int32_t par_spacing = 0;
            if (eol_rule.has_subtrace_eol_width) {
              int32_t width = std::min(getBoundaryMinThickness(merged_layer_data, boundary_min_thickness_cache, eol_boundary.prev_boundary_id),
                                       getBoundaryMinThickness(merged_layer_data, boundary_min_thickness_cache, eol_boundary.next_boundary_id));
              par_spacing = std::max(par_spacing, eol_rule.par_spacing - width);
            } else {
              par_spacing = std::max(par_spacing, eol_rule.par_spacing);
            }

            PlanarRect pre_rect = DRCUTIL.getRect({pre_boundary.end_coord, pre_boundary.end_coord});
            PlanarRect post_rect = DRCUTIL.getRect({post_boundary.begin_coord, post_boundary.begin_coord});

            switch (eol_boundary.orient) {
              case Orientation::kNorth:
                pre_rect = DRCUTIL.getEnlargedRect(pre_rect, 0, eol_rule.par_within, par_spacing, eol_rule.eol_within);
                post_rect = DRCUTIL.getEnlargedRect(post_rect, par_spacing, eol_rule.par_within, 0, eol_rule.eol_within);
                break;
              case Orientation::kSouth:
                pre_rect = DRCUTIL.getEnlargedRect(pre_rect, par_spacing, eol_rule.eol_within, 0, eol_rule.par_within);
                post_rect = DRCUTIL.getEnlargedRect(post_rect, 0, eol_rule.eol_within, par_spacing, eol_rule.par_within);
                break;
              case Orientation::kWest:
                pre_rect = DRCUTIL.getEnlargedRect(pre_rect, eol_rule.eol_within, 0, eol_rule.par_within, par_spacing);
                post_rect = DRCUTIL.getEnlargedRect(post_rect, eol_rule.eol_within, par_spacing, eol_rule.par_within, 0);
                break;
              case Orientation::kEast:
                pre_rect = DRCUTIL.getEnlargedRect(pre_rect, eol_rule.par_within, par_spacing, eol_rule.eol_within, 0);
                post_rect = DRCUTIL.getEnlargedRect(post_rect, eol_rule.par_within, 0, eol_rule.eol_within, par_spacing);
                break;
              default:
                DRCLOG.error(Loc::current(), "The orientation is error!");
            }

            // par left and right neighbors
            std::vector<std::pair<GTLRectInt, int32_t>> env_routing_poly_list;
            merged_layer_data.queryBoundaries(DRCUTIL.convertToGTLRectInt(pre_rect), std::back_inserter(env_routing_poly_list));
            merged_layer_data.queryBoundaries(DRCUTIL.convertToGTLRectInt(post_rect), std::back_inserter(env_routing_poly_list));

            for (auto& [par_gtl_rect, par_id] : env_routing_poly_list) {
              PlanarRect par_rect = DRCUTIL.convertToPlanarRect(par_gtl_rect);
              if (DRCUTIL.isClosedOverlap(par_rect, eol_rect)) {
                continue;
              }
              const BoundaryData& par_boundary = merged_layer_data.getBoundary(par_id);
              if (DRCUTIL.isOpenOverlap(pre_rect, par_rect)) {
                if (pre_boundary.orient == DRCUTIL.getOppositeOrientation(par_boundary.orient)) {
                  pre_par_idx = par_id;
                  pre_par = true;
                }
              }
              if (DRCUTIL.isOpenOverlap(post_rect, par_rect)) {
                if (post_boundary.orient == DRCUTIL.getOppositeOrientation(par_boundary.orient)) {
                  post_par_idx = par_id;
                  post_par = true;
                }
              }
            }
            if (!eol_rule.has_same_metal) {
              pre_par = pre_length_ok ? pre_par : false;
              post_par = post_length_ok ? post_par : false;
            }
            if (eol_rule.has_two_edges) {
              if (!(pre_par && post_par)) {
                continue;
              }
            } else {
              if (!(pre_par || post_par)) {
                continue;
              }
            }
            if (eol_rule.has_same_metal) {
              if (pre_par_idx == -1 || post_par_idx == -1) {
                continue;
              }
              auto& pre_polygon = merged_layer_data.getPolygon(merged_layer_data.getBoundary(pre_par_idx).polygon_id);
              auto& post_polygon = merged_layer_data.getPolygon(merged_layer_data.getBoundary(post_par_idx).polygon_id);

              if (pre_polygon.net_id != post_polygon.net_id) {
                continue;
              }
              GTLRectInt pre_edge_rect = merged_layer_data.getBoundary(pre_par_idx).edge;
              GTLRectInt post_edge_rect = merged_layer_data.getBoundary(post_par_idx).edge;
              int32_t pre_prl_length = DRCUTIL.getParallelLength(pre_rect, DRCUTIL.convertToPlanarRect(pre_edge_rect));
              int32_t post_prl_length = DRCUTIL.getParallelLength(post_rect, DRCUTIL.convertToPlanarRect(post_edge_rect));
              if (pre_prl_length >= eol_rule.par_within || post_prl_length >= eol_rule.par_within || pre_prl_length <= 0 || post_prl_length <= 0) {
                continue;
              }
            }
          }

          if (eol_rule.has_enclose_cut) {
            bool is_pass_cut = false;
            for (const CutData& cut_data : env_cut_list) {
              PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_data.rect);
              if ((DRCUTIL.getEuclideanDistance(cut_rect, eol_edge_rect) < eol_rule.enclosed_dist)
                  && (DRCUTIL.getEuclideanDistance(cut_rect, env_rect)) < eol_rule.cut_to_metal_spacing) {
                is_pass_cut = true;
              }
            }
            if (!is_pass_cut) {
              continue;
            }
          }

          if (eol_edge_inside_env && hasRectInsideCached(rv_layer_data, env_rect, env_containment_cache)) {
            continue;
          }
          PlanarRect spacing_rect = DRCUTIL.getSpacingRect(eol_rect, env_rect);
          int32_t req_size = is_ete ? eol_rule.ete_spacing : eol_rule.eol_spacing;
          if (DRCUTIL.getEuclideanDistance(eol_edge_rect, env_rect) >= req_size) {
            continue;
          }
          std::set<int32_t> net_list{eol_net_idx, queryNetIdxByRect(rv_layer_data, env_rect)};

          Violation violation;
          violation.set_violation_type(ViolationType::kEndOfLineSpacing);
          violation.set_required_size(req_size);
          violation.set_is_routing(true);
          violation.set_violation_net_set(net_list);
          violation.set_layer_idx(routing_layer_idx);
          violation.set_rect(spacing_rect);
          violation_set.insert(violation);
        }
      }
    }

    // Keep the largest required size for the same rectangle.
    for (const Violation& violation : violation_set) {
      bool is_redundant = false;
      for (const Violation& other_violation : violation_set) {
        if (violation.get_rect() == other_violation.get_rect()) {
          is_redundant = violation.get_required_size() < other_violation.get_required_size();
        } else {
          is_redundant = DRCUTIL.isInside(other_violation.get_rect(), violation.get_rect());
        }
        if (is_redundant) {
          break;
        }
      }

      if (!is_redundant) {
        rv_cluster.get_violation_list().push_back(violation);
      }
    }
  }
}

namespace {

Orientation getBoundaryOrient(Rotation rotation, bool is_hole, const PlanarCoord& begin_coord, const PlanarCoord& end_coord)
{
  Orientation travel_orient = DRCUTIL.getOrientation(begin_coord, end_coord);
  bool metal_on_left = (rotation == Rotation::kCounterclockwise);
  if (is_hole) {
    metal_on_left = !metal_on_left;
  }
  return metal_on_left ? DRCUTIL.getCWOrientation(travel_orient) : DRCUTIL.getCCWOrientation(travel_orient);
}

int32_t queryNetIdxByRect(const RVLayerData& rv_layer_data, const PlanarRect& query_rect)
{
  std::vector<std::pair<GTLRectInt, int32_t>> rect_max_rect_pair_list;
  rv_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(query_rect), std::back_inserter(rect_max_rect_pair_list));
  return rect_max_rect_pair_list.empty() ? -1 : rv_layer_data.getNetIdxByMaxRectId(rect_max_rect_pair_list.front().second);
}

int32_t calcBoundaryMinThickness(const RVLayerData& rv_layer_data, int32_t boundary_id)
{
  const BoundaryData& boundary = rv_layer_data.getBoundary(boundary_id);
  Segment<PlanarCoord> boundary_seg(boundary.begin_coord, boundary.end_coord);
  Direction boundary_dir = DRCUTIL.getDirection(boundary.begin_coord, boundary.end_coord);

  std::vector<std::pair<GTLRectInt, int32_t>> rect_hits;
  rv_layer_data.queryMaxRects(boundary.edge, std::back_inserter(rect_hits));

  int32_t min_thickness = INT32_MAX;
  for (const auto& [gtl_rect, max_rect_id] : rect_hits) {
    const MaxRectData& max_rect_data = rv_layer_data.getMaxRect(max_rect_id);
    if (max_rect_data.polygon_id != boundary.polygon_id) {
      continue;
    }

    PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);
    if (DRCUTIL.getTouchedEdgeOrient(rect, boundary_seg) != boundary.orient) {
      continue;
    }

    int32_t thickness = (boundary_dir == Direction::kHorizontal) ? rect.getYSpan() : rect.getXSpan();
    min_thickness = std::min(min_thickness, thickness);
  }
  return min_thickness == INT32_MAX ? 0 : min_thickness;
}

int32_t getBoundaryMinThickness(const RVLayerData& rv_layer_data, std::vector<int32_t>& boundary_min_thickness_cache, int32_t boundary_id)
{
  if (boundary_id < 0 || boundary_id >= static_cast<int32_t>(boundary_min_thickness_cache.size())) {
    return 0;
  }

  int32_t& cached_thickness = boundary_min_thickness_cache[boundary_id];
  if (cached_thickness == -1) {
    cached_thickness = calcBoundaryMinThickness(rv_layer_data, boundary_id);
  }
  return cached_thickness;
}

void appendBoundaryEdges(RVLayerData& rv_layer_data, GTLHolePolyInt& check_hole_poly, bool is_hole, int32_t polygon_id,
                         std::vector<std::pair<GTLRectInt, int32_t>>& boundary_rtree_inputs)
{
  int32_t coord_size = static_cast<int32_t>(check_hole_poly.size());
  std::vector<PlanarCoord> coord_list;
  coord_list.reserve(coord_size);
  for (auto iter = check_hole_poly.begin(); iter != check_hole_poly.end(); ++iter) {
    coord_list.push_back(DRCUTIL.convertToPlanarCoord(*iter));
  }
  while (coord_list.size() > 1 && coord_list.front() == coord_list.back()) {
    coord_list.pop_back();
  }
  coord_size = static_cast<int32_t>(coord_list.size());
  if (coord_size < 2) {
    return;
  }

  Rotation rotation = DRCUTIL.getRotation(check_hole_poly);
  std::vector<bool> convex_corner_list(coord_size, false);
  if (coord_size >= 3) {
    for (int32_t i = 0; i < coord_size; i++) {
      PlanarCoord& pre_coord = coord_list[DRCUTIL.getRingIdx(i - 1, coord_size)];
      PlanarCoord& curr_coord = coord_list[i];
      PlanarCoord& post_coord = coord_list[DRCUTIL.getRingIdx(i + 1, coord_size)];
      convex_corner_list[i] = is_hole ? DRCUTIL.isConcaveCorner(rotation, pre_coord, curr_coord, post_coord)
                                      : DRCUTIL.isConvexCorner(rotation, pre_coord, curr_coord, post_coord);
    }
  }

  std::vector<int32_t> ring_boundary_id_list;
  ring_boundary_id_list.reserve(coord_size);
  for (int32_t i = 0; i < coord_size; i++) {
    PlanarCoord& pre_coord = coord_list[DRCUTIL.getRingIdx(i - 1, coord_size)];
    PlanarCoord& curr_coord = coord_list[i];
    if (pre_coord == curr_coord) {
      continue;
    }

    BoundaryData boundary_data;
    boundary_data.edge = DRCUTIL.convertToGTLRectInt(DRCUTIL.getRect(pre_coord, curr_coord));
    boundary_data.begin_coord = pre_coord;
    boundary_data.end_coord = curr_coord;
    boundary_data.orient = getBoundaryOrient(rotation, is_hole, pre_coord, curr_coord);
    boundary_data.polygon_id = polygon_id;
    boundary_data.edge_length = DRCUTIL.getManhattanDistance(pre_coord, curr_coord);
    boundary_data.isConvex = convex_corner_list[i];
    boundary_data.isHole = is_hole;

    rv_layer_data.boundary_pool.push_back(boundary_data);
    int32_t boundary_id = static_cast<int32_t>(rv_layer_data.boundary_pool.size()) - 1;
    ring_boundary_id_list.push_back(boundary_id);
    boundary_rtree_inputs.push_back({boundary_data.edge, boundary_id});
  }

  int32_t ring_size = static_cast<int32_t>(ring_boundary_id_list.size());
  if (ring_size < 2) {
    return;
  }
  for (int32_t i = 0; i < ring_size; i++) {
    BoundaryData& boundary_data = rv_layer_data.boundary_pool[ring_boundary_id_list[i]];
    boundary_data.prev_boundary_id = ring_boundary_id_list[DRCUTIL.getRingIdx(i - 1, ring_size)];
    boundary_data.next_boundary_id = ring_boundary_id_list[DRCUTIL.getRingIdx(i + 1, ring_size)];
  }
}

void appendPreparedPolygon(const RVLayerData& source_layer_data, int32_t source_polygon_id, int32_t output_net_id,
                           RVLayerData& merged_layer_data, std::vector<std::pair<GTLRectInt, int32_t>>& rect_rtree_inputs,
                           std::vector<std::pair<GTLRectInt, int32_t>>& boundary_rtree_inputs)
{
  // An isolated prepared polygon is unchanged by the layer-wide union. Copy
  // its materialized pools and only remap IDs into the EOL layer data.
  const PolygonData& source_polygon = source_layer_data.getPolygon(source_polygon_id);
  RVRoutingNet& routing_net = merged_layer_data.nets[output_net_id];
  routing_net.polygon_begin = static_cast<int32_t>(merged_layer_data.polygon_pool.size());
  routing_net.max_rect_begin = static_cast<int32_t>(merged_layer_data.max_rect_pool.size());
  routing_net.boundary_begin = static_cast<int32_t>(merged_layer_data.boundary_pool.size());

  int32_t polygon_id = static_cast<int32_t>(merged_layer_data.polygon_pool.size());
  PolygonData polygon_data = source_polygon;
  polygon_data.net_id = output_net_id;
  polygon_data.max_rect_begin = static_cast<int32_t>(merged_layer_data.max_rect_pool.size());
  polygon_data.boundary_begin = static_cast<int32_t>(merged_layer_data.boundary_pool.size());
  merged_layer_data.polygon_pool.push_back(std::move(polygon_data));

  for (const MaxRectData& source_max_rect : source_layer_data.getMaxRects(source_polygon)) {
    MaxRectData max_rect_data = source_max_rect;
    max_rect_data.polygon_id = polygon_id;
    merged_layer_data.max_rect_pool.push_back(max_rect_data);
    rect_rtree_inputs.push_back({max_rect_data.rect, static_cast<int32_t>(merged_layer_data.max_rect_pool.size()) - 1});
  }

  int32_t source_boundary_begin = source_polygon.boundary_begin;
  int32_t source_boundary_end = source_boundary_begin + source_polygon.boundary_count;
  int32_t output_boundary_begin = static_cast<int32_t>(merged_layer_data.boundary_pool.size());
  for (const BoundaryData& source_boundary : source_layer_data.getBoundaries(source_polygon)) {
    BoundaryData boundary_data = source_boundary;
    boundary_data.polygon_id = polygon_id;
    boundary_data.prev_boundary_id = -1;
    boundary_data.next_boundary_id = -1;
    if (source_boundary.prev_boundary_id >= source_boundary_begin && source_boundary.prev_boundary_id < source_boundary_end) {
      boundary_data.prev_boundary_id = output_boundary_begin + source_boundary.prev_boundary_id - source_boundary_begin;
    }
    if (source_boundary.next_boundary_id >= source_boundary_begin && source_boundary.next_boundary_id < source_boundary_end) {
      boundary_data.next_boundary_id = output_boundary_begin + source_boundary.next_boundary_id - source_boundary_begin;
    }
    merged_layer_data.boundary_pool.push_back(boundary_data);
    boundary_rtree_inputs.push_back({boundary_data.edge, static_cast<int32_t>(merged_layer_data.boundary_pool.size()) - 1});
  }

  PolygonData& output_polygon = merged_layer_data.polygon_pool[polygon_id];
  output_polygon.max_rect_count = static_cast<int32_t>(merged_layer_data.max_rect_pool.size()) - output_polygon.max_rect_begin;
  output_polygon.boundary_count = static_cast<int32_t>(merged_layer_data.boundary_pool.size()) - output_polygon.boundary_begin;
  routing_net.polygon_count = 1;
  routing_net.max_rect_count = output_polygon.max_rect_count;
  routing_net.boundary_count = output_polygon.boundary_count;
}

void appendMergedPolyset(RVLayerData& merged_layer_data, const GTLPolySetInt& merged_layer_polyset, int32_t& next_net_id,
                         std::vector<std::pair<GTLRectInt, int32_t>>& rect_rtree_inputs,
                         std::vector<std::pair<GTLRectInt, int32_t>>& boundary_rtree_inputs)
{
  // This path is used only for components containing touching polygons.
  std::vector<GTLHolePolyInt> gtl_hole_poly_list;
  merged_layer_polyset.get(gtl_hole_poly_list);

  for (GTLHolePolyInt& gtl_hole_poly : gtl_hole_poly_list) {
    int32_t net_id = next_net_id++;
    RVRoutingNet& routing_net = merged_layer_data.nets[net_id];
    routing_net.polygon_begin = static_cast<int32_t>(merged_layer_data.polygon_pool.size());
    routing_net.max_rect_begin = static_cast<int32_t>(merged_layer_data.max_rect_pool.size());
    routing_net.boundary_begin = static_cast<int32_t>(merged_layer_data.boundary_pool.size());

    int32_t polygon_id = static_cast<int32_t>(merged_layer_data.polygon_pool.size());
    merged_layer_data.polygon_pool.push_back(
        {net_id, static_cast<int32_t>(merged_layer_data.max_rect_pool.size()), 0, static_cast<int32_t>(merged_layer_data.boundary_pool.size()), 0});
    PolygonData& polygon_data = merged_layer_data.polygon_pool.back();
    polygon_data.hole_poly = std::move(gtl_hole_poly);
    GTLHolePolyInt& polygon_hole_poly = polygon_data.hole_poly;

    std::vector<GTLRectInt> gtl_rect_list;
    gtl::get_max_rectangles(gtl_rect_list, polygon_hole_poly);
    for (const GTLRectInt& gtl_rect : gtl_rect_list) {
      MaxRectData max_rect_data;
      max_rect_data.rect = gtl_rect;
      max_rect_data.polygon_id = polygon_id;
      merged_layer_data.max_rect_pool.push_back(max_rect_data);
      rect_rtree_inputs.push_back({gtl_rect, static_cast<int32_t>(merged_layer_data.max_rect_pool.size()) - 1});
    }
    polygon_data.max_rect_count = static_cast<int32_t>(merged_layer_data.max_rect_pool.size()) - polygon_data.max_rect_begin;

    appendBoundaryEdges(merged_layer_data, polygon_hole_poly, false, polygon_id, boundary_rtree_inputs);
    for (auto iter = polygon_hole_poly.begin_holes(); iter != polygon_hole_poly.end_holes(); ++iter) {
      GTLPolyInt gtl_poly = *iter;
      GTLHolePolyInt hole_poly;
      hole_poly.set(gtl_poly.begin(), gtl_poly.end());
      appendBoundaryEdges(merged_layer_data, hole_poly, true, polygon_id, boundary_rtree_inputs);
    }
    polygon_data.boundary_count = static_cast<int32_t>(merged_layer_data.boundary_pool.size()) - polygon_data.boundary_begin;
    routing_net.polygon_count = 1;
    routing_net.max_rect_count = polygon_data.max_rect_count;
    routing_net.boundary_count = polygon_data.boundary_count;
  }
}

void buildLayerComponentData(RVLayerData& merged_layer_data, const RVLayerData& source_layer_data)
{
  merged_layer_data.nets.clear();
  merged_layer_data.polygon_pool.clear();
  merged_layer_data.max_rect_pool.clear();
  merged_layer_data.boundary_pool.clear();
  merged_layer_data.polygon_pool.reserve(source_layer_data.polygon_pool.size());
  merged_layer_data.max_rect_pool.reserve(source_layer_data.max_rect_pool.size());
  merged_layer_data.boundary_pool.reserve(source_layer_data.boundary_pool.size());

  std::vector<std::pair<GTLRectInt, int32_t>> rect_rtree_inputs;
  std::vector<std::pair<GTLRectInt, int32_t>> boundary_rtree_inputs;

  using PolygonRTree = bgi::rtree<std::pair<GTLRectInt, int32_t>, bgi::quadratic<16>>;
  std::vector<std::pair<GTLRectInt, int32_t>> polygon_rtree_inputs;
  polygon_rtree_inputs.reserve(source_layer_data.polygon_pool.size());
  std::vector<GTLRectInt> polygon_bounding_boxes(source_layer_data.polygon_pool.size());
  std::vector<uint8_t> has_polygon_bounding_box(source_layer_data.polygon_pool.size(), 0);
  for (int32_t polygon_id = 0; polygon_id < static_cast<int32_t>(source_layer_data.polygon_pool.size()); ++polygon_id) {
    if (getPolygonBoundingBox(source_layer_data, source_layer_data.getPolygon(polygon_id), polygon_bounding_boxes[polygon_id])) {
      has_polygon_bounding_box[polygon_id] = 1;
      polygon_rtree_inputs.emplace_back(polygon_bounding_boxes[polygon_id], polygon_id);
    }
  }
  PolygonRTree polygon_rtree(polygon_rtree_inputs);

  // Use one bbox query per polygon to find candidates. The exact check below
  // retains closed-overlap semantics, including edge-to-edge contact.
  boost::disjoint_sets_with_storage<> polygon_components(source_layer_data.polygon_pool.size());
  std::vector<std::pair<GTLRectInt, int32_t>> polygon_overlap_list;
  for (int32_t polygon_id = 0; polygon_id < static_cast<int32_t>(source_layer_data.polygon_pool.size()); ++polygon_id) {
    if (!has_polygon_bounding_box[polygon_id]) {
      continue;
    }
    polygon_overlap_list.clear();
    polygon_rtree.query(bgi::intersects(polygon_bounding_boxes[polygon_id]), std::back_inserter(polygon_overlap_list));
    for (const auto& [overlap_bbox, overlap_polygon_id] : polygon_overlap_list) {
      (void) overlap_bbox;
      if (overlap_polygon_id <= polygon_id) {
        continue;
      }
      if (polygonsHaveClosedOverlap(source_layer_data, polygon_id, overlap_polygon_id)) {
        polygon_components.union_set(polygon_id, overlap_polygon_id);
      }
    }
  }

  std::map<int32_t, std::vector<int32_t>> component_polygon_map;
  for (int32_t polygon_id = 0; polygon_id < static_cast<int32_t>(source_layer_data.polygon_pool.size()); ++polygon_id) {
    component_polygon_map[polygon_components.find_set(polygon_id)].push_back(polygon_id);
  }

  int32_t next_net_id = 0;
  for (const auto& component_entry : component_polygon_map) {
    const std::vector<int32_t>& polygon_id_list = component_entry.second;
    if (polygon_id_list.size() == 1) {
      appendPreparedPolygon(source_layer_data, polygon_id_list.front(), next_net_id++, merged_layer_data, rect_rtree_inputs,
                            boundary_rtree_inputs);
      continue;
    }

    GTLPolySetInt component_polyset;
    for (int32_t polygon_id : polygon_id_list) {
      component_polyset += source_layer_data.getPolygon(polygon_id).hole_poly;
    }
    appendMergedPolyset(merged_layer_data, component_polyset, next_net_id, rect_rtree_inputs, boundary_rtree_inputs);
  }

  merged_layer_data.rect_rtrees = decltype(merged_layer_data.rect_rtrees)(rect_rtree_inputs);
  merged_layer_data.boundary_rtrees = decltype(merged_layer_data.boundary_rtrees)(boundary_rtree_inputs);
}

bool getPolygonBoundingBox(const RVLayerData& rv_layer_data, const PolygonData& polygon_data, GTLRectInt& bounding_box)
{
  const std::span<const MaxRectData> max_rects = rv_layer_data.getMaxRects(polygon_data);
  if (max_rects.empty()) {
    return false;
  }

  int32_t x_min = gtl::xl(max_rects.front().rect);
  int32_t y_min = gtl::yl(max_rects.front().rect);
  int32_t x_max = gtl::xh(max_rects.front().rect);
  int32_t y_max = gtl::yh(max_rects.front().rect);
  for (const MaxRectData& max_rect : max_rects.subspan(1)) {
    x_min = std::min(x_min, gtl::xl(max_rect.rect));
    y_min = std::min(y_min, gtl::yl(max_rect.rect));
    x_max = std::max(x_max, gtl::xh(max_rect.rect));
    y_max = std::max(y_max, gtl::yh(max_rect.rect));
  }
  bounding_box = GTLRectInt(x_min, y_min, x_max, y_max);
  return true;
}

bool polygonsHaveClosedOverlap(const RVLayerData& rv_layer_data, int32_t first_polygon_id, int32_t second_polygon_id)
{
  const PolygonData& first_polygon = rv_layer_data.getPolygon(first_polygon_id);
  const PolygonData& second_polygon = rv_layer_data.getPolygon(second_polygon_id);
  for (const MaxRectData& first_max_rect : rv_layer_data.getMaxRects(first_polygon)) {
    const PlanarRect first_rect = DRCUTIL.convertToPlanarRect(first_max_rect.rect);
    for (const MaxRectData& second_max_rect : rv_layer_data.getMaxRects(second_polygon)) {
      if (DRCUTIL.isClosedOverlap(first_rect, DRCUTIL.convertToPlanarRect(second_max_rect.rect))) {
        return true;
      }
    }
  }
  return false;
}

bool isEolBoundary(const RVLayerData& rv_layer_data, int32_t boundary_id)
{
  const BoundaryData& curr_boundary = rv_layer_data.getBoundary(boundary_id);
  const BoundaryData& prev_boundary = rv_layer_data.getPrevBoundary(boundary_id);
  if (!prev_boundary.isConvex || !curr_boundary.isConvex) {
    return false;
  }

  Direction direction = DRCUTIL.getDirection(curr_boundary.begin_coord, curr_boundary.end_coord);
  PlanarRect start_probe;
  PlanarRect end_probe;
  if (direction == Direction::kHorizontal) {
    int32_t x_min = std::min(curr_boundary.begin_coord.get_x(), curr_boundary.end_coord.get_x());
    int32_t x_max = std::max(curr_boundary.begin_coord.get_x(), curr_boundary.end_coord.get_x());
    int32_t y = curr_boundary.begin_coord.get_y();
    start_probe = PlanarRect(x_min - 2, y, x_min - 1, y);
    end_probe = PlanarRect(x_max + 1, y, x_max + 2, y);
  } else {
    int32_t y_min = std::min(curr_boundary.begin_coord.get_y(), curr_boundary.end_coord.get_y());
    int32_t y_max = std::max(curr_boundary.begin_coord.get_y(), curr_boundary.end_coord.get_y());
    int32_t x = curr_boundary.begin_coord.get_x();
    start_probe = PlanarRect(x, y_min - 2, x, y_min - 1);
    end_probe = PlanarRect(x, y_max + 1, x, y_max + 2);
  }

  std::vector<std::pair<GTLRectInt, int32_t>> overlap_list;
  rv_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(start_probe), std::back_inserter(overlap_list));
  if (!overlap_list.empty()) {
    return false;
  }
  rv_layer_data.queryMaxRects(DRCUTIL.convertToGTLRectInt(end_probe), std::back_inserter(overlap_list));
  return overlap_list.empty();
}

void collectEolBoundaries(const RVLayerData& merged_layer_data, int32_t max_eol_width,
                          std::vector<int32_t>& eol_boundary_id_list)
{
  for (const auto& net_entry : merged_layer_data.nets) {
    const RVRoutingNet& routing_net = net_entry.second;
    for (const PolygonData& polygon_data : merged_layer_data.getPolygons(routing_net)) {
      std::span<const BoundaryData> polygon_boundaries = merged_layer_data.getBoundaries(polygon_data);
      if (polygon_boundaries.empty()) {
        continue;
      }

      std::vector<bool> visited_ring_boundary(static_cast<size_t>(polygon_data.boundary_count), false);
      for (const BoundaryData& seed_boundary : polygon_boundaries) {
        int32_t seed_boundary_id = merged_layer_data.getBoundaryId(seed_boundary);
        int32_t seed_local_idx = seed_boundary_id - polygon_data.boundary_begin;
        if (visited_ring_boundary[seed_local_idx]) {
          continue;
        }

        int32_t curr_boundary_id = seed_boundary_id;
        do {
          int32_t local_idx = curr_boundary_id - polygon_data.boundary_begin;
          if (local_idx < 0 || polygon_data.boundary_count <= local_idx || visited_ring_boundary[local_idx]) {
            break;
          }
          visited_ring_boundary[local_idx] = true;

          const BoundaryData& curr_boundary = merged_layer_data.getBoundary(curr_boundary_id);
          if (curr_boundary.edge_length < max_eol_width && isEolBoundary(merged_layer_data, curr_boundary_id)) {
            eol_boundary_id_list.push_back(curr_boundary_id);
          }
          curr_boundary_id = curr_boundary.next_boundary_id;
        } while (curr_boundary_id != seed_boundary_id);
      }
    }
  }
}

bool hasRectInside(const RVLayerData& rv_layer_data, const PlanarRect& target_rect)
{
  GTLRectInt query_box = DRCUTIL.convertToGTLRectInt(target_rect);
  std::vector<std::pair<GTLRectInt, int32_t>> env_rect_list;
  // Keep the query lazy so a direct hit returns without allocating a result list.
  for (auto iter = rv_layer_data.env_rect_rtree.qbegin(bgi::intersects(query_box)); iter != rv_layer_data.env_rect_rtree.qend(); ++iter) {
    const auto& [gtl_rect, net_idx] = *iter;
    (void) net_idx;
    if (DRCUTIL.isInside(DRCUTIL.convertToPlanarRect(gtl_rect), target_rect)) {
      return true;
    }
    env_rect_list.push_back(*iter);
  }

  if (env_rect_list.size() < 2) {
    return false;
  }

  // The prepared index is per net. Merge only rectangles touching this target
  // to recover the layer-wide environment semantics without building a global union.
  std::vector<GTLRectInt> local_rect_list;
  local_rect_list.reserve(env_rect_list.size());
  for (const auto& [gtl_rect, net_idx] : env_rect_list) {
    (void) net_idx;
    local_rect_list.push_back(gtl_rect);
  }

  GTLPolySetInt local_env_polyset;
  local_env_polyset.insert(local_rect_list.begin(), local_rect_list.end());
  std::vector<GTLRectInt> local_max_rect_list;
  gtl::get_max_rectangles(local_max_rect_list, local_env_polyset);
  for (const GTLRectInt& local_max_rect : local_max_rect_list) {
    if (DRCUTIL.isInside(DRCUTIL.convertToPlanarRect(local_max_rect), target_rect)) {
      return true;
    }
  }
  return false;
}

bool hasRectInsideCached(const RVLayerData& rv_layer_data, const PlanarRect& target_rect,
                         std::map<PlanarRect, bool, CmpPlanarRectByXASC>& contains_cache)
{
  auto [iter, inserted] = contains_cache.try_emplace(target_rect, false);
  if (inserted) {
    iter->second = hasRectInside(rv_layer_data, target_rect);
  }
  return iter->second;
}

}  // namespace

}  // namespace idrc
