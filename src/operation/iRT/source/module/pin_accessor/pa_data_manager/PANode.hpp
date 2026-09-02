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

#include "Direction.hpp"
#include "LayerCoord.hpp"
#include "Orientation.hpp"
#include "RTHeader.hpp"
#include "Utility.hpp"
#include "ViaMasterIdx.hpp"

namespace irt {

#if 1  // astar
enum class PANodeState
{
  kNone = 0,
  kOpen = 1,
  kClose = 2
};
#endif

class PANode : public LayerCoord
{
 public:
  using OrientNetSet = boost::container::flat_set<std::pair<Orientation, int32_t>>;

  static constexpr std::array<Orientation, 6> kOrientationList
      = {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth, Orientation::kAbove, Orientation::kBelow};

  PANode() = default;
  ~PANode() = default;
  // getter
  const OrientNetSet& get_orient_fixed_rect_set() const { return _orient_fixed_rect_set; }
  const OrientNetSet& get_orient_routed_rect_set() const { return _orient_routed_rect_set; }
  int32_t get_neighbor_node_num() const { return _neighbor_node_num; }
  int32_t getViolationNumber(Orientation orientation) const { return _orient_violation_number_list[getOrientationIdx(orientation)]; }
  // function
  PANode* getNeighborNode(Orientation orientation) const { return _neighbor_node_list[getOrientationIdx(orientation)]; }
  void setNeighborNode(Orientation orientation, PANode* neighbor_node)
  {
    PANode*& curr_neighbor_node = _neighbor_node_list[getOrientationIdx(orientation)];
    if (curr_neighbor_node == nullptr && neighbor_node != nullptr) {
      _neighbor_node_num++;
    } else if (curr_neighbor_node != nullptr && neighbor_node == nullptr) {
      _neighbor_node_num--;
    }
    curr_neighbor_node = neighbor_node;
  }
  void addViolationNumber(Orientation orientation) { _orient_violation_number_list[getOrientationIdx(orientation)]++; }
  bool hasViolation() const
  {
    for (int32_t violation_number : _orient_violation_number_list) {
      if (violation_number != 0) {
        return true;
      }
    }
    return false;
  }
  double getFixedRectCost(int32_t net_idx, Orientation orientation, double fixed_rect_unit)
  {
    return getOrientNetCost(_fixed_rect_net_state, net_idx, orientation, fixed_rect_unit);
  }
  double getRoutedRectCost(int32_t net_idx, Orientation orientation, double routed_rect_unit)
  {
    return getOrientNetCost(_routed_rect_net_state, net_idx, orientation, routed_rect_unit);
  }
  double getViolationCost(Orientation orientation, double violation_unit)
  {
    double cost = 0;
    if (getViolationNumber(orientation) > 0) {
      cost = violation_unit;
    }
    return cost;
  }
  void addFixedRectNet(Orientation orientation, int32_t net_idx) { addOrientNet(_orient_fixed_rect_set, _fixed_rect_net_state, orientation, net_idx); }
  void addRoutedRectNet(Orientation orientation, int32_t net_idx) { addOrientNet(_orient_routed_rect_set, _routed_rect_net_state, orientation, net_idx); }
  void delFixedRectNet(Orientation orientation, int32_t net_idx) { delOrientNet(_orient_fixed_rect_set, _fixed_rect_net_state, orientation, net_idx); }
  void delRoutedRectNet(Orientation orientation, int32_t net_idx) { delOrientNet(_orient_routed_rect_set, _routed_rect_net_state, orientation, net_idx); }
  bool hasFixedRectOrient(Orientation orientation) const { return _fixed_rect_net_state[getOrientationIdx(orientation)] != kNoOrientNetIdx; }
#if 1  // astar
  // single path
  PANodeState& get_state() { return _state; }
  PANode* get_parent_node() const { return _parent_node; }
  ViaMasterIdx& get_parent_via_master_idx() { return _parent_via_master_idx; }
  double get_known_cost() const { return _known_cost; }
  double get_estimated_cost() const { return _estimated_cost; }
  int32_t get_open_queue_idx() const { return _open_queue_idx; }
  void set_state(PANodeState state) { _state = state; }
  void set_parent_node(PANode* parent_node) { _parent_node = parent_node; }
  void set_parent_via_master_idx(const ViaMasterIdx& parent_via_master_idx) { _parent_via_master_idx = parent_via_master_idx; }
  void set_known_cost(const double known_cost) { _known_cost = known_cost; }
  void set_estimated_cost(const double estimated_cost) { _estimated_cost = estimated_cost; }
  void set_open_queue_idx(int32_t open_queue_idx) { _open_queue_idx = open_queue_idx; }
  // function
  bool isNone() { return _state == PANodeState::kNone; }
  bool isOpen() { return _state == PANodeState::kOpen; }
  bool isClose() { return _state == PANodeState::kClose; }
  double getTotalCost() { return (_known_cost + _estimated_cost); }
#endif

