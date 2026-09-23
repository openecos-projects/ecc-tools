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

void RuleValidator::verifyMaxViaStack(RVCluster& rv_cluster)
{
  MaxViaStackRule& max_via_stack_rule = DRCDM.getDatabase().get_max_via_stack_rule();
  const auto& layer_data = rv_cluster.get_layer_data();

  int32_t max_via_stack_num = max_via_stack_rule.max_via_stack_num;
  int32_t bottom_cut_layer_idx = -1;
  {
    const std::vector<int32_t>& cut_layer_idx_list = DRCDM.getAdjacentCutLayerIdxList(max_via_stack_rule.bottom_routing_layer_idx);
    bottom_cut_layer_idx = *std::max_element(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
  }
  int32_t top_cut_layer_idx = -1;
  {
    const std::vector<int32_t>& cut_layer_idx_list = DRCDM.getAdjacentCutLayerIdxList(max_via_stack_rule.top_routing_layer_idx);
    top_cut_layer_idx = *std::min_element(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
  }
  for (const auto& [cut_layer_idx, cut_layer_data] : layer_data) {
    if (cut_layer_data.cut_pool.empty()) {
      continue;
    }
    if (cut_layer_idx < bottom_cut_layer_idx || top_cut_layer_idx < (cut_layer_idx + max_via_stack_num)) {
      continue;
    }

    std::map<int32_t, std::vector<PlanarRect>> net_rect_map;
    for (const CutData& cut_data : cut_layer_data.getCuts()) {
      net_rect_map[cut_data.net_idx].push_back(DRCUTIL.convertToPlanarRect(cut_data.rect));
    }

    for (const auto& [net_idx, rect_list] : net_rect_map) {
      for (const PlanarRect& seed_rect : rect_list) {
        std::pair<int32_t, PlanarRect> net_stack_rect_pair(net_idx, seed_rect);
        std::map<int32_t, std::vector<std::pair<int32_t, PlanarRect>>> layer_net_stack_rect_map;
        layer_net_stack_rect_map[cut_layer_idx].push_back(net_stack_rect_pair);
        for (int32_t curr_cut_layer_idx = cut_layer_idx; curr_cut_layer_idx < top_cut_layer_idx; curr_cut_layer_idx++) {
          std::map<int32_t, std::set<PlanarRect, CmpPlanarRectByXASC>> net_used_rect_set;
          for (size_t i = 0; i < layer_net_stack_rect_map[curr_cut_layer_idx].size(); i++) {
            auto next_layer_data_it = layer_data.find(curr_cut_layer_idx + 1);
            if (next_layer_data_it == layer_data.end()) {
              continue;
            }
            std::vector<CutData> neighbor_cut_list;
            next_layer_data_it->second.queryCuts(DRCUTIL.convertToGTLRectInt(net_stack_rect_pair.second), std::back_inserter(neighbor_cut_list));
            for (const CutData& neighbor_cut_data : neighbor_cut_list) {
              PlanarRect env_rect = DRCUTIL.convertToPlanarRect(neighbor_cut_data.rect);
              int32_t env_net_idx = neighbor_cut_data.net_idx;
              if (!DRCUTIL.isOpenOverlap(net_stack_rect_pair.second, env_rect)) {
                continue;
              }
              if (DRCUTIL.exist(net_used_rect_set[env_net_idx], env_rect)) {
                continue;
              }
              layer_net_stack_rect_map[curr_cut_layer_idx + 1].push_back({env_net_idx, env_rect});
              net_used_rect_set[env_net_idx].insert(env_rect);
            }
          }
        }
        if (static_cast<int32_t>(layer_net_stack_rect_map.size()) <= max_via_stack_num) {
          continue;
        }
        // 依次获取违例矩形
        for (auto& [curr_cut_layer_idx, net_stack_rect_map] : layer_net_stack_rect_map) {
          if (curr_cut_layer_idx < bottom_cut_layer_idx + max_via_stack_num || top_cut_layer_idx < curr_cut_layer_idx) {
            continue;
          }
          int32_t routing_layer_idx = -1;
          {
            const std::vector<int32_t>& routing_layer_idx_list = DRCDM.getAdjacentRoutingLayerIdxList(curr_cut_layer_idx);
            routing_layer_idx = *std::min_element(routing_layer_idx_list.begin(), routing_layer_idx_list.end());
          }
          for (auto& [curr_net_idx, curr_stack_rect] : net_stack_rect_map) {
            bool is_violation = true;
            if (DRCUTIL.exist(layer_net_stack_rect_map, curr_cut_layer_idx + 1)) {
              for (auto& [pre_net_idx, pre_stack_rect] : layer_net_stack_rect_map[curr_cut_layer_idx - 1]) {
                if (DRCUTIL.isOpenOverlap(pre_stack_rect, curr_stack_rect)) {
                  is_violation = false;
                  break;
                }
              }
            }
            if (!is_violation) {
              continue;
            }
            Violation violation;
            violation.set_violation_type(ViolationType::kMaxViaStack);
            violation.set_is_routing(true);
            violation.set_layer_idx(routing_layer_idx);
            violation.set_rect(curr_stack_rect);
            violation.set_violation_net_set({curr_net_idx});
            violation.set_required_size(max_via_stack_num);
            rv_cluster.get_violation_list().push_back(violation);
          }
        }
      }
    }
  }
}

}  // namespace idrc
