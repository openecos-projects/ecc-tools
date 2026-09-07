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

#include "EnvPixelOverlap.hpp"
#include "LineSegment.hpp"
#include "RCXHeader.hpp"
#include "TopoEdge.hpp"
#include "Utility.hpp"

namespace ircx {

class EnvPixelGrid
{
 public:
  EnvPixelGrid() = default;
  ~EnvPixelGrid() = default;

  // getter
  int32_t get_x_origin() const { return _x_origin; }
  int32_t get_y_origin() const { return _y_origin; }
  int32_t get_x_count() const { return _x_count; }
  int32_t get_y_count() const { return _y_count; }
  int32_t get_x_step() const { return _x_step; }
  int32_t get_y_step() const { return _y_step; }

  // setter
  void set_x_origin(int32_t x_origin) { _x_origin = x_origin; }
  void set_y_origin(int32_t y_origin) { _y_origin = y_origin; }
  void set_x_count(int32_t x_count) { _x_count = x_count; }
  void set_y_count(int32_t y_count) { _y_count = y_count; }
  void set_x_step(int32_t x_step) { _x_step = x_step; }
  void set_y_step(int32_t y_step) { _y_step = y_step; }

  // coord mapping
  int32_t getXIdxByCoord(int32_t coord) const { return (coord - _x_origin) / _x_step; }
  int32_t getYIdxByCoord(int32_t coord) const { return (coord - _y_origin) / _y_step; }

  int32_t getXCoordByIdx(int32_t idx) const { return _x_origin + idx * _x_step; }
  int32_t getYCoordByIdx(int32_t idx) const { return _y_origin + idx * _y_step; }

  bool initPixelMap()
  {
    if (_x_count <= 0 || _y_count <= 0 || _x_step <= 0 || _y_step <= 0) {
      return false;
    }

    _pixel_is_conductor_map.assign(_x_count, std::vector<bool>(_y_count, false));
    return true;
  }

  void addTopoEdge(TopoEdge& edge)
  {
    if (_pixel_is_conductor_map.empty() || _pixel_is_conductor_map.front().empty()) {
      if (!initPixelMap()) {
        return;
      }
    }

    const GTLRectInt& edge_shape = edge.get_shape();

    int32_t x_start_coord = RCXUTIL.minX(edge_shape);
    int32_t y_start_coord = RCXUTIL.minY(edge_shape);
    int32_t x_end_coord = RCXUTIL.maxX(edge_shape);
    int32_t y_end_coord = RCXUTIL.maxY(edge_shape);

    if (x_start_coord >= x_end_coord || y_start_coord >= y_end_coord) {
      return;
    }

    int32_t x_start_idx = getXIdxByCoord(x_start_coord);
    int32_t y_start_idx = getYIdxByCoord(y_start_coord);
    int32_t x_end_idx = getXIdxByCoord(x_end_coord);
    int32_t y_end_idx = getYIdxByCoord(y_end_coord);

    if (!isXIdxValid(x_start_idx) || !isYIdxValid(y_start_idx) || !isXIdxValid(x_end_idx) || !isYIdxValid(y_end_idx)) {
      return;
    }

    for (int32_t x_idx = x_start_idx; x_idx <= x_end_idx; ++x_idx) {
      for (int32_t y_idx = y_start_idx; y_idx <= y_end_idx; ++y_idx) {
        _pixel_is_conductor_map[x_idx][y_idx] = true;
      }
    }
  }

  std::vector<EnvPixelOverlap> getOverlapList(const LineSegment& line_segment) const
  {
    std::vector<EnvPixelOverlap> pixel_overlap_list;
    if (_pixel_is_conductor_map.empty() || _pixel_is_conductor_map.front().empty()) {
      return pixel_overlap_list;
    }

    bool is_horizontal = line_segment.get_is_horizontal();
    int32_t fixed_coord = line_segment.get_coord();
    int32_t start_coord = line_segment.get_lower();
    int32_t end_coord = line_segment.get_upper();

    RCXUTIL.normalizeInterval(start_coord, end_coord);

    if (is_horizontal) {
      int32_t fixed_y_idx = getYIdxByCoord(fixed_coord);
      if (!isYIdxValid(fixed_y_idx)) {
        return pixel_overlap_list;
      }
    }

    if (!is_horizontal) {
      int32_t fixed_x_idx = getXIdxByCoord(fixed_coord);
      if (!isXIdxValid(fixed_x_idx)) {
        return pixel_overlap_list;
      }
      return collectConductorRuns(start_coord, end_coord, fixed_x_idx, false);
    }

    return collectConductorRuns(start_coord, end_coord, getYIdxByCoord(fixed_coord), true);
  }

