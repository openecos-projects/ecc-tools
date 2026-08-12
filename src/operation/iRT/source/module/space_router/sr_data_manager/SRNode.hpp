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

#include "Direction.hpp"
#include "LayerCoord.hpp"
#include "Orientation.hpp"
#include "RoutingAllowedNet.hpp"
#include "RTHeader.hpp"
#include "Utility.hpp"

namespace irt {

#if 1  // astar
enum class SRNodeState
{
  kNone = 0,
  kOpen = 1,
  kClose = 2
};
#endif

inline bool isSRPlanarOrientation(Orientation orientation)
{
  return orientation == Orientation::kEast || orientation == Orientation::kWest || orientation == Orientation::kSouth
         || orientation == Orientation::kNorth;
}

inline bool isSRViaOrientation(Orientation orientation)
{
  return orientation == Orientation::kAbove || orientation == Orientation::kBelow;
}

inline int32_t getSROrientIndex(Orientation orientation)
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

inline Orientation getSROrientationByIndex(int32_t orient_idx)
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

class SRNode : public LayerCoord
{
 public:
  SRNode() = default;
  ~SRNode() = default;
  // getter
  double get_boundary_wire_unit() const { return _boundary_wire_unit; }
  double get_internal_wire_unit() const { return _internal_wire_unit; }
  double get_internal_via_unit() const { return _internal_via_unit; }
  std::map<Orientation, SRNode*>& get_neighbor_node_map() { return _neighbor_node_map; }
  std::map<Orientation, int32_t>& get_orient_supply_map() { return _orient_supply_map; }
  std::map<int32_t, std::set<Orientation>>& get_ignore_net_orient_map() { return _ignore_net_orient_map; }
  RoutingOrientAllowedNetMap& get_orient_allowed_net_map() { return _orient_allowed_net_map; }
  std::map<Orientation, std::set<int32_t>>& get_orient_net_map() { return _orient_net_map; }
  std::map<int32_t, std::set<Orientation>>& get_net_orient_map() { return _net_orient_map; }
  // setter
  void set_boundary_wire_unit(const double boundary_wire_unit) { _boundary_wire_unit = boundary_wire_unit; }
  void set_internal_wire_unit(const double internal_wire_unit) { _internal_wire_unit = internal_wire_unit; }
  void set_internal_via_unit(const double internal_via_unit) { _internal_via_unit = internal_via_unit; }
  void set_neighbor_node_map(const std::map<Orientation, SRNode*>& neighbor_node_map) { _neighbor_node_map = neighbor_node_map; }
  void set_orient_supply_map(const std::map<Orientation, int32_t>& orient_supply_map)
  {
    _orient_supply_map = orient_supply_map;
    rebuildFastSupply();
  }
  void set_ignore_net_orient_map(const std::map<int32_t, std::set<Orientation>>& ignore_net_orient_map) { _ignore_net_orient_map = ignore_net_orient_map; }
  void set_orient_allowed_net_map(const RoutingOrientAllowedNetMap& orient_allowed_net_map)
  {
    _orient_allowed_net_map = orient_allowed_net_map;
    rebuildFastDemand();
  }
  void set_orient_net_map(const std::map<Orientation, std::set<int32_t>>& orient_net_map) { _orient_net_map = orient_net_map; }
  void set_net_orient_map(const std::map<int32_t, std::set<Orientation>>& net_orient_map) { _net_orient_map = net_orient_map; }
  // function
  SRNode* getNeighborNode(Orientation orientation)
  {
    SRNode* neighbor_node = nullptr;
    if (RTUTIL.exist(_neighbor_node_map, orientation)) {
      neighbor_node = _neighbor_node_map[orientation];
    }
    return neighbor_node;
  }
  double getOverflowCost(int32_t net_idx, Direction direction, double overflow_unit)
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    std::map<Orientation, std::set<int32_t>> orient_net_map = _orient_net_map;
    std::map<int32_t, std::set<Orientation>> net_orient_map = _net_orient_map;
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
    } else if (direction == Direction::kProximal) {
      for (Orientation orient : {Orientation::kAbove, Orientation::kBelow}) {
        orient_net_map[orient].insert(net_idx);
        net_orient_map[net_idx].insert(orient);
      }
    } else {
      RTLOG.error(Loc::current(), "The direction is error!");
    }
    double boundary_overflow = 0;
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
      boundary_overflow += calcCost(boundary_demand, boundary_supply);
    }
    double internal_overflow = 0;
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
      for (auto& [net_idx, orient_set] : net_orient_map) {
        if (RTUTIL.exist(_ignore_net_orient_map, net_idx)
            && (RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kAbove) || RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kBelow))) {
          continue;
        }
        if (RTUTIL.exist(orient_set, Orientation::kEast) || RTUTIL.exist(orient_set, Orientation::kWest) || RTUTIL.exist(orient_set, Orientation::kSouth)
            || RTUTIL.exist(orient_set, Orientation::kNorth)) {
          continue;
        }
        if (RTUTIL.exist(orient_set, Orientation::kAbove) || RTUTIL.exist(orient_set, Orientation::kBelow)) {
          internal_demand += _internal_via_unit;
        }
      }
      double internal_supply = 0;
      for (auto& [orient, supply] : _orient_supply_map) {
        internal_supply += supply;
      }
      internal_overflow += calcCost(internal_demand, internal_supply);
    }
    double cost = 0;
    cost += (overflow_unit * (boundary_overflow + internal_overflow));
    int32_t policy_overflow = getRoutingPolicyOverflow(_orient_allowed_net_map, net_orient_map);
    if (policy_overflow > 0) {
      cost += overflow_unit * calcCost(policy_overflow, 0);
    }
    return cost;
  }
  double getFastCost(int32_t net_idx, Direction direction, double overflow_unit)
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    std::array<int32_t, 4> orient_demand_count = _orient_demand_count;
    int32_t internal_wire_demand_count = _internal_wire_demand_count;
    int32_t internal_via_only_demand_count = _internal_via_only_demand_count;
    int32_t policy_overflow = _policy_overflow;

    std::set<Orientation> orient_set;
    if (RTUTIL.exist(_net_orient_map, net_idx)) {
      orient_set = _net_orient_map[net_idx];
    }
    policy_overflow -= getRoutingPolicyOverflow(_orient_allowed_net_map, net_idx, orient_set);
    delFastDemand(net_idx, orient_set, orient_demand_count, internal_wire_demand_count, internal_via_only_demand_count);
    addDirectionToOrientSet(direction, orient_set);
    policy_overflow += getRoutingPolicyOverflow(_orient_allowed_net_map, net_idx, orient_set);
    addFastDemand(net_idx, orient_set, orient_demand_count, internal_wire_demand_count, internal_via_only_demand_count);

    return getFastCostByDemandCount(orient_demand_count, internal_wire_demand_count, internal_via_only_demand_count, policy_overflow, overflow_unit);
  }
  bool validDemandUnit()
  {
    if (_boundary_wire_unit <= 0) {
      return false;
    }
    if (_internal_wire_unit <= 0) {
      return false;
    }
    if (_internal_via_unit <= 0) {
      return false;
    }
    return true;
  }
  double calcCost(double demand, double supply)
  {
    double cost = 0;
    if (demand == supply) {
      cost = 1;
    } else if (demand > supply) {
      cost = std::pow(demand - supply + 1, 4);
    } else if (demand < supply) {
      cost = std::pow(demand / supply, 4);
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
    for (auto& [net_idx, orient_set] : _net_orient_map) {
      if (RTUTIL.exist(_ignore_net_orient_map, net_idx)
          && (RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kAbove) || RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kBelow))) {
        continue;
      }
      if (RTUTIL.exist(orient_set, Orientation::kEast) || RTUTIL.exist(orient_set, Orientation::kWest) || RTUTIL.exist(orient_set, Orientation::kSouth)
          || RTUTIL.exist(orient_set, Orientation::kNorth)) {
        continue;
      }
      if (RTUTIL.exist(orient_set, Orientation::kAbove) || RTUTIL.exist(orient_set, Orientation::kBelow)) {
        internal_demand += _internal_via_unit;
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
      for (auto& [net_idx, orient_set] : _net_orient_map) {
        if (RTUTIL.exist(_ignore_net_orient_map, net_idx)
            && (RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kAbove) || RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kBelow))) {
          continue;
        }
        if (RTUTIL.exist(orient_set, Orientation::kEast) || RTUTIL.exist(orient_set, Orientation::kWest) || RTUTIL.exist(orient_set, Orientation::kSouth)
            || RTUTIL.exist(orient_set, Orientation::kNorth)) {
          continue;
        }
        if (RTUTIL.exist(orient_set, Orientation::kAbove) || RTUTIL.exist(orient_set, Orientation::kBelow)) {
          internal_demand += _internal_via_unit;
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
  std::set<int32_t> getOverflowNetSet()
  {
    if (!validDemandUnit()) {
      RTLOG.error(Loc::current(), "The demand unit is error!");
    }
    std::set<int32_t> overflow_net_set;
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
      if (boundary_demand - boundary_supply > 0) {
        overflow_net_set.insert(_orient_net_map[orient].begin(), _orient_net_map[orient].end());
      }
    }
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
      for (auto& [net_idx, orient_set] : _net_orient_map) {
        if (RTUTIL.exist(_ignore_net_orient_map, net_idx)
            && (RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kAbove) || RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kBelow))) {
          continue;
        }
        if (RTUTIL.exist(orient_set, Orientation::kEast) || RTUTIL.exist(orient_set, Orientation::kWest) || RTUTIL.exist(orient_set, Orientation::kSouth)
            || RTUTIL.exist(orient_set, Orientation::kNorth)) {
          continue;
        }
        if (RTUTIL.exist(orient_set, Orientation::kAbove) || RTUTIL.exist(orient_set, Orientation::kBelow)) {
          internal_demand += _internal_via_unit;
        }
      }
      double internal_supply = 0;
      for (auto& [orient, supply] : _orient_supply_map) {
        internal_supply += supply;
      }
      if (internal_demand - internal_supply > 0) {
        for (auto& [net_idx, orient_set] : _net_orient_map) {
          overflow_net_set.insert(net_idx);
        }
      }
    }
    for (auto& [net_idx, orient_set] : _net_orient_map) {
      if (getRoutingPolicyOverflow(_orient_allowed_net_map, net_idx, orient_set) > 0) {
        overflow_net_set.insert(net_idx);
      }
    }
    return overflow_net_set;
  }
  void updateDemand(int32_t net_idx, std::set<Orientation> orient_set, ChangeType change_type)
  {
    std::set<Orientation> old_orient_set;
    if (RTUTIL.exist(_net_orient_map, net_idx)) {
      old_orient_set = _net_orient_map[net_idx];
    }
    delFastDemand(net_idx, old_orient_set);
    for (const Orientation& orient : orient_set) {
      if (change_type == ChangeType::kAdd) {
        _orient_net_map[orient].insert(net_idx);
        _net_orient_map[net_idx].insert(orient);
      } else {
        if (RTUTIL.exist(_orient_net_map, orient)) {
          _orient_net_map[orient].erase(net_idx);
          if (_orient_net_map[orient].empty()) {
            _orient_net_map.erase(orient);
          }
        }
        if (RTUTIL.exist(_net_orient_map, net_idx)) {
          _net_orient_map[net_idx].erase(orient);
          if (_net_orient_map[net_idx].empty()) {
            _net_orient_map.erase(net_idx);
          }
        }
      }
    }
    std::set<Orientation> new_orient_set;
    if (RTUTIL.exist(_net_orient_map, net_idx)) {
      new_orient_set = _net_orient_map[net_idx];
    }
    addFastDemand(net_idx, new_orient_set);
  }
  void clearDemand()
  {
    _orient_net_map.clear();
    _net_orient_map.clear();
    clearFastDemand();
  }
  void rebuildFastDemand()
  {
    clearFastDemand();
    if (!_net_orient_map.empty()) {
      for (auto& [net_idx, orient_set] : _net_orient_map) {
        addFastDemand(net_idx, orient_set);
      }
      return;
    }
    std::map<int32_t, std::set<Orientation>> net_orient_map;
    for (auto& [orient, net_set] : _orient_net_map) {
      for (int32_t net_idx : net_set) {
        net_orient_map[net_idx].insert(orient);
      }
    }
    for (auto& [net_idx, orient_set] : net_orient_map) {
      addFastDemand(net_idx, orient_set);
    }
  }
  void rebuildFastSupply()
  {
    _orient_supply_count.fill(0);
    _internal_supply_count = 0;
    for (int32_t orient_idx = 0; orient_idx < 4; orient_idx++) {
      Orientation orient = getSROrientationByIndex(orient_idx);
      if (RTUTIL.exist(_orient_supply_map, orient)) {
        _orient_supply_count[orient_idx] = _orient_supply_map[orient];
      }
    }
    for (auto& [orient, supply] : _orient_supply_map) {
      _internal_supply_count += supply;
    }
  }
