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

void RuleValidator::verifyMinimumCut(RVCluster& rv_cluster)
{
  std::vector<RoutingLayer>& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  const std::map<int32_t, std::vector<int32_t>>& routing_to_adjacent_cut_map = DRCDM.getDatabase().get_routing_to_adjacent_cut_map();
  const auto& layer_data = rv_cluster.get_layer_data();

  for (const auto& [routing_layer_idx, rv_layer_data] : layer_data) {
    std::vector<MinimumCutRule>& minimum_cut_rule_list = routing_layer_list[routing_layer_idx].get_minimum_cut_rule_list();
    if (minimum_cut_rule_list.empty()) {
      continue;
    }
    const auto cut_layer_map_it = routing_to_adjacent_cut_map.find(routing_layer_idx);
    if (cut_layer_map_it == routing_to_adjacent_cut_map.end() || cut_layer_map_it->second.empty()) {
      continue;
    }

    const std::vector<int32_t>& cut_layer_idx_list = cut_layer_map_it->second;
    int32_t above_cut_layer_idx = *std::max_element(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
    int32_t below_cut_layer_idx = *std::min_element(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
    std::map<int32_t, std::vector<Violation>> cut_violation_list_map;

    for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
      if (net_idx == -1) {
        continue;
      }
      for (const PolygonData& polygon_data : rv_layer_data.getPolygons(routing_net)) {
        GTLPolyInt polygon_poly;
        polygon_poly.set(polygon_data.hole_poly.begin(), polygon_data.hole_poly.end());
        for (const MaxRectData& max_rect_data : rv_layer_data.getMaxRects(polygon_data)) {
          const GTLRectInt& gtl_rect = max_rect_data.rect;
          PlanarRect routing_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
          for (int32_t rule_idx = static_cast<int32_t>(minimum_cut_rule_list.size()) - 1; rule_idx >= 0; rule_idx--) {
            const MinimumCutRule& curr_rule = minimum_cut_rule_list[rule_idx];
            if (routing_rect.getWidth() < curr_rule.width) {
              continue;
            }
            std::vector<int32_t> check_cut_layer_idx_list;
            if (curr_rule.has_from_above) {
              check_cut_layer_idx_list.push_back(above_cut_layer_idx);
            } else if (curr_rule.has_from_below) {
              check_cut_layer_idx_list.push_back(below_cut_layer_idx);
            } else {
              check_cut_layer_idx_list = cut_layer_idx_list;
            }

            std::map<int32_t, std::vector<PlanarRect>> rule_cut_rect_list_map;
            if (curr_rule.has_length && routing_rect.getLength() > curr_rule.length) {
              for (const MaxRectData& env_max_rect_data : rv_layer_data.getMaxRects(polygon_data)) {
                for (int32_t cut_layer_idx : check_cut_layer_idx_list) {
                  const auto cut_layer_data_it = layer_data.find(cut_layer_idx);
                  if (cut_layer_data_it == layer_data.end() || cut_layer_data_it->second.cut_pool.empty()) {
                    continue;
                  }
                  std::vector<CutData> cut_data_list;
                  cut_layer_data_it->second.queryCuts(env_max_rect_data.rect, std::back_inserter(cut_data_list));
                  for (const CutData& cut_data : cut_data_list) {
                    if (cut_data.isEnv) {
                      continue;
                    }
                    PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_data.rect);
                    if (DRCUTIL.getEuclideanDistance(cut_rect, routing_rect) > curr_rule.distance) {
                      continue;
                    }
                    std::vector<PlanarRect>& rule_cut_rect_list = rule_cut_rect_list_map[cut_layer_idx];
                    if (!DRCUTIL.exist(rule_cut_rect_list, cut_rect)) {
                      rule_cut_rect_list.push_back(cut_rect);
                    }
                  }
                }
              }
            } else {
              for (int32_t cut_layer_idx : check_cut_layer_idx_list) {
                const auto cut_layer_data_it = layer_data.find(cut_layer_idx);
                if (cut_layer_data_it == layer_data.end() || cut_layer_data_it->second.cut_pool.empty()) {
                  continue;
                }
                const RVLayerData& cut_layer_data = cut_layer_data_it->second;
                std::vector<CutData> cut_data_list;
                cut_layer_data.queryCuts(gtl_rect, std::back_inserter(cut_data_list));
                std::vector<PlanarRect> cut_rect_list;
                for (const CutData& cut_data : cut_data_list) {
                  if (cut_data.isEnv) {
                    continue;
                  }
                  PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_data.rect);
                  if (DRCUTIL.isClosedOverlap(cut_rect, routing_rect)) {
                    if (!DRCUTIL.exist(cut_rect_list, cut_rect)) {
                      cut_rect_list.push_back(cut_rect);
                    }
                  }
                }
                // All overlapping cuts are violations for this rule.
                for (const PlanarRect& cut_rect : cut_rect_list) {
                  rule_cut_rect_list_map[cut_layer_idx].push_back(cut_rect);
                }
              }
            }
            for (auto& [cut_layer_idx, cut_rect_list] : rule_cut_rect_list_map) {
              for (const PlanarRect& cut_rect : cut_rect_list) {
                Violation violation;
                violation.set_violation_type(ViolationType::kMinimumCut);
                violation.set_is_routing(true);
                violation.set_violation_net_set({net_idx});
                violation.set_layer_idx(routing_layer_idx);
                violation.set_rect(cut_rect);
                violation.set_required_size(curr_rule.num_cuts);
                cut_violation_list_map[cut_layer_idx].push_back(violation);
              }
            }
          }
        }
      }
    }
    for (auto& [cut_layer_idx, violation_list] : cut_violation_list_map) {
      rv_cluster.get_violation_list().insert(rv_cluster.get_violation_list().end(), violation_list.begin(), violation_list.end());
    }
  }
}

}  // namespace idrc
