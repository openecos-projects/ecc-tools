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

#include <cstddef>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace irt {

class TATrackTable
{
 public:
  struct Key
  {
    int32_t layer_idx = -1;
    int32_t track_coord = 0;

    bool operator==(const Key& rhs) const
    {
      return layer_idx == rhs.layer_idx && track_coord == rhs.track_coord;
    }

    struct Hash
    {
      size_t operator()(const Key& key) const
      {
        uint64_t value = static_cast<uint64_t>(key.pack()) * 0x9E3779B97F4A7C15ULL;
        return static_cast<size_t>(value ^ (value >> 32));
      }
    };

   private:
    int64_t pack() const
    {
      return (static_cast<int64_t>(layer_idx) << 32) | static_cast<int64_t>(static_cast<uint32_t>(track_coord));
    }
  };

  struct Overlap
  {
    int32_t interval_num = 0;
    double length = 0.0;
  };

  TATrackTable() = default;
  ~TATrackTable() = default;

  void clear();
  void ripupNet(int32_t net_idx);
  Overlap getOverlap(const Key& key, int32_t begin, int32_t end, int32_t net_idx) const;
  std::set<int32_t> scanOverflow(double history_penalty);

  void addUsage(const Key& key, int32_t begin, int32_t end, int32_t net_idx)
  {
    if (begin > end) {
      std::swap(begin, end);
    }
    if (begin == end) {
      return;  // zero extent occupies nothing, and would be seen as live by the overflow sweep
    }
    Interval interval;
    interval.begin = begin;
    interval.end = end;
    interval.net_idx = net_idx;
    usage_[key].push_back(interval);
    net_index_[net_idx].push_back(key);
    dirty_key_set_.insert(key);
  }

  double getHistoryCost(const Key& key) const
  {
    auto history_iter = history_cost_.find(key);
    if (history_iter == history_cost_.end()) {
      return 0.0;
    }
    return history_iter->second;
  }

 private:
  struct Interval
  {
    int32_t begin = 0;
    int32_t end = 0;
    int32_t net_idx = -1;
  };

  std::unordered_map<Key, std::vector<Interval>, Key::Hash> usage_;
  std::unordered_map<Key, double, Key::Hash> history_cost_;
  std::unordered_map<int32_t, std::vector<Key>> net_index_;  // net -> its tracks, for rip up
  std::unordered_set<Key, Key::Hash> dirty_key_set_;        // tracks changed since the last scan
};

}  // namespace irt