 private:
  static constexpr int32_t kNoOrientNetIdx = std::numeric_limits<int32_t>::min();
  static constexpr int32_t kManyOrientNetIdx = kNoOrientNetIdx + 1;
  static constexpr size_t getOrientationIdx(Orientation orientation) { return static_cast<size_t>(orientation) - 1; }
  static void addOrientNet(OrientNetSet& orient_net_set, std::array<int32_t, 6>& orient_net_state_list, Orientation orientation, int32_t net_idx)
  {
    if (!orient_net_set.insert({orientation, net_idx}).second) {
      return;
    }
    int32_t& orient_net_state = orient_net_state_list[getOrientationIdx(orientation)];
    if (orient_net_state == kNoOrientNetIdx) {
      orient_net_state = net_idx;
    } else if (orient_net_state != net_idx) {
      orient_net_state = kManyOrientNetIdx;
    }
  }
  static void delOrientNet(OrientNetSet& orient_net_set, std::array<int32_t, 6>& orient_net_state_list, Orientation orientation, int32_t net_idx)
  {
    if (orient_net_set.erase({orientation, net_idx}) == 0) {
      return;
    }
    int32_t& orient_net_state = orient_net_state_list[getOrientationIdx(orientation)];
    if (orient_net_state != kManyOrientNetIdx) {
      orient_net_state = kNoOrientNetIdx;
      return;
    }
    auto iter = orient_net_set.lower_bound({orientation, std::numeric_limits<int32_t>::min()});
    if (iter == orient_net_set.end() || iter->first != orientation) {
      orient_net_state = kNoOrientNetIdx;
      return;
    }
    auto next_iter = iter;
    ++next_iter;
    orient_net_state = (next_iter == orient_net_set.end() || next_iter->first != orientation) ? iter->second : kManyOrientNetIdx;
  }
  static double getOrientNetCost(const std::array<int32_t, 6>& orient_net_state_list, int32_t net_idx, Orientation orientation, double unit)
  {
    int32_t orient_net_state = orient_net_state_list[getOrientationIdx(orientation)];
    return (orient_net_state == kNoOrientNetIdx || orient_net_state == net_idx) ? 0 : unit;
  }

  std::array<PANode*, 6> _neighbor_node_list{};
  int32_t _neighbor_node_num = 0;
  // obstacle & pin_shape
  OrientNetSet _orient_fixed_rect_set;
  std::array<int32_t, 6> _fixed_rect_net_state = {kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx};
  // net_result
  OrientNetSet _orient_routed_rect_set;
  std::array<int32_t, 6> _routed_rect_net_state = {kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx};
  // violation
  std::array<int32_t, 6> _orient_violation_number_list{};
#if 1  // astar
  // single path
  PANodeState _state = PANodeState::kNone;
  PANode* _parent_node = nullptr;
  ViaMasterIdx _parent_via_master_idx;
  double _known_cost = 0.0;  // include curr
  double _estimated_cost = 0.0;
  int32_t _open_queue_idx = -1;
#endif
};

#if 1  // astar
struct CmpPANodeCost
{
  bool operator()(PANode* a, PANode* b)
  {
    if (RTUTIL.equalDoubleByError(a->getTotalCost(), b->getTotalCost(), RT_ERROR)) {
      if (RTUTIL.equalDoubleByError(a->get_estimated_cost(), b->get_estimated_cost(), RT_ERROR)) {
        return a->get_neighbor_node_num() < b->get_neighbor_node_num();
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
