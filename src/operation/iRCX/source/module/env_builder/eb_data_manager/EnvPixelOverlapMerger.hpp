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

#include "CrossLayerOverlap.hpp"
#include "EnvLayerPixelOverlapList.hpp"
#include "EnvPixelGrid.hpp"
#include "RCXHeader.hpp"
#include "Utility.hpp"

namespace ircx {

class EnvPixelOverlapMerger
{
 public:
  void merge(int32_t start_coord, int32_t end_coord, const std::vector<EnvLayerPixelOverlapList>& lower_layer_pixel_overlap_list,
             const std::vector<EnvLayerPixelOverlapList>& upper_layer_pixel_overlap_list, std::vector<CrossLayerOverlap>& cross_layer_overlap_list) const
  {
    if (start_coord > end_coord) {
      std::swap(start_coord, end_coord);
    }

    cross_layer_overlap_list.clear();
    if (!(start_coord < end_coord)) {
      return;
    }

    std::vector<EnvLayerPixelOverlapList> normalized_lower_layer_pixel_overlap_list;
    std::vector<EnvLayerPixelOverlapList> normalized_upper_layer_pixel_overlap_list;
    normalized_lower_layer_pixel_overlap_list.reserve(lower_layer_pixel_overlap_list.size());
    normalized_upper_layer_pixel_overlap_list.reserve(upper_layer_pixel_overlap_list.size());

    for (const EnvLayerPixelOverlapList& layer_pixel_overlap_list : lower_layer_pixel_overlap_list) {
      if (layer_pixel_overlap_list.get_layer_idx() == 0 || layer_pixel_overlap_list.get_layer_idx() == -1) {
        continue;
      }
      EnvLayerPixelOverlapList normalized_layer_pixel_overlap_list;
      normalized_layer_pixel_overlap_list.set_layer_idx(layer_pixel_overlap_list.get_layer_idx());
      normalizePixelOverlapList(start_coord, end_coord, layer_pixel_overlap_list.get_pixel_overlap_list(),
                                normalized_layer_pixel_overlap_list.get_pixel_overlap_list());
      if (!normalized_layer_pixel_overlap_list.get_pixel_overlap_list().empty()) {
        normalized_lower_layer_pixel_overlap_list.push_back(std::move(normalized_layer_pixel_overlap_list));
      }
    }

    for (const EnvLayerPixelOverlapList& layer_pixel_overlap_list : upper_layer_pixel_overlap_list) {
      if (layer_pixel_overlap_list.get_layer_idx() == 0 || layer_pixel_overlap_list.get_layer_idx() == -1) {
        continue;
      }
      EnvLayerPixelOverlapList normalized_layer_pixel_overlap_list;
      normalized_layer_pixel_overlap_list.set_layer_idx(layer_pixel_overlap_list.get_layer_idx());
      normalizePixelOverlapList(start_coord, end_coord, layer_pixel_overlap_list.get_pixel_overlap_list(),
                                normalized_layer_pixel_overlap_list.get_pixel_overlap_list());
      if (!normalized_layer_pixel_overlap_list.get_pixel_overlap_list().empty()) {
        normalized_upper_layer_pixel_overlap_list.push_back(std::move(normalized_layer_pixel_overlap_list));
      }
    }

    std::vector<int32_t> breakpoint_coord_list;
    breakpoint_coord_list.reserve(2 + countBreakpointNum(normalized_lower_layer_pixel_overlap_list)
                                  + countBreakpointNum(normalized_upper_layer_pixel_overlap_list));
    breakpoint_coord_list.push_back(start_coord);
    breakpoint_coord_list.push_back(end_coord);

    addBreakpointCoordList(normalized_lower_layer_pixel_overlap_list, breakpoint_coord_list);
    addBreakpointCoordList(normalized_upper_layer_pixel_overlap_list, breakpoint_coord_list);

    RCXUTIL.sortAndUnique(breakpoint_coord_list);

    if (breakpoint_coord_list.size() < 2) {
      return;
    }

    std::vector<int32_t> lower_pixel_overlap_idx_list(normalized_lower_layer_pixel_overlap_list.size(), 0);
    std::vector<int32_t> upper_pixel_overlap_idx_list(normalized_upper_layer_pixel_overlap_list.size(), 0);

    for (int32_t interval_idx = 0; interval_idx + 1 < static_cast<int32_t>(breakpoint_coord_list.size()); ++interval_idx) {
      int32_t interval_start_coord = breakpoint_coord_list[interval_idx];
      int32_t interval_end_coord = breakpoint_coord_list[interval_idx + 1];
      if (!(interval_start_coord < interval_end_coord)) {
        continue;
      }

      int32_t lower_layer_idx
          = getFirstCoveringLayerIdx(normalized_lower_layer_pixel_overlap_list, lower_pixel_overlap_idx_list, interval_start_coord, interval_end_coord);
      int32_t upper_layer_idx
          = getFirstCoveringLayerIdx(normalized_upper_layer_pixel_overlap_list, upper_pixel_overlap_idx_list, interval_start_coord, interval_end_coord);

      appendCrossLayerOverlap(interval_start_coord, interval_end_coord, lower_layer_idx, upper_layer_idx, cross_layer_overlap_list);
    }
  }

 private:
  int32_t countBreakpointNum(const std::vector<EnvLayerPixelOverlapList>& layer_pixel_overlap_list) const
  {
    int32_t breakpoint_num = 0;
    for (const EnvLayerPixelOverlapList& layer_pixel_overlap : layer_pixel_overlap_list) {
      breakpoint_num += static_cast<int32_t>(layer_pixel_overlap.get_pixel_overlap_list().size()) * 2;
    }
    return breakpoint_num;
  }

