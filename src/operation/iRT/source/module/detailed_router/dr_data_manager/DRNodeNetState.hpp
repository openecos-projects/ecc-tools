#pragma once

#include "Orientation.hpp"
#include "RTHeader.hpp"

namespace irt {

class DRNodeNetState
{
 public:
  using OrientNetList = boost::container::vector<std::pair<Orientation, int32_t>>;

  bool hasNet(Orientation orientation) const { return _orient_net_state_list[getOrientationIdx(orientation)] != kNoNetIdx; }
  double getCost(int32_t net_idx, Orientation orientation, double unit) const
  {
    int32_t orient_net_state = _orient_net_state_list[getOrientationIdx(orientation)];
    return (orient_net_state == kNoNetIdx || orient_net_state == net_idx) ? 0 : unit;
  }
  void addNet(Orientation orientation, int32_t net_idx)
  {
    int32_t& orient_net_state = _orient_net_state_list[getOrientationIdx(orientation)];
    if (orient_net_state == net_idx) {
      return;
    }
    if (orient_net_state == kNoNetIdx) {
      orient_net_state = net_idx;
      return;
    }
    std::pair<Orientation, int32_t> orient_net = {orientation, net_idx};
    if (orient_net_state == kManyNetIdx) {
      if (std::find(_orient_net_list.begin(), _orient_net_list.end(), orient_net) != _orient_net_list.end()) {
        return;
      }
    } else {
      _orient_net_list.emplace_back(orientation, orient_net_state);
      orient_net_state = kManyNetIdx;
    }
    _orient_net_list.push_back(orient_net);
  }
  void delNet(Orientation orientation, int32_t net_idx)
  {
    int32_t& orient_net_state = _orient_net_state_list[getOrientationIdx(orientation)];
    if (orient_net_state == net_idx) {
      orient_net_state = kNoNetIdx;
      return;
    }
    if (orient_net_state != kManyNetIdx) {
      return;
    }
    auto net_iter = std::find(_orient_net_list.begin(), _orient_net_list.end(), std::make_pair(orientation, net_idx));
    if (net_iter == _orient_net_list.end()) {
      return;
    }
    *net_iter = _orient_net_list.back();
    _orient_net_list.pop_back();
    auto remaining_iter = _orient_net_list.end();
    for (auto curr_iter = _orient_net_list.begin(); curr_iter != _orient_net_list.end(); ++curr_iter) {
      if (curr_iter->first != orientation) {
        continue;
      }
      if (remaining_iter != _orient_net_list.end()) {
        return;
      }
      remaining_iter = curr_iter;
    }
    orient_net_state = remaining_iter->second;
    *remaining_iter = _orient_net_list.back();
    _orient_net_list.pop_back();
  }
  OrientNetList getOrientNetList() const
  {
    OrientNetList orient_net_list = _orient_net_list;
    for (size_t orient_idx = 0; orient_idx < _orient_net_state_list.size(); orient_idx++) {
      int32_t net_idx = _orient_net_state_list[orient_idx];
      if (net_idx != kNoNetIdx && net_idx != kManyNetIdx) {
        orient_net_list.emplace_back(static_cast<Orientation>(orient_idx + 1), net_idx);
      }
    }
    return orient_net_list;
  }

 private:
  static constexpr int32_t kNoNetIdx = std::numeric_limits<int32_t>::min();
  static constexpr int32_t kManyNetIdx = kNoNetIdx + 1;
  static constexpr size_t getOrientationIdx(Orientation orientation) { return static_cast<size_t>(orientation) - 1; }

  std::array<int32_t, 6> _orient_net_state_list = {kNoNetIdx, kNoNetIdx, kNoNetIdx, kNoNetIdx, kNoNetIdx, kNoNetIdx};
  OrientNetList _orient_net_list;
};

}
