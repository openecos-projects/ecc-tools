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

void RuleValidator::verifyMetalShort(RVCluster& rv_cluster)
{
  const auto& layer_data = rv_cluster.get_layer_data();
  // Per (layer, net) max rectangles of result-only merged geometry. Nets without env
  // shapes in this cluster have identical combined and result-only geometry, so the max
  // rectangles already materialized by prepare are reused; other nets rebuild as before.
  std::map<int32_t, std::map<int32_t, std::vector<GTLRectInt>>> routing_net_checking_rects;
  std::map<int32_t, std::map<int32_t, bool>> routing_net_is_special;

  {
    std::map<int32_t, std::map<int32_t, std::vector<GTLRectInt>>> net_rects_to_merge_checking;
    for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
      if (drc_shape->get_is_routing() && drc_shape->get_net_idx() != -1) {
        const int32_t layer_idx = drc_shape->get_layer_idx();
        const int32_t net_idx = drc_shape->get_net_idx();
        routing_net_is_special[layer_idx][net_idx] = drc_shape->get_is_special_net();
        bool reuse_prepared = false;
        auto layer_it = layer_data.find(layer_idx);
        if (layer_it != layer_data.end()) {
          auto net_it = layer_it->second.nets.find(net_idx);
          reuse_prepared =
              net_it != layer_it->second.nets.end() && !net_it->second.has_env_shapes && net_it->second.polygon_count > 0;
        }
        if (!reuse_prepared) {
          net_rects_to_merge_checking[layer_idx][net_idx].push_back(DRCUTIL.convertToGTLRectInt(drc_shape->get_rect()));
        }
      }
    }
    for (auto& [layer_idx, net_map] : net_rects_to_merge_checking) {
      for (auto& [net_idx, rect_vec] : net_map) {
        GTLPolySetInt polyset;
        polyset.insert(rect_vec.begin(), rect_vec.end());
        gtl::get_max_rectangles(routing_net_checking_rects[layer_idx][net_idx], polyset);
      }
    }
  }
  for (const auto& [layer_idx, rv_layer_data] : layer_data) {
    for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
      if (net_idx == -1 || routing_net.has_env_shapes || routing_net.polygon_count <= 0) {
        continue;
      }
      auto special_it = routing_net_is_special.find(layer_idx);
      if (special_it == routing_net_is_special.end() || special_it->second.count(net_idx) == 0) {
        continue;
      }
      std::vector<GTLRectInt>& rect_list = routing_net_checking_rects[layer_idx][net_idx];
      for (const MaxRectData& max_rect : rv_layer_data.getMaxRects(routing_net)) {
        rect_list.push_back(max_rect.rect);
      }
    }
  }

  // check rules
  for (const auto& [routing_layer_idx, net_rects] : routing_net_checking_rects) {
    auto layer_data_it = layer_data.find(routing_layer_idx);
    if (layer_data_it == layer_data.end()) {
      continue;
    }
    const RVLayerData& rv_layer_data = layer_data_it->second;
    std::vector<Violation> layer_violations;
    std::vector<std::pair<GTLRectInt, int32_t>> overlap_metal_rects;
    std::vector<GTLRectInt> overlap_obs_rects;
    for (auto& [net_idx, rect_list] : net_rects) {
      bool is_special_net = routing_net_is_special[routing_layer_idx][net_idx];
      for (const GTLRectInt& gtl_rect : rect_list) {
        PlanarRect rect = DRCUTIL.convertToPlanarRect(gtl_rect);

        overlap_metal_rects.clear();
        rv_layer_data.queryMetalShortMetalRects(gtl_rect, std::back_inserter(overlap_metal_rects));
        for (auto [env_gtl_rect, env_net_idx] : overlap_metal_rects) {
          if (net_idx == env_net_idx) {
            continue;
          }
          PlanarRect env_rect = DRCUTIL.convertToPlanarRect(env_gtl_rect);
          if (!DRCUTIL.isClosedOverlap(rect, env_rect)) {
            continue;
          }
          Violation violation;
          violation.set_violation_type(ViolationType::kMetalShort);
          violation.set_is_routing(true);
          violation.set_violation_net_set({net_idx, env_net_idx});
          violation.set_layer_idx(routing_layer_idx);
          violation.set_rect(DRCUTIL.getOverlap(rect, env_rect));
          violation.set_required_size(0);
          layer_violations.push_back(std::move(violation));
        }

        overlap_obs_rects.clear();
        rv_layer_data.queryMetalShortObsRects(gtl_rect, std::back_inserter(overlap_obs_rects));
        for (const GTLRectInt& obs_gtl_rect : overlap_obs_rects) {
          PlanarRect obs_rect = DRCUTIL.convertToPlanarRect(obs_gtl_rect);
          if (!DRCUTIL.isClosedOverlap(rect, obs_rect)) {
            continue;
          }
          if (is_special_net && !DRCUTIL.isOpenOverlap(rect, obs_rect)) {
            continue;
          }
          Violation violation;
          violation.set_violation_type(ViolationType::kMetalShort);
          violation.set_is_routing(true);
          violation.set_violation_net_set({net_idx, -1});
          violation.set_layer_idx(routing_layer_idx);
          violation.set_rect(DRCUTIL.getOverlap(rect, obs_rect));
          violation.set_required_size(0);
          layer_violations.push_back(std::move(violation));
        }
      }
    }

    // postprocess, build final violations
    {
      if (layer_violations.size() > 1) {
        std::sort(layer_violations.begin(), layer_violations.end(), [](const Violation& a, const Violation& b) {
          const auto& ra = a.get_rect();
          const auto& rb = b.get_rect();
          if (ra.get_ll_x() != rb.get_ll_x())
            return ra.get_ll_x() < rb.get_ll_x();
          if (ra.get_ur_x() != rb.get_ur_x())
            return ra.get_ur_x() > rb.get_ur_x();
          if (ra.get_ll_y() != rb.get_ll_y())
            return ra.get_ll_y() < rb.get_ll_y();
          return ra.get_ur_y() > rb.get_ur_y();
        });

        std::vector<Violation> results;
        results.reserve(layer_violations.size());

        std::vector<const Violation*> active_set;

        for (const auto& v : layer_violations) {
          bool is_redundant = false;
          const auto& cur_r = v.get_rect();

          active_set.erase(
              std::remove_if(active_set.begin(), active_set.end(), [&](const Violation* p) { return p->get_rect().get_ur_x() < cur_r.get_ll_x(); }),
              active_set.end());

          for (const auto* p_active : active_set) {
            if (DRCUTIL.isInside(p_active->get_rect(), cur_r)) {
              is_redundant = true;
              break;
            }
          }

          if (!is_redundant) {
            results.push_back(v);
            active_set.push_back(&results.back());
          }
        }
        layer_violations = std::move(results);
      }

      rv_cluster.get_violation_list().insert(rv_cluster.get_violation_list().end(), std::make_move_iterator(layer_violations.begin()),
                                             std::make_move_iterator(layer_violations.end()));
    }
  }
}

}  // namespace idrc
