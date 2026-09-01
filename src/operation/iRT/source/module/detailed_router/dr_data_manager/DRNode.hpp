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

namespace irt {

#if 1  // astar
enum class DRNodeState
{
  kNone = 0,
  kOpen = 1,
  kClose = 2
};
#endif

class DRNode : public LayerCoord
{
 public:
  using OrientNetList = boost::container::vector<std::pair<Orientation, int32_t>>;

  static constexpr std::array<Orientation, 6> kOrientationList
      = {Orientation::kEast, Orientation::kWest, Orientation::kSouth, Orientation::kNorth, Orientation::kAbove, Orientation::kBelow};

  DRNode() = default;
  ~DRNode() = default;
  // getter
  int32_t get_neighbor_node_num() const { return _neighbor_node_num; }
  PlanarCoord& get_gcell_coord() { return _gcell_coord; }
  const OrientNetList& get_orient_fixed_rect_list() const { return _orient_fixed_rect_list; }
  const OrientNetList& get_orient_routed_rect_list() const { return _orient_routed_rect_list; }
  uint8_t get_direction_mask() const { return _direction_mask; }
  // setter
  void set_gcell_coord(const PlanarCoord& gcell_coord) { _gcell_coord = gcell_coord; }
  // function
  static bool isNeighborOrientation(Orientation orientation) { return Orientation::kEast <= orientation && orientation <= Orientation::kBelow; }
  DRNode* getNeighborNode(Orientation orientation) const
  {
    return isNeighborOrientation(orientation) ? _neighbor_node_list[getOrientationIdx(orientation)] : nullptr;
  }
  void setNeighborNode(Orientation orientation, DRNode* neighbor_node)
  {
    if (!isNeighborOrientation(orientation)) {
      RTLOG.error(Loc::current(), "The neighbor orientation is invalid!");
      return;
    }
    DRNode*& curr_neighbor_node = _neighbor_node_list[getOrientationIdx(orientation)];
    if (curr_neighbor_node == nullptr && neighbor_node != nullptr) {
      _neighbor_node_num++;
    } else if (curr_neighbor_node != nullptr && neighbor_node == nullptr) {
      _neighbor_node_num--;
    }
    curr_neighbor_node = neighbor_node;
  }
  bool hasNeighborNode(Orientation orientation) const { return getNeighborNode(orientation) != nullptr; }
  void addFixedRectNet(Orientation orientation, int32_t net_idx) { addOrientNet(_orient_fixed_rect_list, _fixed_rect_net_state, orientation, net_idx); }
  void addRoutedRectNet(Orientation orientation, int32_t net_idx) { addOrientNet(_orient_routed_rect_list, _routed_rect_net_state, orientation, net_idx); }
  void delFixedRectNet(Orientation orientation, int32_t net_idx) { delOrientNet(_orient_fixed_rect_list, _fixed_rect_net_state, orientation, net_idx); }
  void delRoutedRectNet(Orientation orientation, int32_t net_idx) { delOrientNet(_orient_routed_rect_list, _routed_rect_net_state, orientation, net_idx); }
  bool hasFixedRectOrient(Orientation orientation) const { return _fixed_rect_net_state[getOrientationIdx(orientation)] != kNoOrientNetIdx; }
  double getFixedRectCost(int32_t net_idx, Orientation orientation, double fixed_rect_unit)
  {
    return getOrientNetCost(_fixed_rect_net_state, net_idx, orientation, fixed_rect_unit);
  }
  double getRoutedRectCost(int32_t net_idx, Orientation orientation, double routed_rect_unit)
  {
    return getOrientNetCost(_routed_rect_net_state, net_idx, orientation, routed_rect_unit);
  }
  double getViolationCost(Orientation orientation, double violation_unit) { return getViolationNumber(orientation) > 0 ? violation_unit : 0; }
  int32_t getViolationNumber(Orientation orientation) const
  {
    return isNeighborOrientation(orientation) ? _violation_number_list[getOrientationIdx(orientation)] : 0;
  }
  void addViolationNumber(Orientation orientation)
  {
    if (isNeighborOrientation(orientation)) {
      _violation_number_list[getOrientationIdx(orientation)]++;
    }
  }
  bool hasViolation() const
  {
    for (int32_t violation_number : _violation_number_list) {
      if (violation_number > 0) {
        return true;
      }
    }
    return false;
  }
#if 1  // astar
  // single task
  static uint8_t getDirectionMask(Direction direction) { return static_cast<uint8_t>(1U << static_cast<uint8_t>(direction)); }
  void setDirectionSet(const std::set<Direction>& direction_set)
  {
    _direction_mask = 0;
    for (Direction direction : direction_set) {
      addDirection(direction);
    }
  }
  void addDirection(Direction direction) { _direction_mask |= getDirectionMask(direction); }
  bool hasDirection(Direction direction) const { return (_direction_mask & getDirectionMask(direction)) != 0; }
  bool hasDirection() const { return _direction_mask != 0; }
  void clearDirection() { _direction_mask = 0; }
  // single path
  DRNodeState& get_state() { return _state; }
  DRNode* get_parent_node() const { return _parent_node; }
  double get_known_cost() const { return _known_cost; }
  double get_estimated_cost() const { return _estimated_cost; }
  int32_t get_open_queue_idx() const { return _open_queue_idx; }
  void set_state(DRNodeState state) { _state = state; }
  void set_parent_node(DRNode* parent_node) { _parent_node = parent_node; }
  void set_known_cost(const double known_cost) { _known_cost = known_cost; }
  void set_estimated_cost(const double estimated_cost) { _estimated_cost = estimated_cost; }
  void set_open_queue_idx(int32_t open_queue_idx) { _open_queue_idx = open_queue_idx; }
  // function
  bool isNone() { return _state == DRNodeState::kNone; }
  bool isOpen() { return _state == DRNodeState::kOpen; }
  bool isClose() { return _state == DRNodeState::kClose; }
  double getTotalCost() { return (_known_cost + _estimated_cost); }
#endif

