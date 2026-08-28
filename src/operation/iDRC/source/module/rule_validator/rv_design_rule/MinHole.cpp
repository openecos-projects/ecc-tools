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

namespace idrc {

namespace {

void addHoleViolation(RVCluster& rv_cluster, const GTLPolyInt& gtl_poly, int32_t net_idx,
                      int32_t min_hole_area, int32_t routing_layer_idx);

}  // namespace

void RuleValidator::verifyMinHole(RVCluster& rv_cluster)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  const auto& layer_data = rv_cluster.get_layer_data();

  for (const auto& [routing_layer_idx, rv_layer_data] : layer_data) {
    const int32_t min_hole_area = routing_layer_list[routing_layer_idx].get_min_hole_rule().min_hole_area;
    for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
      if (net_idx == -1) {
        continue;
      }

      // Reuse the polygons materialized during RV cluster preparation.
      for (const PolygonData& polygon_data : rv_layer_data.getPolygons(routing_net)) {
        const GTLHolePolyInt& gtl_hole_poly = polygon_data.hole_poly;
        if (gtl_hole_poly.begin_holes() == gtl_hole_poly.end_holes()) {
          continue;
        }

        bool need_repair = false;
        std::set<PlanarCoord, CmpPlanarCoordByXASC> outer_vertex_set;
        for (auto iter = gtl_hole_poly.begin(); iter != gtl_hole_poly.end(); iter++) {
          outer_vertex_set.insert(DRCUTIL.convertToPlanarCoord(*iter));
        }
        for (auto hole_iter = gtl_hole_poly.begin_holes(); hole_iter != gtl_hole_poly.end_holes() && !need_repair; hole_iter++) {
          const GTLPolyInt& gtl_poly = *hole_iter;
          for (auto iter = gtl_poly.begin(); iter != gtl_poly.end(); iter++) {
            if (DRCUTIL.exist(outer_vertex_set, DRCUTIL.convertToPlanarCoord(*iter))) {
              need_repair = true;
              break;
            }
          }
        }

        if (!need_repair) {
          for (auto iter = gtl_hole_poly.begin_holes(); iter != gtl_hole_poly.end_holes(); iter++) {
            addHoleViolation(rv_cluster, *iter, net_idx, min_hole_area, routing_layer_idx);
          }
          continue;
        }

        // Repair only the pathological polygons whose inner-ring vertices touch the outer ring.
        GTLPolySetInt new_poly;
        new_poly += gtl_hole_poly;
        for (const PlanarCoord& vertex : outer_vertex_set) {
          new_poly += DRCUTIL.convertToGTLRectInt(DRCUTIL.getEnlargedRect(DRCUTIL.getRect(vertex, vertex), 1));
        }

        std::vector<GTLHolePolyInt> repaired_hole_poly_list;
        new_poly.get(repaired_hole_poly_list);
        for (GTLHolePolyInt& repaired_hole_poly : repaired_hole_poly_list) {
          for (auto iter = repaired_hole_poly.begin_holes(); iter != repaired_hole_poly.end_holes(); iter++) {
            addHoleViolation(rv_cluster, *iter, net_idx, min_hole_area, routing_layer_idx);
          }
        }
      }
    }
  }
}

namespace {

void addHoleViolation(RVCluster& rv_cluster, const GTLPolyInt& gtl_poly, int32_t net_idx,
                      int32_t min_hole_area, int32_t routing_layer_idx)
{
  if (gtl::area(gtl_poly) >= min_hole_area) {
    return;
  }

  GTLRectInt gtl_rect;
  gtl::extents(gtl_rect, gtl_poly);
  Violation violation;
  violation.set_violation_type(ViolationType::kMinHole);
  violation.set_is_routing(true);
  violation.set_violation_net_set({net_idx});
  violation.set_required_size(min_hole_area);
  violation.set_layer_idx(routing_layer_idx);
  violation.set_rect(DRCUTIL.convertToPlanarRect(gtl_rect));
  rv_cluster.get_violation_list().push_back(std::move(violation));
}

}  // namespace

}  // namespace idrc
