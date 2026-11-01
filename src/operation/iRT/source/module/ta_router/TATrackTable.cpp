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
#include "TATrackTable.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <utility>

namespace irt {

void TATrackTable::clear()
{
  usage_.clear();
  history_cost_.clear();
  net_index_.clear();
  dirty_key_set_.clear();
}

void TATrackTable::ripupNet(int32_t net_idx)
{
  auto index_iter = net_index_.find(net_idx);
  if (index_iter == net_index_.end()) {
    return;
  }
  for (const Key& key : index_iter->second) {
    dirty_key_set_.insert(key);
    auto usage_iter = usage_.find(key);
    if (usage_iter == usage_.end()) {
      continue;
    }
    std::vector<Interval>& interval_list = usage_iter->second;
    interval_list.erase(std::remove_if(interval_list.begin(), interval_list.end(),
                                       [net_idx](const Interval& interval) { return interval.net_idx == net_idx; }),
                        interval_list.end());
    if (interval_list.empty()) {
      usage_.erase(usage_iter);
    }
  }
  net_index_.erase(index_iter);
}

TATrackTable::Overlap TATrackTable::getOverlap(const Key& key, int32_t begin, int32_t end, int32_t net_idx) const
{
  Overlap overlap;
  auto usage_iter = usage_.find(key);
  if (usage_iter == usage_.end()) {
    return overlap;
  }
  for (const Interval& interval : usage_iter->second) {
    if (interval.net_idx == net_idx) {
      continue;
    }
    int32_t overlap_begin = std::max(begin, interval.begin);
    int32_t overlap_end = std::min(end, interval.end);
    if (overlap_begin < overlap_end) {
      overlap.interval_num++;
      overlap.length += static_cast<double>(overlap_end - overlap_begin);
    }
  }
  return overlap;
}

std::set<int32_t> TATrackTable::scanOverflow(double history_penalty)
{
  std::set<int32_t> overflow_net_set;
  // reused across tracks to avoid per-track allocations
  std::set<int32_t> nets_on_track;
  std::set<int32_t> conflict_net_set;
  std::unordered_map<int32_t, int32_t> active_interval_count_by_net;
  std::set<int32_t> active_unmarked_net_set;
  std::priority_queue<std::pair<int32_t, int32_t>, std::vector<std::pair<int32_t, int32_t>>, std::greater<>> active_end_queue;

  // only tracks whose usage changed since the last scan can have a new conflict
  for (const Key& track_key : dirty_key_set_) {
    auto usage_iter = usage_.find(track_key);
    if (usage_iter == usage_.end()) {
      continue;
    }
    std::vector<Interval>& interval_list = usage_iter->second;
    if (interval_list.size() < 2) {
      continue;
    }
    // sweep in ascending begin order, so every interval seen so far starts at or before the current begin
    std::sort(interval_list.begin(), interval_list.end(), [](const Interval& a, const Interval& b) {
      return a.begin != b.begin ? a.begin < b.begin : a.end < b.end;
    });
    nets_on_track.clear();
    for (const Interval& interval : interval_list) {
      nets_on_track.insert(interval.net_idx);
    }
    if (nets_on_track.size() < 2) {
      continue;
    }

    // An interval [begin, end) is live at x iff begin <= x < end. Everything ending at or before the
    // current begin is retired first, so touching endpoints (end == begin) do not count as overlap.
    // a net can hold several intervals on one track, so count live intervals instead of keeping a net set
    conflict_net_set.clear();
    active_interval_count_by_net.clear();
    active_unmarked_net_set.clear();
    while (!active_end_queue.empty()) {
      active_end_queue.pop();
    }

    for (const Interval& interval : interval_list) {
      const int32_t net_idx = interval.net_idx;

      // retire expired intervals; a net stays live until its last interval expires
      while (!active_end_queue.empty() && active_end_queue.top().first <= interval.begin) {
        const int32_t expired_net_idx = active_end_queue.top().second;
        active_end_queue.pop();
        auto count_iter = active_interval_count_by_net.find(expired_net_idx);
        --(count_iter->second);
        if (count_iter->second > 0) {
          continue;
        }
        active_interval_count_by_net.erase(count_iter);
        active_unmarked_net_set.erase(expired_net_idx);
      }

      // every live interval covers the current begin, hence they all overlap each other:
      // one conflict marks the current net and all live nets at once
      // map size == number of live nets
      const bool net_is_live = active_interval_count_by_net.contains(net_idx);
      const bool other_net_is_live = net_is_live ? active_interval_count_by_net.size() > 1 : !active_interval_count_by_net.empty();
      if (other_net_is_live) {
        conflict_net_set.insert(net_idx);
        conflict_net_set.insert(active_unmarked_net_set.begin(), active_unmarked_net_set.end());
        active_unmarked_net_set.clear();
        if (conflict_net_set.size() == nets_on_track.size()) {
          break;  // every net on this track is already in conflict, the rest cannot add anything
        }
      }

      // the current interval becomes live; nets already in conflict never rejoin the unmarked set
      int32_t& active_interval_count = active_interval_count_by_net[net_idx];
      if (active_interval_count == 0 && !conflict_net_set.contains(net_idx)) {
        active_unmarked_net_set.insert(net_idx);
      }
      active_interval_count++;
      active_end_queue.emplace(interval.end, net_idx);
    }

    if (conflict_net_set.empty()) {
      continue;
    }
    // penalize the track once per scan, not once per conflict
    history_cost_[track_key] += history_penalty;
    overflow_net_set.insert(conflict_net_set.begin(), conflict_net_set.end());
  }
  dirty_key_set_.clear();
  return overflow_net_set;
}

}  // namespace irt
