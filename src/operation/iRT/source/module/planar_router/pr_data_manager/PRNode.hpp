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
#pragma once

#include <array>
#include <cstdint>

#include "Direction.hpp"
#include "LayerCoord.hpp"
#include "Orientation.hpp"
#include "RoutingAllowedNet.hpp"
#include "RTHeader.hpp"
#include "Utility.hpp"

namespace irt {

enum PROrientMask : uint8_t
{
  kPRMaskNone = 0,
  kPRMaskEast = 1 << 0,
  kPRMaskWest = 1 << 1,
  kPRMaskSouth = 1 << 2,
  kPRMaskNorth = 1 << 3,
  kPRMaskHorizontal = kPRMaskEast | kPRMaskWest,
  kPRMaskVertical = kPRMaskSouth | kPRMaskNorth
};

inline int32_t getPROrientIndex(Orientation orientation)
{
  switch (orientation) {
    case Orientation::kEast:
      return 0;
    case Orientation::kWest:
      return 1;
    case Orientation::kSouth:
      return 2;
    case Orientation::kNorth:
      return 3;
    default:
      RTLOG.error(Loc::current(), "The orientation is error!");
  }
  return -1;
}

inline Orientation getPROrientationByIndex(int32_t orient_idx)
{
  switch (orient_idx) {
    case 0:
      return Orientation::kEast;
    case 1:
      return Orientation::kWest;
    case 2:
      return Orientation::kSouth;
    case 3:
      return Orientation::kNorth;
    default:
      RTLOG.error(Loc::current(), "The orientation index is error!");
  }
  return Orientation::kNone;
}

inline uint8_t getPROrientMask(Orientation orientation)
{
  return static_cast<uint8_t>(1 << getPROrientIndex(orientation));
}

inline uint8_t getPRDirectionMask(Direction direction)
{
  if (direction == Direction::kHorizontal) {
    return kPRMaskHorizontal;
  }
  if (direction == Direction::kVertical) {
    return kPRMaskVertical;
  }
  RTLOG.error(Loc::current(), "The direction is error!");
  return kPRMaskNone;
}

inline int32_t getPRMaskBitNum(uint8_t mask)
{
  int32_t bit_num = 0;
  for (int32_t i = 0; i < 4; i++) {
    if (mask & (1 << i)) {
      bit_num++;
    }
  }
  return bit_num;
}

struct PRNodeCost
{
  double usage_cost = 0.0;
  double saturation_cost = 0.0;
  double hotspot_cost = 0.0;
  double overflow_cost = 0.0;
  double overflow = 0.0;
  double max_usage_ratio = 0.0;
  int32_t saturation_orient_num = 0;
  int32_t hotspot_orient_num = 0;
  int32_t overflow_orient_num = 0;