 private:
  static constexpr int32_t kNoOrientNetIdx = std::numeric_limits<int32_t>::min();
  static constexpr int32_t kManyOrientNetIdx = kNoOrientNetIdx + 1;
  static constexpr size_t getOrientationIdx(Orientation orientation) { return static_cast<size_t>(orientation) - 1; }
  static void addOrientNet(OrientNetList& orient_net_list, std::array<int32_t, 6>& orient_net_state_list, Orientation orientation, int32_t net_idx)
  {
    int32_t& orient_net_state = orient_net_state_list[getOrientationIdx(orientation)];
    if (orient_net_state == net_idx) {
      return;
    }
    std::pair<Orientation, int32_t> orient_net = {orientation, net_idx};
    if (orient_net_state == kManyOrientNetIdx && std::find(orient_net_list.begin(), orient_net_list.end(), orient_net) != orient_net_list.end()) {
      return;
    }
    orient_net_list.push_back(orient_net);
    if (orient_net_state == kNoOrientNetIdx) {
      orient_net_state = net_idx;
    } else {
      orient_net_state = kManyOrientNetIdx;
    }
  }
  static void delOrientNet(OrientNetList& orient_net_list, std::array<int32_t, 6>& orient_net_state_list, Orientation orientation, int32_t net_idx)
  {
    int32_t& orient_net_state = orient_net_state_list[getOrientationIdx(orientation)];
    if (orient_net_state == kNoOrientNetIdx || (orient_net_state != kManyOrientNetIdx && orient_net_state != net_idx)) {
      return;
    }
    auto iter = std::find(orient_net_list.begin(), orient_net_list.end(), std::make_pair(orientation, net_idx));
    if (iter == orient_net_list.end()) {
      return;
    }
    *iter = orient_net_list.back();
    orient_net_list.pop_back();
    if (orient_net_state != kManyOrientNetIdx) {
      orient_net_state = kNoOrientNetIdx;
      return;
    }
    orient_net_state = kNoOrientNetIdx;
    for (const auto& [curr_orientation, curr_net_idx] : orient_net_list) {
      if (curr_orientation != orientation) {
        continue;
      }
      if (orient_net_state == kNoOrientNetIdx) {
        orient_net_state = curr_net_idx;
      } else {
        orient_net_state = kManyOrientNetIdx;
        break;
      }
    }
  }
  static double getOrientNetCost(const std::array<int32_t, 6>& orient_net_state_list, int32_t net_idx, Orientation orientation, double unit)
  {
    int32_t orient_net_state = orient_net_state_list[getOrientationIdx(orientation)];
    return (orient_net_state == kNoOrientNetIdx || orient_net_state == net_idx) ? 0 : unit;
  }

#if 1  // astar
  DRNodeState _state = DRNodeState::kNone;
  uint8_t _direction_mask = 0;
#endif
  uint8_t _neighbor_node_num = 0;
#if 1  // astar
  int32_t _open_queue_idx = -1;
  DRNode* _parent_node = nullptr;
  double _known_cost = 0.0;  // include curr
  double _estimated_cost = 0.0;
#endif
  std::array<DRNode*, 6> _neighbor_node_list{};
  // obstacle & pin_shape state
  std::array<int32_t, 6> _fixed_rect_net_state = {kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx};
  // net_result state
  std::array<int32_t, 6> _routed_rect_net_state = {kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx, kNoOrientNetIdx};
  // violation
  std::array<int32_t, 6> _violation_number_list{};
  PlanarCoord _gcell_coord;
  OrientNetList _orient_fixed_rect_list;
  OrientNetList _orient_routed_rect_list;
};

#if 1  // astar
struct CmpDRNodeCost
{
  bool operator()(DRNode* a, DRNode* b)
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