 private:
  std::vector<EnvPixelOverlap> collectConductorRuns(int32_t start_coord, int32_t end_coord, int32_t fixed_idx, bool is_horizontal) const
  {
    std::vector<EnvPixelOverlap> pixel_overlap_list;
    if (start_coord >= end_coord) {
      return pixel_overlap_list;
    }

    int32_t start_idx = getAxisIdxByCoord(start_coord, is_horizontal);
    int32_t end_idx = getAxisIdxByCoord(end_coord, is_horizontal) + 1;
    if (start_idx > end_idx) {
      return pixel_overlap_list;
    }

    bool is_conductor = isConductor(start_idx, fixed_idx, is_horizontal);
    int32_t run_start_idx = start_idx;

    for (int32_t idx = start_idx + 1; idx <= end_idx; ++idx) {
      bool is_current_conductor = isConductor(idx, fixed_idx, is_horizontal);
      if (is_current_conductor != is_conductor) {
        if (is_conductor) {
          appendConductorRun(pixel_overlap_list, start_coord, end_coord, is_horizontal, run_start_idx, idx);
        }

        run_start_idx = idx;
        is_conductor = is_current_conductor;
      }
    }

    if (is_conductor) {
      appendConductorRun(pixel_overlap_list, start_coord, end_coord, is_horizontal, run_start_idx, end_idx + 1);
    }

    return pixel_overlap_list;
  }

  void appendConductorRun(std::vector<EnvPixelOverlap>& pixel_overlap_list, int32_t start_coord, int32_t end_coord, bool is_horizontal, int32_t start_idx,
                          int32_t end_idx_exclusive) const
  {
    if (end_idx_exclusive <= start_idx) {
      return;
    }

    int32_t overlap_start_coord = RCXUTIL.getIntervalMidpoint(getAxisCoordByIdx(start_idx, is_horizontal), getAxisCoordByIdx(start_idx + 1, is_horizontal));
    int32_t overlap_end_coord
        = RCXUTIL.getIntervalMidpoint(getAxisCoordByIdx(end_idx_exclusive - 1, is_horizontal), getAxisCoordByIdx(end_idx_exclusive, is_horizontal));
    EnvPixelOverlap pixel_overlap = clipPixelOverlap(overlap_start_coord, overlap_end_coord, start_coord, end_coord);
    if (!pixel_overlap.empty()) {
      pixel_overlap_list.push_back(pixel_overlap);
    }
  }

  EnvPixelOverlap clipPixelOverlap(int32_t overlap_start_coord, int32_t overlap_end_coord, int32_t start_coord, int32_t end_coord) const
  {
    EnvPixelOverlap pixel_overlap;
    pixel_overlap.set_start_coord(std::max(overlap_start_coord, start_coord));
    pixel_overlap.set_end_coord(std::min(overlap_end_coord, end_coord));
    return pixel_overlap;
  }

  bool isConductor(int32_t axis_idx, int32_t fixed_idx, bool is_horizontal) const
  {
    if (!isAxisIdxValid(axis_idx, is_horizontal)) {
      return false;
    }
    return is_horizontal ? _pixel_is_conductor_map[axis_idx][fixed_idx] : _pixel_is_conductor_map[fixed_idx][axis_idx];
  }

  int32_t getAxisIdxByCoord(int32_t coord, bool is_horizontal) const { return is_horizontal ? getXIdxByCoord(coord) : getYIdxByCoord(coord); }
  int32_t getAxisCoordByIdx(int32_t idx, bool is_horizontal) const { return is_horizontal ? getXCoordByIdx(idx) : getYCoordByIdx(idx); }
  bool isAxisIdxValid(int32_t axis_idx, bool is_horizontal) const { return is_horizontal ? isXIdxValid(axis_idx) : isYIdxValid(axis_idx); }

  bool isXIdxValid(int32_t x_idx) const { return 0 <= x_idx && x_idx < _x_count; }
  bool isYIdxValid(int32_t y_idx) const { return 0 <= y_idx && y_idx < _y_count; }

 private:
  std::vector<std::vector<bool>> _pixel_is_conductor_map;

  int32_t _x_origin = -1;
  int32_t _y_origin = -1;
  int32_t _x_count = -1;
  int32_t _y_count = -1;
  int32_t _x_step = -1;
  int32_t _y_step = -1;
};

}  // namespace ircx
