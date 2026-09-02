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

#include "EdgeEnvInterval.hpp"
#include "EnvTrackIdx.hpp"
#include "EnvTrackOverlap.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class EnvTrackOverlapMerger
{
 public:
  void merge(int32_t start_coord, int32_t end_coord, const std::vector<EnvTrackOverlap>& lower_track_overlap_list,
             const std::vector<EnvTrackOverlap>& upper_track_overlap_list, std::vector<EdgeEnvInterval>& edge_env_interval_list) const
  {
    if (start_coord > end_coord) {
      std::swap(start_coord, end_coord);
    }

    edge_env_interval_list.clear();
    if (!(start_coord < end_coord)) {
      return;
    }

    std::vector<EnvTrackOverlap> normalized_lower_track_overlap_list;
    std::vector<EnvTrackOverlap> normalized_upper_track_overlap_list;

    normalizeTrackOverlapList(start_coord, end_coord, lower_track_overlap_list, normalized_lower_track_overlap_list);
    normalizeTrackOverlapList(start_coord, end_coord, upper_track_overlap_list, normalized_upper_track_overlap_list);

    mergeTrackOverlapList(normalized_lower_track_overlap_list, normalized_upper_track_overlap_list, edge_env_interval_list);
  }

 private:
  EnvTrackOverlap makeEmptyTrackOverlap(int32_t start_coord, int32_t end_coord) const { return EnvTrackOverlap(start_coord, end_coord, INT32_MAX, nullptr); }

  void appendTrackOverlap(std::vector<EnvTrackOverlap>& track_overlap_list, const EnvTrackOverlap& track_overlap) const
  {
    if (!(track_overlap.get_start_coord() < track_overlap.get_end_coord())) {
      return;
    }

    if (!track_overlap_list.empty() && track_overlap_list.back().get_end_coord() == track_overlap.get_start_coord()
        && track_overlap_list.back().get_edge() == track_overlap.get_edge() && track_overlap_list.back().get_spacing() == track_overlap.get_spacing()) {
      track_overlap_list.back().set_end_coord(track_overlap.get_end_coord());
      return;
    }

    track_overlap_list.push_back(track_overlap);
  }

  void normalizeTrackOverlapList(int32_t start_coord, int32_t end_coord, const std::vector<EnvTrackOverlap>& input_track_overlap_list,
                                 std::vector<EnvTrackOverlap>& normalized_track_overlap_list) const
  {
    normalized_track_overlap_list.clear();

    std::vector<EnvTrackOverlap> clipped_track_overlap_list;
    clipped_track_overlap_list.reserve(input_track_overlap_list.size());

    for (const EnvTrackOverlap& track_overlap : input_track_overlap_list) {
      int32_t clipped_start_coord = std::max(start_coord, track_overlap.get_start_coord());
      int32_t clipped_end_coord = std::min(end_coord, track_overlap.get_end_coord());
      if (!(clipped_start_coord < clipped_end_coord)) {
        continue;
      }

      EnvTrackOverlap clipped_track_overlap = track_overlap;
      clipped_track_overlap.set_start_coord(clipped_start_coord);
      clipped_track_overlap.set_end_coord(clipped_end_coord);
      clipped_track_overlap_list.push_back(clipped_track_overlap);
    }

    std::sort(clipped_track_overlap_list.begin(), clipped_track_overlap_list.end(),
              [this](const EnvTrackOverlap& lhs, const EnvTrackOverlap& rhs) { return isTrackOverlapLess(lhs, rhs); });

    int32_t cursor_coord = start_coord;
    for (const EnvTrackOverlap& track_overlap : clipped_track_overlap_list) {
      if (cursor_coord < track_overlap.get_start_coord()) {
        appendTrackOverlap(normalized_track_overlap_list, makeEmptyTrackOverlap(cursor_coord, track_overlap.get_start_coord()));
      }

      appendTrackOverlap(normalized_track_overlap_list, track_overlap);
      cursor_coord = track_overlap.get_end_coord();
    }

    if (cursor_coord < end_coord) {
      appendTrackOverlap(normalized_track_overlap_list, makeEmptyTrackOverlap(cursor_coord, end_coord));
    }

    if (normalized_track_overlap_list.empty()) {
      normalized_track_overlap_list.push_back(makeEmptyTrackOverlap(start_coord, end_coord));
    }
  }

  bool isTrackOverlapLess(const EnvTrackOverlap& lhs, const EnvTrackOverlap& rhs) const
  {
    if (lhs.get_start_coord() != rhs.get_start_coord()) {
      return lhs.get_start_coord() < rhs.get_start_coord();
    }
    if (lhs.get_end_coord() != rhs.get_end_coord()) {
      return lhs.get_end_coord() < rhs.get_end_coord();
    }
    if (lhs.get_edge() != rhs.get_edge()) {
      if (lhs.get_edge() == nullptr || rhs.get_edge() == nullptr) {
        return lhs.get_edge() == nullptr;
      }
      if (lhs.get_edge()->get_is_special_net() != rhs.get_edge()->get_is_special_net()) {
        return lhs.get_edge()->get_is_special_net() < rhs.get_edge()->get_is_special_net();
      }
      if (lhs.get_edge()->get_net_idx() != rhs.get_edge()->get_net_idx()) {
        return lhs.get_edge()->get_net_idx() < rhs.get_edge()->get_net_idx();
      }
      return lhs.get_edge()->get_edge_idx() < rhs.get_edge()->get_edge_idx();
    }
    return lhs.get_spacing() < rhs.get_spacing();
  }

  void appendEdgeEnvInterval(std::vector<EdgeEnvInterval>& edge_env_interval_list, int32_t start_coord, int32_t end_coord,
                             const EnvTrackOverlap& lower_track_overlap, const EnvTrackOverlap& upper_track_overlap) const
  {
    if (!(start_coord < end_coord)) {
      return;
    }

    int32_t lower_spacing = lower_track_overlap.get_spacing();
    int32_t upper_spacing = upper_track_overlap.get_spacing();
    TopoEdge* lower_adjacent_edge = lower_track_overlap.get_edge();
    TopoEdge* upper_adjacent_edge = upper_track_overlap.get_edge();

    if (!edge_env_interval_list.empty() && edge_env_interval_list.back().get_end_coord() == start_coord
        && edge_env_interval_list.back().get_lower_adjacent_edge() == lower_adjacent_edge
        && edge_env_interval_list.back().get_upper_adjacent_edge() == upper_adjacent_edge && edge_env_interval_list.back().get_lower_spacing() == lower_spacing
        && edge_env_interval_list.back().get_upper_spacing() == upper_spacing) {
      edge_env_interval_list.back().set_end_coord(end_coord);
      return;
    }

    EdgeEnvInterval edge_env_interval;
    edge_env_interval.set_start_coord(start_coord);
    edge_env_interval.set_end_coord(end_coord);
    edge_env_interval.set_lower_adjacent_edge(lower_adjacent_edge);
    edge_env_interval.set_upper_adjacent_edge(upper_adjacent_edge);
    edge_env_interval.set_lower_spacing(lower_spacing);
    edge_env_interval.set_upper_spacing(upper_spacing);
    edge_env_interval_list.push_back(edge_env_interval);
  }

  void mergeTrackOverlapList(const std::vector<EnvTrackOverlap>& lower_track_overlap_list, const std::vector<EnvTrackOverlap>& upper_track_overlap_list,
                             std::vector<EdgeEnvInterval>& edge_env_interval_list) const
  {
    edge_env_interval_list.clear();
    if (lower_track_overlap_list.empty() || upper_track_overlap_list.empty()) {
      return;
    }

    int32_t lower_overlap_idx = 0;
    int32_t upper_overlap_idx = 0;

    while (lower_overlap_idx < static_cast<int32_t>(lower_track_overlap_list.size())
           && upper_overlap_idx < static_cast<int32_t>(upper_track_overlap_list.size())) {
      int32_t overlap_start_coord
          = std::max(lower_track_overlap_list[lower_overlap_idx].get_start_coord(), upper_track_overlap_list[upper_overlap_idx].get_start_coord());
      int32_t overlap_end_coord
          = std::min(lower_track_overlap_list[lower_overlap_idx].get_end_coord(), upper_track_overlap_list[upper_overlap_idx].get_end_coord());

      if (overlap_start_coord < overlap_end_coord) {
        appendEdgeEnvInterval(edge_env_interval_list, overlap_start_coord, overlap_end_coord, lower_track_overlap_list[lower_overlap_idx],
                              upper_track_overlap_list[upper_overlap_idx]);
      }

      if (lower_track_overlap_list[lower_overlap_idx].get_end_coord() == overlap_end_coord) {
        ++lower_overlap_idx;
      }
      if (upper_track_overlap_list[upper_overlap_idx].get_end_coord() == overlap_end_coord) {
        ++upper_overlap_idx;
      }
    }
  }
};

}  // namespace ircx
