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

void RuleValidator::verifyAdjacentCutSpacing(RVCluster& rv_cluster)
{
  std::vector<CutLayer>& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = DRCDM.getDatabase().get_cut_to_adjacent_routing_map();
  const auto& layer_data = rv_cluster.get_layer_data();

  for (const auto& [cut_layer_idx, cut_layer_data] : layer_data) {
    if (cut_layer_data.cut_pool.empty()) {
      continue;
    }

    int32_t routing_layer_idx = -1;
    {
      std::vector<int32_t>& routing_layer_idx_list = cut_to_adjacent_routing_map[cut_layer_idx];
      routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
    }
    CutLayer& cut_layer = cut_layer_list[cut_layer_idx];

    AdjacentCutSpacingRule& adj_cut_rule = cut_layer.get_adjacent_cut_rule();
    if (!adj_cut_rule.has_rule) {
      continue;
    }

    using RectKey = std::tuple<int32_t, int32_t, int32_t, int32_t>;
    using CandidateKey = std::tuple<int32_t, int32_t, int32_t, int32_t, int32_t, int32_t>;

    for (const CutData& cut_data : cut_layer_data.getCuts()) {
      GTLRectInt cut_gtl_rect = cut_data.rect;
      PlanarRect cut_rect = DRCUTIL.convertToPlanarRect(cut_gtl_rect);
      int32_t net_idx = cut_data.net_idx;

      std::vector<CutData> neighbor_cut_list;
      {
        PlanarRect check_rect = DRCUTIL.getEnlargedRect(cut_rect, adj_cut_rule.cut_within);
        cut_layer_data.queryCuts(DRCUTIL.convertToGTLRectInt(check_rect), std::back_inserter(neighbor_cut_list));
      }

      int32_t adjacent_cut_count = 0;
      std::set<RectKey> seen_neighbor_rects;
      bool has_representative_neighbor = false;
      CandidateKey representative_neighbor_key;
      int32_t representative_neighbor_net_idx = -1;

      for (const CutData& neighbor_cut_data : neighbor_cut_list) {
        int32_t env_net_idx = neighbor_cut_data.net_idx;
        PlanarRect env_rect = DRCUTIL.convertToPlanarRect(neighbor_cut_data.rect);
        if (cut_rect == env_rect) {
          continue;
        }

        int32_t distance = DRCUTIL.getEuclideanDistance(cut_rect, env_rect);
        if (distance < adj_cut_rule.cut_within) {
          RectKey neighbor_rect_key = std::make_tuple(env_rect.get_ll_x(), env_rect.get_ll_y(), env_rect.get_ur_x(), env_rect.get_ur_y());
          if (!seen_neighbor_rects.insert(neighbor_rect_key).second) {
            continue;
          }
          adjacent_cut_count++;
          if (distance < adj_cut_rule.cut_spacing && !(net_idx == -1 && env_net_idx == -1)) {
            CandidateKey candidate_key = std::make_tuple(distance, env_rect.get_ll_x(), env_rect.get_ll_y(), env_rect.get_ur_x(),
                                                         env_rect.get_ur_y(), env_net_idx);
            if (!has_representative_neighbor || candidate_key < representative_neighbor_key) {
              has_representative_neighbor = true;
              representative_neighbor_key = candidate_key;
              representative_neighbor_net_idx = env_net_idx;
            }
          }
        }
      }

      if (adjacent_cut_count < adj_cut_rule.adjacnet_cuts || !has_representative_neighbor) {
        continue;
      }

      Violation violation;
      violation.set_violation_type(ViolationType::kAdjacentCutSpacing);
      violation.set_is_routing(true);
      violation.set_violation_net_set({net_idx, representative_neighbor_net_idx});
      violation.set_layer_idx(routing_layer_idx);
      violation.set_rect(cut_rect);
      violation.set_required_size(adj_cut_rule.cut_spacing);
      rv_cluster.get_violation_list().push_back(std::move(violation));
    }
  }
}

}  // namespace idrc