  void addBreakpointCoordList(const std::vector<EnvLayerPixelOverlapList>& layer_pixel_overlap_list, std::vector<int32_t>& breakpoint_coord_list) const
  {
    for (const EnvLayerPixelOverlapList& layer_pixel_overlap : layer_pixel_overlap_list) {
      for (const EnvPixelOverlap& pixel_overlap : layer_pixel_overlap.get_pixel_overlap_list()) {
        breakpoint_coord_list.push_back(pixel_overlap.get_start_coord());
        breakpoint_coord_list.push_back(pixel_overlap.get_end_coord());
      }
    }
  }

  void normalizePixelOverlapList(int32_t start_coord, int32_t end_coord, const std::vector<EnvPixelOverlap>& input_pixel_overlap_list,
                                 std::vector<EnvPixelOverlap>& normalized_pixel_overlap_list) const
  {
    normalized_pixel_overlap_list.clear();
    normalized_pixel_overlap_list.reserve(input_pixel_overlap_list.size());

    for (const EnvPixelOverlap& pixel_overlap : input_pixel_overlap_list) {
      int32_t clipped_start_coord = std::max(start_coord, pixel_overlap.get_start_coord());
      int32_t clipped_end_coord = std::min(end_coord, pixel_overlap.get_end_coord());
      if (clipped_start_coord < clipped_end_coord) {
        normalized_pixel_overlap_list.emplace_back(clipped_start_coord, clipped_end_coord);
      }
    }

    std::sort(normalized_pixel_overlap_list.begin(), normalized_pixel_overlap_list.end(),
              [this](const EnvPixelOverlap& lhs, const EnvPixelOverlap& rhs) { return isPixelOverlapLess(lhs, rhs); });

    std::vector<EnvPixelOverlap> merged_pixel_overlap_list;
    merged_pixel_overlap_list.reserve(normalized_pixel_overlap_list.size());

    for (const EnvPixelOverlap& pixel_overlap : normalized_pixel_overlap_list) {
      if (merged_pixel_overlap_list.empty() || merged_pixel_overlap_list.back().get_end_coord() < pixel_overlap.get_start_coord()) {
        merged_pixel_overlap_list.push_back(pixel_overlap);
      } else {
        merged_pixel_overlap_list.back().set_end_coord(std::max(merged_pixel_overlap_list.back().get_end_coord(), pixel_overlap.get_end_coord()));
      }
    }

    normalized_pixel_overlap_list.swap(merged_pixel_overlap_list);
  }

  bool isPixelOverlapLess(const EnvPixelOverlap& lhs, const EnvPixelOverlap& rhs) const
  {
    if (lhs.get_start_coord() != rhs.get_start_coord()) {
      return lhs.get_start_coord() < rhs.get_start_coord();
    }
    return lhs.get_end_coord() < rhs.get_end_coord();
  }

  void advancePixelOverlapIdx(const std::vector<EnvPixelOverlap>& pixel_overlap_list, int32_t& pixel_overlap_idx, int32_t coord) const
  {
    while (pixel_overlap_idx < static_cast<int32_t>(pixel_overlap_list.size()) && pixel_overlap_list[pixel_overlap_idx].get_end_coord() <= coord) {
      ++pixel_overlap_idx;
    }
  }

  bool isCovered(const std::vector<EnvPixelOverlap>& pixel_overlap_list, int32_t pixel_overlap_idx, int32_t start_coord, int32_t end_coord) const
  {
    return pixel_overlap_idx < static_cast<int32_t>(pixel_overlap_list.size()) && pixel_overlap_list[pixel_overlap_idx].get_start_coord() < end_coord
           && pixel_overlap_list[pixel_overlap_idx].get_end_coord() > start_coord;
  }

  int32_t getFirstCoveringLayerIdx(const std::vector<EnvLayerPixelOverlapList>& layer_pixel_overlap_list, std::vector<int32_t>& pixel_overlap_idx_list,
                                   int32_t start_coord, int32_t end_coord) const
  {
    for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(layer_pixel_overlap_list.size()); ++layer_idx) {
      advancePixelOverlapIdx(layer_pixel_overlap_list[layer_idx].get_pixel_overlap_list(), pixel_overlap_idx_list[layer_idx], start_coord);
      if (isCovered(layer_pixel_overlap_list[layer_idx].get_pixel_overlap_list(), pixel_overlap_idx_list[layer_idx], start_coord, end_coord)) {
        return layer_pixel_overlap_list[layer_idx].get_layer_idx();
      }
    }
    return 0;
  }

  void appendCrossLayerOverlap(int32_t start_coord, int32_t end_coord, int32_t lower_layer_idx, int32_t upper_layer_idx,
                               std::vector<CrossLayerOverlap>& cross_layer_overlap_list) const
  {
    if (!(start_coord < end_coord)) {
      return;
    }

    if (lower_layer_idx == 0 && upper_layer_idx == 0) {
      return;
    }

    if (!cross_layer_overlap_list.empty() && cross_layer_overlap_list.back().get_end_coord() == start_coord
        && cross_layer_overlap_list.back().get_below_layer_idx() == lower_layer_idx
        && cross_layer_overlap_list.back().get_above_layer_idx() == upper_layer_idx) {
      cross_layer_overlap_list.back().set_end_coord(end_coord);
      return;
    }

    CrossLayerOverlap cross_layer_overlap;
    cross_layer_overlap.set_start_coord(start_coord);
    cross_layer_overlap.set_end_coord(end_coord);
    cross_layer_overlap.set_below_layer_idx(lower_layer_idx);
    cross_layer_overlap.set_above_layer_idx(upper_layer_idx);
    cross_layer_overlap_list.push_back(cross_layer_overlap);
  }
};

}  // namespace ircx
