// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may use this file under the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// ***************************************************************************************
#pragma once

#include "Orientation.hpp"
#include "RTHeader.hpp"

namespace irt {

using RoutingOrientAllowedNetMap = std::map<Orientation, std::set<int32_t>>;
using RoutingLayerAllowedNetMap = std::map<int32_t, RoutingOrientAllowedNetMap>;

inline bool isRoutingNetAllowed(const RoutingOrientAllowedNetMap& orient_allowed_net_map, int32_t net_idx, Orientation orient)
{
  auto iter = orient_allowed_net_map.find(orient);
  return iter == orient_allowed_net_map.end() || iter->second.find(net_idx) != iter->second.end();
}

inline int32_t getRoutingPolicyOverflow(const RoutingOrientAllowedNetMap& orient_allowed_net_map, int32_t net_idx,
                                        const std::set<Orientation>& orient_set)
{
  int32_t policy_overflow = 0;
  for (Orientation orient : orient_set) {
    if (!isRoutingNetAllowed(orient_allowed_net_map, net_idx, orient)) {
      policy_overflow++;
    }
  }
  return policy_overflow;
}

inline int32_t getRoutingPolicyOverflow(const RoutingOrientAllowedNetMap& orient_allowed_net_map,
                                        const std::map<int32_t, std::set<Orientation>>& net_orient_map)
{
  int32_t policy_overflow = 0;
  for (auto& [net_idx, orient_set] : net_orient_map) {
    policy_overflow += getRoutingPolicyOverflow(orient_allowed_net_map, net_idx, orient_set);
  }
  return policy_overflow;
}

}  // namespace irt
