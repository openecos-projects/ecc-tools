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

#include "FISegment.hpp"
#include "IdbEnum.h"
#include "ZHHeader.hpp"

namespace izh {

class FIRow
{
 public:
  FIRow() = default;
  ~FIRow() = default;
  // getter
  int32_t get_row_idx() const { return _row_idx; }
  int32_t get_origin_x() const { return _origin_x; }
  int32_t get_origin_y() const { return _origin_y; }
  int32_t get_site_width() const { return _site_width; }
  int32_t get_row_height() const { return _row_height; }
  int32_t get_site_count() const { return _site_count; }
  int32_t get_end_x() const { return _origin_x + _site_count * _site_width; }
  int32_t get_end_y() const { return _origin_y + _row_height; }
  idb::IdbOrient get_orient() const { return _orient; }
  std::vector<bool>& get_site_available_list() { return _site_available_list; }
  std::vector<FISegment>& get_available_segment_list() { return _available_segment_list; }
  // setter
  void set_row_idx(const int32_t row_idx) { _row_idx = row_idx; }
  void set_origin_x(const int32_t origin_x) { _origin_x = origin_x; }
  void set_origin_y(const int32_t origin_y) { _origin_y = origin_y; }
  void set_site_width(const int32_t site_width) { _site_width = site_width; }
  void set_row_height(const int32_t row_height) { _row_height = row_height; }
  void set_site_count(const int32_t site_count) { _site_count = site_count; }
  void set_orient(const idb::IdbOrient orient) { _orient = orient; }
  void set_site_available_list(const std::vector<bool>& site_available_list) { _site_available_list = site_available_list; }
  void set_available_segment_list(const std::vector<FISegment>& available_segment_list) { _available_segment_list = available_segment_list; }
  // function
  void initSiteAvailableList()
  {
    _site_available_list.assign(_site_count, true);
    _available_segment_list.clear();
  }

  void blockSiteRange(const int32_t begin_site_idx, const int32_t end_site_idx)
  {
    if (_site_available_list.empty() || end_site_idx < begin_site_idx) {
      return;
    }
    int32_t real_begin_site_idx = std::max(0, begin_site_idx);
    int32_t real_end_site_idx = std::min(_site_count - 1, end_site_idx);
    for (int32_t site_idx = real_begin_site_idx; site_idx <= real_end_site_idx; ++site_idx) {
      _site_available_list[site_idx] = false;
    }
  }

  void buildAvailableSegmentList()
  {
    _available_segment_list.clear();
    int32_t begin_site_idx = -1;
    bool last_available = false;
    for (int32_t site_idx = 0; site_idx < _site_count; ++site_idx) {
      bool curr_available = _site_available_list[site_idx];
      if (curr_available && !last_available) {
        begin_site_idx = site_idx;
      } else if (!curr_available && last_available) {
        FISegment segment;
        segment.set_begin_site_idx(begin_site_idx);
        segment.set_end_site_idx(site_idx - 1);
        _available_segment_list.push_back(segment);
      }
      last_available = curr_available;
    }
    if (last_available) {
      FISegment segment;
      segment.set_begin_site_idx(begin_site_idx);
      segment.set_end_site_idx(_site_count - 1);
      _available_segment_list.push_back(segment);
    }
  }

 private:
  int32_t _row_idx = -1;
  int32_t _origin_x = -1;
  int32_t _origin_y = -1;
  int32_t _site_width = -1;
  int32_t _row_height = -1;
  int32_t _site_count = -1;
  idb::IdbOrient _orient = idb::IdbOrient::kNone;
  std::vector<bool> _site_available_list;
  std::vector<FISegment> _available_segment_list;
};

}  // namespace izh