#if 1  // astar
  // single path
  SRNodeState& get_state() { return _state; }
  SRNode* get_parent_node() const { return _parent_node; }
  double get_known_cost() const { return _known_cost; }
  double get_estimated_cost() const { return _estimated_cost; }
  void set_state(SRNodeState state) { _state = state; }
  void set_parent_node(SRNode* parent_node) { _parent_node = parent_node; }
  void set_known_cost(const double known_cost) { _known_cost = known_cost; }
  void set_estimated_cost(const double estimated_cost) { _estimated_cost = estimated_cost; }
  // function
  bool isNone() { return _state == SRNodeState::kNone; }
  bool isOpen() { return _state == SRNodeState::kOpen; }
  bool isClose() { return _state == SRNodeState::kClose; }
  double getTotalCost() { return (_known_cost + _estimated_cost); }
#endif

 private:
  double _boundary_wire_unit = -1;
  double _internal_wire_unit = -1;
  double _internal_via_unit = -1;
  std::array<int32_t, 4> _orient_demand_count = {0, 0, 0, 0};
  std::array<int32_t, 4> _orient_supply_count = {0, 0, 0, 0};
  int32_t _internal_wire_demand_count = 0;
  int32_t _internal_via_only_demand_count = 0;
  int32_t _policy_overflow = 0;
  double _internal_supply_count = 0;
  std::map<Orientation, SRNode*> _neighbor_node_map;
  std::map<Orientation, int32_t> _orient_supply_map;
  std::map<int32_t, std::set<Orientation>> _ignore_net_orient_map;
  RoutingOrientAllowedNetMap _orient_allowed_net_map;
  std::map<Orientation, std::set<int32_t>> _orient_net_map;
  std::map<int32_t, std::set<Orientation>> _net_orient_map;
