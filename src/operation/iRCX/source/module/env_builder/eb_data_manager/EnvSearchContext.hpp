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

#include "EnvOverlapWidenContext.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class EnvSearchContext
{
 public:
  EnvSearchContext(int32_t track_coord, int32_t base_track_idx, int32_t query_start_coord, int32_t query_end_coord,
                   int32_t track_direction_step, const std::function<int32_t(const EnvOverlapWidenContext&)>& widen_func)
      : _track_coord(track_coord),
        _base_track_idx(base_track_idx),
        _query_start_coord(query_start_coord),
        _query_end_coord(query_end_coord),
        _track_direction_step(track_direction_step),
        _widen_func(widen_func)
  {
  }
  ~EnvSearchContext() = default;
  // getter
  int32_t get_track_coord() const { return _track_coord; }
  int32_t get_base_track_idx() const { return _base_track_idx; }
  int32_t get_query_start_coord() const { return _query_start_coord; }
  int32_t get_query_end_coord() const { return _query_end_coord; }
  int32_t get_track_direction_step() const { return _track_direction_step; }
  const std::function<int32_t(const EnvOverlapWidenContext&)>& get_widen_func() const { return _widen_func; }
  // setter
  // function

 private:
  int32_t _track_coord = -1;
  int32_t _base_track_idx = -1;
  int32_t _query_start_coord = -1;
  int32_t _query_end_coord = -1;
  int32_t _track_direction_step = -1;
  const std::function<int32_t(const EnvOverlapWidenContext&)>& _widen_func;
};

}  // namespace ircx
