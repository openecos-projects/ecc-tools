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

#include "CmpEnvTopoEdgeByCoordASC.hpp"
#include "EnvOverlapWidenContext.hpp"
#include "EnvSearchContext.hpp"
#include "EnvTrackOverlap.hpp"
#include "LineSegment.hpp"
#include "RCXHeader.hpp"
#include "TopoEdge.hpp"
#include "Utility.hpp"

namespace ircx {

class EnvTrackIdx
{
 public:
  using EnvEdgeSet = std::set<TopoEdge*, CmpEnvTopoEdgeByCoordASC>;
  EnvTrackIdx() = default;
  ~EnvTrackIdx() = default;

  // getter
  int32_t get_track_origin() const { return _track_origin; }
  int32_t get_track_count() const { return _track_count; }
  int32_t get_track_step() const { return _track_step; }
  int32_t get_bucket_origin() const { return _bucket_origin; }
  int32_t get_bucket_count() const { return _bucket_count; }
  int32_t get_bucket_step() const { return _bucket_step; }

  // setter
  void set_track_origin(int32_t track_origin) { _track_origin = track_origin; }
  void set_track_count(int32_t track_count) { _track_count = track_count; }
  void set_track_step(int32_t track_step) { _track_step = track_step; }
  void set_bucket_origin(int32_t bucket_origin) { _bucket_origin = bucket_origin; }
  void set_bucket_count(int32_t bucket_count) { _bucket_count = bucket_count; }
  void set_bucket_step(int32_t bucket_step) { _bucket_step = bucket_step; }

  // coord mapping
  int32_t getTrackIdxByCoord(int32_t coord) const { return (coord - _track_origin) / _track_step; }
  int32_t getBucketIdxByCoord(int32_t coord) const { return (coord - _bucket_origin) / _bucket_step; }

  bool initTrackBucketList()
  {
    if (_track_count <= 0 || _track_step <= 0) {
      return false;
    }
    if (_bucket_count <= 0 || _bucket_step <= 0) {
      return false;
    }

    _track_bucket_list.assign(_track_count, std::vector<EnvEdgeSet>(_bucket_count));
    return true;
  }

  void addTopoEdge(TopoEdge& edge)
  {
    int32_t start_coord = edge.get_line_segment().get_lower();
    int32_t end_coord = edge.get_line_segment().get_upper();
    RCXUTIL.normalizeInterval(start_coord, end_coord);

    int32_t track_idx = getTrackIdxByCoord(edge.get_line_segment().get_coord());
    int32_t start_bucket_idx = getBucketIdxByCoord(start_coord);
    int32_t end_bucket_idx = getBucketIdxByCoord(end_coord - 1);

    if (!isTrackIdxValid(track_idx) || !isBucketIdxValid(start_bucket_idx) || !isBucketIdxValid(end_bucket_idx)) {
      return;
    }

    for (int32_t bucket_idx = start_bucket_idx; bucket_idx <= end_bucket_idx; ++bucket_idx) {
      _track_bucket_list[track_idx][bucket_idx].insert(&edge);
    }
  }