  double getTotalCost() const { return usage_cost + saturation_cost + hotspot_cost + overflow_cost; }
  void addCost(const PRNodeCost& cost)
  {
    usage_cost += cost.usage_cost;
    saturation_cost += cost.saturation_cost;
    hotspot_cost += cost.hotspot_cost;
    overflow_cost += cost.overflow_cost;
    overflow += cost.overflow;
    max_usage_ratio = std::max(max_usage_ratio, cost.max_usage_ratio);
    saturation_orient_num += cost.saturation_orient_num;
    hotspot_orient_num += cost.hotspot_orient_num;
    overflow_orient_num += cost.overflow_orient_num;
  }
};

class PRNode : public PlanarCoord
{
 public:
  PRNode() = default;
  ~PRNode() = default;
  // getter
  double get_boundary_wire_unit() const { return _boundary_wire_unit; }
  double get_internal_wire_unit() const { return _internal_wire_unit; }
  double get_internal_via_unit() const { return _internal_via_unit; }
  std::map<Orientation, PRNode*>& get_neighbor_node_map() { return _neighbor_node_map; }
  std::map<Orientation, int32_t>& get_orient_supply_map() { return _orient_supply_map; }
  std::map<int32_t, std::set<Orientation>>& get_ignore_net_orient_map() { return _ignore_net_orient_map; }
  RoutingOrientAllowedNetMap& get_orient_allowed_net_map() { return _orient_allowed_net_map; }
  std::map<Orientation, std::set<int32_t>>& get_orient_net_map() { return _orient_net_map; }
  std::map<int32_t, std::set<Orientation>>& get_net_orient_map() { return _net_orient_map; }
  std::map<Orientation, std::map<int32_t, int32_t>>& get_orient_net_ref_count_map() { return _orient_net_ref_count_map; }
  // setter
  void set_boundary_wire_unit(const double boundary_wire_unit) { _boundary_wire_unit = boundary_wire_unit; }
  void set_internal_wire_unit(const double internal_wire_unit) { _internal_wire_unit = internal_wire_unit; }
  void set_internal_via_unit(const double internal_via_unit) { _internal_via_unit = internal_via_unit; }
  void set_neighbor_node_map(const std::map<Orientation, PRNode*>& neighbor_node_map) { _neighbor_node_map = neighbor_node_map; }
  void set_orient_supply_map(const std::map<Orientation, int32_t>& orient_supply_map)
  {
    _orient_supply_map = orient_supply_map;
    _orient_supply_count.fill(0);
    _internal_supply_count = 0;
    for (auto& [orient, supply] : _orient_supply_map) {
      if (orient == Orientation::kEast || orient == Orientation::kWest || orient == Orientation::kSouth || orient == Orientation::kNorth) {
        _orient_supply_count[getPROrientIndex(orient)] = supply;
        _internal_supply_count += supply;
      }
    }
  }
  void set_ignore_net_orient_map(const std::map<int32_t, std::set<Orientation>>& ignore_net_orient_map)
  {
    _ignore_net_orient_map = ignore_net_orient_map;
    rebuildFastDemand();
  }
  void set_orient_allowed_net_map(const RoutingOrientAllowedNetMap& orient_allowed_net_map)
  {
    _orient_allowed_net_map = orient_allowed_net_map;
    rebuildFastDemand();
  }
  void set_orient_net_map(const std::map<Orientation, std::set<int32_t>>& orient_net_map)
  {
    _orient_net_map = orient_net_map;
    rebuildDemandRefCount();
    rebuildFastDemand();
  }
  void set_net_orient_map(const std::map<int32_t, std::set<Orientation>>& net_orient_map)
  {
    _net_orient_map = net_orient_map;
    rebuildDemandRefCount();
    rebuildFastDemand();
  }
  // function
  void clearDemand()
  {
    _orient_net_ref_count_map.clear();
    _orient_net_map.clear();
    _net_orient_map.clear();
    clearFastDemand();
  }
  PRNode* getNeighborNode(Orientation orientation)
  {
    PRNode* neighbor_node = nullptr;
    if (RTUTIL.exist(_neighbor_node_map, orientation)) {
      neighbor_node = _neighbor_node_map[orientation];
    }
    return neighbor_node;
  }
  PRNodeCost getCost(int32_t net_idx, Direction direction, double overflow_unit,
                     const std::set<Orientation>* extra_orient_set = nullptr)
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    std::map<Orientation, std::set<int32_t>> orient_net_map = _orient_net_map;
    std::map<int32_t, std::set<Orientation>> net_orient_map = _net_orient_map;
    if (extra_orient_set) {
      for (Orientation orient : *extra_orient_set) {
        orient_net_map[orient].insert(net_idx);
        net_orient_map[net_idx].insert(orient);
      }
    }
    if (direction == Direction::kHorizontal) {
      for (Orientation orient : {Orientation::kEast, Orientation::kWest}) {
        orient_net_map[orient].insert(net_idx);
        net_orient_map[net_idx].insert(orient);
      }
    } else if (direction == Direction::kVertical) {
      for (Orientation orient : {Orientation::kSouth, Orientation::kNorth}) {
        orient_net_map[orient].insert(net_idx);
        net_orient_map[net_idx].insert(orient);
      }
    } else {
      RTLOG.error(Loc::current(), "The direction is error!");
    }
    PRNodeCost node_cost;
    for (Orientation orient : {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth}) {
      double boundary_demand = 0;
      if (RTUTIL.exist(orient_net_map, orient)) {
        for (int32_t demand_net_idx : orient_net_map[orient]) {
          if (RTUTIL.exist(_ignore_net_orient_map, demand_net_idx) && RTUTIL.exist(_ignore_net_orient_map[demand_net_idx], orient)) {
            continue;
          }
          boundary_demand += _boundary_wire_unit;
        }
      }
      double boundary_supply = 0;
      if (RTUTIL.exist(_orient_supply_map, orient)) {
        boundary_supply = _orient_supply_map[orient];
      }
      node_cost.addCost(calcCost(boundary_demand, boundary_supply, overflow_unit));
    }
    {
      double internal_demand = 0;
      for (Orientation orient : {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth}) {
        if (RTUTIL.exist(orient_net_map, orient)) {
          for (int32_t demand_net_idx : orient_net_map[orient]) {
            if (RTUTIL.exist(_ignore_net_orient_map, demand_net_idx) && RTUTIL.exist(_ignore_net_orient_map[demand_net_idx], orient)) {
              continue;
            }
            internal_demand += _internal_wire_unit;
          }
        }
      }
      double internal_supply = 0;
      for (auto& [orient, supply] : _orient_supply_map) {
        internal_supply += supply;
      }
      node_cost.addCost(calcCost(internal_demand, internal_supply, overflow_unit));
    }
    addPolicyOverflow(node_cost, getRoutingPolicyOverflow(_orient_allowed_net_map, net_orient_map), overflow_unit);
    return node_cost;
  }
  double getOverflowCost(int32_t net_idx, Direction direction, double overflow_unit,
                         const std::set<Orientation>* extra_orient_set = nullptr)
  {
    return getCost(net_idx, direction, overflow_unit, extra_orient_set).getTotalCost();
  }
  PRNodeCost getFastCost(uint8_t orient_mask, double overflow_unit)
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    return getFastCostByDemandCount(_orient_demand_count, _internal_demand_count, orient_mask, _policy_overflow, overflow_unit);
  }
  PRNodeCost getFastCost(int32_t net_idx, uint8_t orient_mask, double overflow_unit, bool ignore_curr_net)
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    std::array<int32_t, 4> orient_demand_count = _orient_demand_count;
    int32_t internal_demand_count = _internal_demand_count;
    int32_t policy_overflow = _policy_overflow;
    uint8_t add_orient_mask = orient_mask;
    for (int32_t orient_idx = 0; orient_idx < 4; orient_idx++) {
      Orientation orient = getPROrientationByIndex(orient_idx);
      if (isIgnored(net_idx, orient)) {
        add_orient_mask &= static_cast<uint8_t>(~getPROrientMask(orient));
      }
    }
    if (RTUTIL.exist(_net_orient_map, net_idx)) {
      for (Orientation orient : _net_orient_map[net_idx]) {
        if (ignore_curr_net && !isRoutingNetAllowed(_orient_allowed_net_map, net_idx, orient)) {
          policy_overflow--;
        }
        if (isIgnored(net_idx, orient)) {
          continue;
        }
        if (ignore_curr_net) {
          orient_demand_count[getPROrientIndex(orient)]--;
          internal_demand_count--;
        } else {
          add_orient_mask &= static_cast<uint8_t>(~getPROrientMask(orient));
        }
      }
    }
    for (int32_t orient_idx = 0; orient_idx < 4; orient_idx++) {
      Orientation orient = getPROrientationByIndex(orient_idx);
      bool has_curr_orient = RTUTIL.exist(_net_orient_map, net_idx) && RTUTIL.exist(_net_orient_map[net_idx], orient);
      if ((orient_mask & (1 << orient_idx)) && (ignore_curr_net || !has_curr_orient)
          && !isRoutingNetAllowed(_orient_allowed_net_map, net_idx, orient)) {
        policy_overflow++;
      }
    }
    return getFastCostByDemandCount(orient_demand_count, internal_demand_count, add_orient_mask,
                                    policy_overflow, overflow_unit);
  }
  bool validDemandUnit()
  {
    if (_boundary_wire_unit <= 0) {
      return false;
    }
    if (_internal_wire_unit <= 0) {
      return false;
    }
    return true;
  }
  PRNodeCost calcCost(double demand, double supply, double overflow_unit)
  {
    constexpr double kSaturationStartRatio = 0.8;
    constexpr double kHotspotStartRatio = 0.9;
    constexpr double kFullSupplyPenaltyScale = 1.0;
    constexpr double kHotspotPenaltyScale = 2.0;

    PRNodeCost cost;
    if (supply <= 0) {
      if (demand <= 0) {
        return cost;
      }
      cost.max_usage_ratio = demand + 1.0;
      cost.overflow = demand;
      cost.overflow_orient_num = 1;
      cost.overflow_cost = overflow_unit * std::pow(cost.overflow + 1, 4);
      return cost;
    }

    double usage_ratio = demand / supply;
    cost.max_usage_ratio = usage_ratio;
    if (demand > supply) {
      cost.overflow = demand - supply;
      cost.overflow_orient_num = 1;
      cost.overflow_cost = overflow_unit * std::pow(cost.overflow + 1, 4);
      return cost;
    }

    cost.usage_cost = overflow_unit * std::pow(usage_ratio, 4);
    if (usage_ratio >= kSaturationStartRatio) {
      double saturation_ratio = (usage_ratio - kSaturationStartRatio) / (1.0 - kSaturationStartRatio);
      cost.saturation_orient_num = 1;
      cost.saturation_cost = overflow_unit * std::pow(saturation_ratio, 2);
      if (usage_ratio >= 1.0) {
        cost.saturation_cost += overflow_unit * kFullSupplyPenaltyScale;
      }
    }
    if (usage_ratio >= kHotspotStartRatio) {
      double hotspot_ratio = (usage_ratio - kHotspotStartRatio) / (1.0 - kHotspotStartRatio);
      cost.hotspot_orient_num = 1;
      cost.hotspot_cost = overflow_unit * kHotspotPenaltyScale * std::pow(hotspot_ratio, 2);
    }
    return cost;
  }
  double getDemand()
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    double boundary_demand = 0;
    for (Orientation orient : {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth}) {
      if (RTUTIL.exist(_orient_net_map, orient)) {
        for (int32_t demand_net_idx : _orient_net_map[orient]) {
          if (RTUTIL.exist(_ignore_net_orient_map, demand_net_idx) && RTUTIL.exist(_ignore_net_orient_map[demand_net_idx], orient)) {
            continue;
          }
          boundary_demand += _boundary_wire_unit;
        }
      }
    }
    double internal_demand = 0;
    for (Orientation orient : {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth}) {
      if (RTUTIL.exist(_orient_net_map, orient)) {
        for (int32_t demand_net_idx : _orient_net_map[orient]) {
          if (RTUTIL.exist(_ignore_net_orient_map, demand_net_idx) && RTUTIL.exist(_ignore_net_orient_map[demand_net_idx], orient)) {
            continue;
          }
          internal_demand += _internal_wire_unit;
        }
      }
    }
    return (boundary_demand + internal_demand);
  }
  double getOverflow()
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    double boundary_overflow = 0;
    for (Orientation orient : {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth}) {
      double boundary_demand = 0;
      if (RTUTIL.exist(_orient_net_map, orient)) {
        for (int32_t demand_net_idx : _orient_net_map[orient]) {
          if (RTUTIL.exist(_ignore_net_orient_map, demand_net_idx) && RTUTIL.exist(_ignore_net_orient_map[demand_net_idx], orient)) {
            continue;
          }
          boundary_demand += _boundary_wire_unit;
        }
      }
      double boundary_supply = 0;
      if (RTUTIL.exist(_orient_supply_map, orient)) {
        boundary_supply = _orient_supply_map[orient];
      }
      boundary_overflow += std::max(0.0, boundary_demand - boundary_supply);
    }
    double internal_overflow = 0;
    {
      double internal_demand = 0;
      for (Orientation orient : {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth}) {
        if (RTUTIL.exist(_orient_net_map, orient)) {
          for (int32_t demand_net_idx : _orient_net_map[orient]) {
            if (RTUTIL.exist(_ignore_net_orient_map, demand_net_idx) && RTUTIL.exist(_ignore_net_orient_map[demand_net_idx], orient)) {
              continue;
            }
            internal_demand += _internal_wire_unit;
          }
        }
      }
      double internal_supply = 0;
      for (auto& [orient, supply] : _orient_supply_map) {
        internal_supply += supply;
      }
      internal_overflow += std::max(0.0, internal_demand - internal_supply);
    }
    return (boundary_overflow + internal_overflow + _policy_overflow);
  }
  void updateDemand(int32_t net_idx, std::set<Orientation> orient_set, ChangeType change_type)
  {
    for (const Orientation& orient : orient_set) {
      if (change_type == ChangeType::kAdd) {
        int32_t& ref_count = _orient_net_ref_count_map[orient][net_idx];
        ref_count++;
        if (ref_count == 1) {
          _orient_net_map[orient].insert(net_idx);
          _net_orient_map[net_idx].insert(orient);
          addFastDemand(net_idx, orient);
        }
      } else {
        if (!RTUTIL.exist(_orient_net_ref_count_map, orient) || !RTUTIL.exist(_orient_net_ref_count_map[orient], net_idx)) {
          continue;
        }
        int32_t& ref_count = _orient_net_ref_count_map[orient][net_idx];
        ref_count--;
        if (ref_count > 0) {
          continue;
        }
        _orient_net_ref_count_map[orient].erase(net_idx);
        if (_orient_net_ref_count_map[orient].empty()) {
          _orient_net_ref_count_map.erase(orient);
        }
        if (RTUTIL.exist(_net_orient_map, net_idx) && RTUTIL.exist(_net_orient_map[net_idx], orient)) {
          delFastDemand(net_idx, orient);
        }
        _orient_net_map[orient].erase(net_idx);
        if (_orient_net_map[orient].empty()) {
          _orient_net_map.erase(orient);
        }
        if (RTUTIL.exist(_net_orient_map, net_idx)) {
          _net_orient_map[net_idx].erase(orient);
          if (_net_orient_map[net_idx].empty()) {
            _net_orient_map.erase(net_idx);
          }
        }
      }
    }
  }

 private:
  bool isIgnored(int32_t net_idx, Orientation orient)
  {
    return RTUTIL.exist(_ignore_net_orient_map, net_idx) && RTUTIL.exist(_ignore_net_orient_map[net_idx], orient);
  }
  void clearFastDemand()
  {
    _orient_demand_count.fill(0);
    _internal_demand_count = 0;
    _policy_overflow = 0;
  }
  void rebuildFastDemand()
  {
    clearFastDemand();
    for (auto& [orient, net_set] : _orient_net_map) {
      if (orient != Orientation::kEast && orient != Orientation::kWest && orient != Orientation::kSouth && orient != Orientation::kNorth) {
        continue;
      }
      for (int32_t net_idx : net_set) {
        addFastDemand(net_idx, orient);
      }
    }
  }
  void rebuildDemandRefCount()
  {
    _orient_net_ref_count_map.clear();
    if (!_orient_net_map.empty()) {
      for (auto& [orient, net_set] : _orient_net_map) {
        for (int32_t net_idx : net_set) {
          _orient_net_ref_count_map[orient][net_idx] = 1;
        }
      }
    } else {
      for (auto& [net_idx, orient_set] : _net_orient_map) {
        for (Orientation orient : orient_set) {
          _orient_net_ref_count_map[orient][net_idx] = 1;
        }
      }
    }
  }
  void addFastDemand(int32_t net_idx, Orientation orient)
  {
    if (!isRoutingNetAllowed(_orient_allowed_net_map, net_idx, orient)) {
      _policy_overflow++;
    }
    if (isIgnored(net_idx, orient)) {
      return;
    }
    _orient_demand_count[getPROrientIndex(orient)]++;
    _internal_demand_count++;
  }
  void delFastDemand(int32_t net_idx, Orientation orient)
  {
    if (!isRoutingNetAllowed(_orient_allowed_net_map, net_idx, orient)) {
      _policy_overflow--;
    }
    if (isIgnored(net_idx, orient)) {
      return;
    }
    _orient_demand_count[getPROrientIndex(orient)]--;
    _internal_demand_count--;
  }
  void addPolicyOverflow(PRNodeCost& node_cost, int32_t policy_overflow, double overflow_unit)
  {
    if (policy_overflow > 0) {
      node_cost.addCost(calcCost(policy_overflow, 0, overflow_unit));
    }
  }
  PRNodeCost getFastCostByDemandCount(const std::array<int32_t, 4>& orient_demand_count, int32_t internal_demand_count,
                                      uint8_t orient_mask, int32_t policy_overflow, double overflow_unit)
  {
    PRNodeCost node_cost;
    for (int32_t orient_idx = 0; orient_idx < 4; orient_idx++) {
      int32_t demand_count = orient_demand_count[orient_idx];
      if (orient_mask & (1 << orient_idx)) {
        demand_count++;
      }
      node_cost.addCost(calcCost(demand_count * _boundary_wire_unit, _orient_supply_count[orient_idx], overflow_unit));
    }
    int32_t total_internal_demand_count = internal_demand_count + getPRMaskBitNum(orient_mask);
    node_cost.addCost(calcCost(total_internal_demand_count * _internal_wire_unit, _internal_supply_count, overflow_unit));
    addPolicyOverflow(node_cost, policy_overflow, overflow_unit);
    return node_cost;
  }

  double _boundary_wire_unit = -1;
  double _internal_wire_unit = -1;
  double _internal_via_unit = -1;
  std::array<int32_t, 4> _orient_demand_count = {0, 0, 0, 0};
  std::array<int32_t, 4> _orient_supply_count = {0, 0, 0, 0};
  int32_t _internal_demand_count = 0;
  int32_t _internal_supply_count = 0;
  int32_t _policy_overflow = 0;
  std::map<Orientation, PRNode*> _neighbor_node_map;
  std::map<Orientation, int32_t> _orient_supply_map;
  std::map<int32_t, std::set<Orientation>> _ignore_net_orient_map;
  RoutingOrientAllowedNetMap _orient_allowed_net_map;
  std::map<Orientation, std::map<int32_t, int32_t>> _orient_net_ref_count_map;
  std::map<Orientation, std::set<int32_t>> _orient_net_map;
  std::map<int32_t, std::set<Orientation>> _net_orient_map;
};

}  // namespace irt
