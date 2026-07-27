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

#include "RCXHeader.hpp"

namespace ircx {

class LineSegment
{
 public:
  LineSegment() = default;
  ~LineSegment() = default;
  // getter
  bool get_is_horizontal() const { return _is_horizontal; }
  int32_t get_coord() const { return _coord; }
  int32_t get_lower() const { return _lower; }
  int32_t get_upper() const { return _upper; }
  // setter
  void set_is_horizontal(bool is_horizontal) { _is_horizontal = is_horizontal; }
  void set_coord(int32_t coord) { _coord = coord; }
  void set_lower(int32_t lower) { _lower = lower; }
  void set_upper(int32_t upper) { _upper = upper; }
  // function

 private:
  bool _is_horizontal = false;
  int32_t _coord = -1;
  int32_t _lower = -1;
  int32_t _upper = -1;
};

}  // namespace ircx