  std::vector<EnvTrackOverlap> getOverlapList(const LineSegment& line_segment, int32_t search_track_num,
                                              const std::function<int32_t(const EnvOverlapWidenContext&)>& widen_func = {}) const
  {
    std::vector<EnvTrackOverlap> track_overlap_list;
    if (search_track_num == 0) {
      return track_overlap_list;
    }

    int32_t start_coord = line_segment.get_lower();
    int32_t end_coord = line_segment.get_upper();
    RCXUTIL.normalizeInterval(start_coord, end_coord);
    if (!RCXUTIL.isIntervalValid(start_coord, end_coord)) {
      return track_overlap_list;
    }

    int32_t track_coord = line_segment.get_coord();
    int32_t query_start_coord = start_coord;
    int32_t query_end_coord = end_coord;

    int32_t base_track_idx = getTrackIdxByCoord(track_coord);
    if (!isTrackIdxValid(base_track_idx)) {
      return track_overlap_list;
    }

    std::vector<IntervalRange<int32_t>> remaining_interval_list;
    remaining_interval_list.emplace_back(query_start_coord, query_end_coord);

    int32_t track_direction_step = (search_track_num > 0) ? 1 : -1;
    int32_t tracks_to_search = (search_track_num > 0) ? search_track_num : -search_track_num;

    EnvSearchContext search_context(track_coord, base_track_idx, query_start_coord, query_end_coord, track_direction_step, widen_func);

    remaining_interval_list = searchAcrossTracks(base_track_idx, tracks_to_search, remaining_interval_list, track_overlap_list, search_context);

    for (const IntervalRange<int32_t>& remaining_interval : remaining_interval_list) {
      EnvTrackOverlap track_overlap;
      track_overlap.set_start_coord(remaining_interval.get_start());
      track_overlap.set_end_coord(remaining_interval.get_end());
      track_overlap.set_spacing(INT32_MAX);
      track_overlap.set_edge(nullptr);
      track_overlap_list.push_back(track_overlap);
    }

    return track_overlap_list;
  }

 private:
  bool isEdgeInSearchDirection(TopoEdge* edge, const EnvSearchContext& search_context) const
  {
    if (edge == nullptr) {
      return false;
    }
    return (search_context.get_track_direction_step() > 0) ? (edge->get_line_segment().get_coord() > search_context.get_track_coord())
                                                           : (edge->get_line_segment().get_coord() < search_context.get_track_coord());
  }

  EnvTrackOverlap applyWidenAndClip(const EnvTrackOverlap& track_overlap, int32_t track_idx, const EnvSearchContext& search_context) const
  {
    EnvTrackOverlap widened_track_overlap = track_overlap;

    if (search_context.get_widen_func() && track_overlap.get_edge() != nullptr) {
      EnvOverlapWidenContext widen_context(std::abs(track_idx - search_context.get_base_track_idx()),
                                           track_overlap.get_end_coord() - track_overlap.get_start_coord(), *track_overlap.get_edge());

      int32_t widen_length = search_context.get_widen_func()(widen_context);
      if (widen_length < 0) {
        widen_length = 0;
      }

      widened_track_overlap.set_start_coord(widened_track_overlap.get_start_coord() - widen_length);
      widened_track_overlap.set_end_coord(widened_track_overlap.get_end_coord() + widen_length);
    }

    widened_track_overlap.set_start_coord(
        std::clamp(widened_track_overlap.get_start_coord(), search_context.get_query_start_coord(), search_context.get_query_end_coord()));
    widened_track_overlap.set_end_coord(
        std::clamp(widened_track_overlap.get_end_coord(), search_context.get_query_start_coord(), search_context.get_query_end_coord()));

    return widened_track_overlap;
  }

  EnvEdgeSet collectCandidateEdgeSet(int32_t track_idx, const std::vector<IntervalRange<int32_t>>& remaining_interval_list) const
  {
    EnvEdgeSet candidate_edge_set;
    if (!isTrackIdxValid(track_idx)) {
      return candidate_edge_set;
    }

    for (const IntervalRange<int32_t>& remaining_interval : remaining_interval_list) {
      int32_t start_bucket_idx = getBucketIdxByCoord(remaining_interval.get_start());
      int32_t end_bucket_idx = getBucketIdxByCoord(remaining_interval.get_end() - 1);
      if (start_bucket_idx > end_bucket_idx) {
        std::swap(start_bucket_idx, end_bucket_idx);
      }

      if (!isBucketIdxValid(start_bucket_idx) || !isBucketIdxValid(end_bucket_idx)) {
        continue;
      }

      for (int32_t bucket_idx = start_bucket_idx; bucket_idx <= end_bucket_idx; ++bucket_idx) {
        const EnvEdgeSet& edge_set = _track_bucket_list[track_idx][bucket_idx];
        candidate_edge_set.insert(edge_set.begin(), edge_set.end());
      }
    }

    return candidate_edge_set;
  }