#if 1  // astar
  // single path
  SRNodeState _state = SRNodeState::kNone;
  SRNode* _parent_node = nullptr;
  double _known_cost = 0.0;  // include curr
  double _estimated_cost = 0.0;
#endif

  void addDirectionToOrientSet(Direction direction, std::set<Orientation>& orient_set)
  {
    if (direction == Direction::kHorizontal) {
      orient_set.insert(Orientation::kEast);
      orient_set.insert(Orientation::kWest);
    } else if (direction == Direction::kVertical) {
      orient_set.insert(Orientation::kSouth);
      orient_set.insert(Orientation::kNorth);
    } else if (direction == Direction::kProximal) {
      orient_set.insert(Orientation::kAbove);
      orient_set.insert(Orientation::kBelow);
    } else {
      RTLOG.error(Loc::current(), "The direction is error!");
    }
  }
  void clearFastDemand()
  {
    _orient_demand_count.fill(0);
    _internal_wire_demand_count = 0;
    _internal_via_only_demand_count = 0;
    _policy_overflow = 0;
  }
  void addFastDemand(int32_t net_idx, const std::set<Orientation>& orient_set)
  {
    _policy_overflow += getRoutingPolicyOverflow(_orient_allowed_net_map, net_idx, orient_set);
    addFastDemand(net_idx, orient_set, _orient_demand_count, _internal_wire_demand_count, _internal_via_only_demand_count);
  }
  void delFastDemand(int32_t net_idx, const std::set<Orientation>& orient_set)
  {
    _policy_overflow -= getRoutingPolicyOverflow(_orient_allowed_net_map, net_idx, orient_set);
    delFastDemand(net_idx, orient_set, _orient_demand_count, _internal_wire_demand_count, _internal_via_only_demand_count);
  }
  void addFastDemand(int32_t net_idx, const std::set<Orientation>& orient_set, std::array<int32_t, 4>& orient_demand_count,
                     int32_t& internal_wire_demand_count, int32_t& internal_via_only_demand_count)
  {
    updateFastDemand(net_idx, orient_set, 1, orient_demand_count, internal_wire_demand_count, internal_via_only_demand_count);
  }
  void delFastDemand(int32_t net_idx, const std::set<Orientation>& orient_set, std::array<int32_t, 4>& orient_demand_count,
                     int32_t& internal_wire_demand_count, int32_t& internal_via_only_demand_count)
  {
    updateFastDemand(net_idx, orient_set, -1, orient_demand_count, internal_wire_demand_count, internal_via_only_demand_count);
  }
  void updateFastDemand(int32_t net_idx, const std::set<Orientation>& orient_set, int32_t delta, std::array<int32_t, 4>& orient_demand_count,
                        int32_t& internal_wire_demand_count, int32_t& internal_via_only_demand_count)
  {
    if (orient_set.empty()) {
      return;
    }
    bool has_planar_orient = false;
    bool has_via_orient = false;
    for (Orientation orient : orient_set) {
      if (isSRPlanarOrientation(orient)) {
        has_planar_orient = true;
        if (isIgnored(net_idx, orient)) {
          continue;
        }
        orient_demand_count[getSROrientIndex(orient)] += delta;
        internal_wire_demand_count += delta;
      } else if (isSRViaOrientation(orient)) {
        has_via_orient = true;
      }
    }
    if (!has_planar_orient && has_via_orient && !isViaIgnored(net_idx)) {
      internal_via_only_demand_count += delta;
    }
  }
  double getFastCostByDemandCount(const std::array<int32_t, 4>& orient_demand_count, int32_t internal_wire_demand_count,
                                  int32_t internal_via_only_demand_count, int32_t policy_overflow, double overflow_unit)
  {
    double boundary_overflow = 0;
    for (int32_t orient_idx = 0; orient_idx < 4; orient_idx++) {
      boundary_overflow += calcCost(orient_demand_count[orient_idx] * _boundary_wire_unit, _orient_supply_count[orient_idx]);
    }
    double internal_demand = internal_wire_demand_count * _internal_wire_unit + internal_via_only_demand_count * _internal_via_unit;
    double internal_overflow = calcCost(internal_demand, _internal_supply_count);
    double cost = overflow_unit * (boundary_overflow + internal_overflow);
    if (policy_overflow > 0) {
      cost += overflow_unit * calcCost(policy_overflow, 0);
    }
    return cost;
  }
  bool isIgnored(int32_t net_idx, Orientation orient)
  {
    return RTUTIL.exist(_ignore_net_orient_map, net_idx) && RTUTIL.exist(_ignore_net_orient_map[net_idx], orient);
  }
  bool isViaIgnored(int32_t net_idx)
  {
    return RTUTIL.exist(_ignore_net_orient_map, net_idx)
           && (RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kAbove)
               || RTUTIL.exist(_ignore_net_orient_map[net_idx], Orientation::kBelow));
  }
};

#if 1  // astar
struct CmpSRNodeCost
{
  bool operator()(SRNode* a, SRNode* b)
  {
    if (RTUTIL.equalDoubleByError(a->getTotalCost(), b->getTotalCost(), RT_ERROR)) {
      if (RTUTIL.equalDoubleByError(a->get_estimated_cost(), b->get_estimated_cost(), RT_ERROR)) {
        return a->get_neighbor_node_map().size() < b->get_neighbor_node_map().size();
      } else {
        return a->get_estimated_cost() > b->get_estimated_cost();
      }
    } else {
      return a->getTotalCost() > b->getTotalCost();
    }
  }
};
#endif

}  // namespace irt
