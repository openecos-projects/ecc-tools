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

template <typename T>
class RectRelation
{
 public:
  RectRelation() = default;
  ~RectRelation() = default;
  // getter
  T get_overlap_x() const { return _overlap_x; }
  T get_overlap_y() const { return _overlap_y; }
  T get_gap_x() const { return _gap_x; }
  T get_gap_y() const { return _gap_y; }
  // setter
  void set_overlap_x(T overlap_x) { _overlap_x = overlap_x; }
  void set_overlap_y(T overlap_y) { _overlap_y = overlap_y; }
  void set_gap_x(T gap_x) { _gap_x = gap_x; }
  void set_gap_y(T gap_y) { _gap_y = gap_y; }
  // function

 private:
  T _overlap_x = 0;
  T _overlap_y = 0;
  T _gap_x = 0;
  T _gap_y = 0;
};

}  // namespace ircx