  bool isEdgeOverlapRemaining(TopoEdge* edge, const std::vector<IntervalRange<int32_t>>& remaining_interval_list) const
  {
    if (edge == nullptr) {
      return false;
    }

    int32_t edge_start_coord = edge->get_line_segment().get_lower();
    int32_t edge_end_coord = edge->get_line_segment().get_upper();
    RCXUTIL.normalizeInterval(edge_start_coord, edge_end_coord);

    for (const IntervalRange<int32_t>& remaining_interval : remaining_interval_list) {
      if (RCXUTIL.isIntervalOverlap(edge_start_coord, edge_end_coord, remaining_interval.get_start(), remaining_interval.get_end())) {
        return true;
      }
    }
    return false;
  }

  std::vector<EnvTrackOverlap> getEdgeOverlapList(int32_t track_idx, TopoEdge* edge, const std::vector<IntervalRange<int32_t>>& remaining_interval_list,
                                                  const EnvSearchContext& search_context) const
  {
    std::vector<EnvTrackOverlap> edge_overlap_list;
    if (edge == nullptr) {
      return edge_overlap_list;
    }

    int32_t edge_start_coord = edge->get_line_segment().get_lower();
    int32_t edge_end_coord = edge->get_line_segment().get_upper();
    RCXUTIL.normalizeInterval(edge_start_coord, edge_end_coord);

    for (const IntervalRange<int32_t>& remaining_interval : remaining_interval_list) {
      if (!RCXUTIL.isIntervalOverlap(edge_start_coord, edge_end_coord, remaining_interval.get_start(), remaining_interval.get_end())) {
        continue;
      }

      IntervalRange<int32_t> overlap_range
          = RCXUTIL.getIntervalIntersection(edge_start_coord, edge_end_coord, remaining_interval.get_start(), remaining_interval.get_end());
      EnvTrackOverlap track_overlap;
      track_overlap.set_start_coord(overlap_range.get_start());
      track_overlap.set_end_coord(overlap_range.get_end());
      track_overlap.set_spacing(std::abs(edge->get_line_segment().get_coord() - search_context.get_track_coord()));
      track_overlap.set_edge(edge);

      if (track_overlap.get_start_coord() < track_overlap.get_end_coord()) {
        EnvTrackOverlap widened_track_overlap = applyWidenAndClip(track_overlap, track_idx, search_context);

        // widened interval should not cross the current remaining fragment
        widened_track_overlap.set_start_coord(std::max(widened_track_overlap.get_start_coord(), remaining_interval.get_start()));
        widened_track_overlap.set_end_coord(std::min(widened_track_overlap.get_end_coord(), remaining_interval.get_end()));

        if (widened_track_overlap.get_start_coord() < widened_track_overlap.get_end_coord()) {
          edge_overlap_list.push_back(widened_track_overlap);
        }
      }
    }

    return edge_overlap_list;
  }

  void searchWithinTrack(int32_t track_idx, std::vector<IntervalRange<int32_t>> remaining_interval_list, std::vector<EnvTrackOverlap>& track_overlap_list,
                         const EnvSearchContext& search_context) const
  {
    if (!isTrackIdxValid(track_idx) || remaining_interval_list.empty()) {
      return;
    }

    EnvEdgeSet candidate_edge_set = collectCandidateEdgeSet(track_idx, remaining_interval_list);
    if (candidate_edge_set.empty()) {
      return;
    }

    if (search_context.get_track_direction_step() > 0) {
      EnvEdgeSet::const_iterator edge_iter = candidate_edge_set.begin();
      while (edge_iter != candidate_edge_set.end() && (*edge_iter)->get_line_segment().get_coord() <= search_context.get_track_coord()) {
        ++edge_iter;
      }
      for (; edge_iter != candidate_edge_set.end() && !remaining_interval_list.empty(); ++edge_iter) {
        consumeEdge(track_idx, *edge_iter, remaining_interval_list, track_overlap_list, search_context);
      }
    } else {
      EnvEdgeSet::const_iterator edge_iter = candidate_edge_set.begin();
      while (edge_iter != candidate_edge_set.end() && (*edge_iter)->get_line_segment().get_coord() < search_context.get_track_coord()) {
        ++edge_iter;
      }
      for (EnvEdgeSet::const_reverse_iterator reverse_edge_iter = std::make_reverse_iterator(edge_iter);
           reverse_edge_iter != candidate_edge_set.rend() && !remaining_interval_list.empty(); ++reverse_edge_iter) {
        consumeEdge(track_idx, *reverse_edge_iter, remaining_interval_list, track_overlap_list, search_context);
      }
    }
  }

