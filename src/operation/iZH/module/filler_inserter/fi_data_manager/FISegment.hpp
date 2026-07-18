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

#include "ZHHeader.hpp"

namespace izh {

class FISegment
{
 public:
  FISegment() = default;
  ~FISegment() = default;
  // getter
  int32_t get_begin_site_idx() const { return _begin_site_idx; }
  int32_t get_end_site_idx() const { return _end_site_idx; }
  int32_t get_site_count() const { return _end_site_idx >= _begin_site_idx ? _end_site_idx - _begin_site_idx + 1 : 0; }
  // setter
  void set_begin_site_idx(const int32_t begin_site_idx) { _begin_site_idx = begin_site_idx; }
  void set_end_site_idx(const int32_t end_site_idx) { _end_site_idx = end_site_idx; }

 private:
  int32_t _begin_site_idx = -1;
  int32_t _end_site_idx = -1;
};

}  // namespace izh
