#pragma once

#include "DRNodeNetState.hpp"
#include "Logger.hpp"

namespace irt {

class DRNodeNetCountState
{
 public:
  double getCost(int32_t net_idx, Orientation orientation, double unit) const
  {
    int32_t orient_net_state = _orient_net_state_list[getOrientationIdx(orientation)];
    return (orient_net_state == kNoNetIdx || orient_net_state == net_idx) ? 0 : unit;
  }
  void addNet(Orientation orientation, int32_t net_idx)
  {
    size_t orient_idx = getOrientationIdx(orientation);
    int32_t& orient_net_state = _orient_net_state_list[orient_idx];
    if (orient_net_state == net_idx) {
      _orient_single_net_count_list[orient_idx]++;
      return;
    }
    if (orient_net_state == kNoNetIdx) {
      orient_net_state = net_idx;
      _orient_single_net_count_list[orient_idx] = 1;
      return;
    }
    if (orient_net_state == kManyNetIdx) {
      for (OrientNetCount& orient_net_count : _orient_net_count_list) {
        if (orient_net_count.orientation == orientation && orient_net_count.net_idx == net_idx) {
          orient_net_count.count++;
          return;
        }
      }
    } else {
      _orient_net_count_list.push_back({orientation, orient_net_state, _orient_single_net_count_list[orient_idx]});
      orient_net_state = kManyNetIdx;
      _orient_single_net_count_list[orient_idx] = 0;
    }
    _orient_net_count_list.push_back({orientation, net_idx, 1});
  }
  void delNet(Orientation orientation, int32_t net_idx)
  {
    size_t orient_idx = getOrientationIdx(orientation);
    int32_t& orient_net_state = _orient_net_state_list[orient_idx];
    if (orient_net_state == net_idx) {
      if (--_orient_single_net_count_list[orient_idx] == 0) {
        orient_net_state = kNoNetIdx;
      }
      return;
    }
    auto net_iter = _orient_net_count_list.begin();
    for (; net_iter != _orient_net_count_list.end(); ++net_iter) {
      if (net_iter->orientation == orientation && net_iter->net_idx == net_idx) {
        break;
      }
    }
    if (net_iter == _orient_net_count_list.end()) {
      RTLOG.error(Loc::current(), "Missing routed DR node contribution for net ", net_idx, " orientation ", static_cast<int32_t>(orientation));
    }
    if (--net_iter->count > 0) {
      return;
    }
    *net_iter = _orient_net_count_list.back();
    _orient_net_count_list.pop_back();
    auto remaining_iter = _orient_net_count_list.end();
    for (auto curr_iter = _orient_net_count_list.begin(); curr_iter != _orient_net_count_list.end(); ++curr_iter) {
      if (curr_iter->orientation != orientation) {
        continue;
      }
      if (remaining_iter != _orient_net_count_list.end()) {
        return;
      }
      remaining_iter = curr_iter;
    }
    orient_net_state = remaining_iter->net_idx;
    _orient_single_net_count_list[orient_idx] = remaining_iter->count;
    *remaining_iter = _orient_net_count_list.back();
    _orient_net_count_list.pop_back();
  }
  DRNodeNetState::OrientNetList getOrientNetList() const
  {
    DRNodeNetState::OrientNetList orient_net_list;
    for (const OrientNetCount& orient_net_count : _orient_net_count_list) {
      orient_net_list.emplace_back(orient_net_count.orientation, orient_net_count.net_idx);
    }
    for (size_t orient_idx = 0; orient_idx < _orient_net_state_list.size(); orient_idx++) {
      int32_t net_idx = _orient_net_state_list[orient_idx];
      if (net_idx != kNoNetIdx && net_idx != kManyNetIdx) {
        orient_net_list.emplace_back(static_cast<Orientation>(orient_idx + 1), net_idx);
      }
    }
    return orient_net_list;
  }

 private:
  struct OrientNetCount
  {
    Orientation orientation;
    int32_t net_idx;
    int32_t count;
  };

  static constexpr int32_t kNoNetIdx = std::numeric_limits<int32_t>::min();
  static constexpr int32_t kManyNetIdx = kNoNetIdx + 1;
  static constexpr size_t getOrientationIdx(Orientation orientation) { return static_cast<size_t>(orientation) - 1; }

  std::array<int32_t, 6> _orient_net_state_list = {kNoNetIdx, kNoNetIdx, kNoNetIdx, kNoNetIdx, kNoNetIdx, kNoNetIdx};
  std::array<int32_t, 6> _orient_single_net_count_list{};
  boost::container::vector<OrientNetCount> _orient_net_count_list;
};

}