  void consumeEdge(int32_t track_idx, TopoEdge* edge, std::vector<IntervalRange<int32_t>>& remaining_interval_list,
                   std::vector<EnvTrackOverlap>& track_overlap_list, const EnvSearchContext& search_context) const
  {
    if (!isEdgeOverlapRemaining(edge, remaining_interval_list)) {
      return;
    }

    std::vector<EnvTrackOverlap> edge_overlap_list = getEdgeOverlapList(track_idx, edge, remaining_interval_list, search_context);
    if (edge_overlap_list.empty()) {
      return;
    }

    track_overlap_list.insert(track_overlap_list.end(), edge_overlap_list.begin(), edge_overlap_list.end());

    std::vector<IntervalRange<int32_t>> next_remaining_interval_list = remaining_interval_list;
    for (const EnvTrackOverlap& track_overlap : edge_overlap_list) {
      next_remaining_interval_list = RCXUTIL.subtractInterval(next_remaining_interval_list, track_overlap.get_start_coord(), track_overlap.get_end_coord());
      if (next_remaining_interval_list.empty()) {
        break;
      }
    }
    remaining_interval_list = std::move(next_remaining_interval_list);
  }

  std::vector<IntervalRange<int32_t>> searchAcrossTracks(int32_t track_idx, int32_t remaining_track_num,
                                                         std::vector<IntervalRange<int32_t>> remaining_interval_list,
                                                         std::vector<EnvTrackOverlap>& track_overlap_list, const EnvSearchContext& search_context) const
  {
    if (remaining_track_num <= 0 || remaining_interval_list.empty()) {
      return remaining_interval_list;
    }

    if (!isTrackIdxValid(track_idx)) {
      return remaining_interval_list;
    }

    std::vector<EnvTrackOverlap> local_track_overlap_list;
    searchWithinTrack(track_idx, remaining_interval_list, local_track_overlap_list, search_context);

    for (const EnvTrackOverlap& track_overlap : local_track_overlap_list) {
      if (!isEdgeInSearchDirection(track_overlap.get_edge(), search_context)) {
        continue;
      }

      track_overlap_list.push_back(track_overlap);
      remaining_interval_list = RCXUTIL.subtractInterval(remaining_interval_list, track_overlap.get_start_coord(), track_overlap.get_end_coord());
      if (remaining_interval_list.empty()) {
        return remaining_interval_list;
      }
    }

    return searchAcrossTracks(track_idx + search_context.get_track_direction_step(), remaining_track_num - 1, remaining_interval_list, track_overlap_list,
                              search_context);
  }

 private:
  std::vector<std::vector<EnvEdgeSet>> _track_bucket_list;

  int32_t _track_origin = -1;
  int32_t _track_count = -1;
  int32_t _track_step = -1;

  int32_t _bucket_origin = -1;
  int32_t _bucket_count = -1;
  int32_t _bucket_step = -1;

  bool isTrackIdxValid(int32_t track_idx) const { return 0 <= track_idx && track_idx < _track_count; }
  bool isBucketIdxValid(int32_t bucket_idx) const { return 0 <= bucket_idx && bucket_idx < _bucket_count; }
};

}  // namespace ircx
