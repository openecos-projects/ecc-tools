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

#include "MJHeader.hpp"

namespace imj {

class DCRect
{
 public:
  DCRect() = default;
  DCRect(int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y) : _ll_x(ll_x), _ll_y(ll_y), _ur_x(ur_x), _ur_y(ur_y) {}
  ~DCRect() = default;
  // getter
  int32_t get_ll_x() const { return _ll_x; }
  int32_t get_ll_y() const { return _ll_y; }
  int32_t get_ur_x() const { return _ur_x; }
  int32_t get_ur_y() const { return _ur_y; }
  // setter
  void set_ll_x(int32_t value) { _ll_x = value; }
  void set_ll_y(int32_t value) { _ll_y = value; }
  void set_ur_x(int32_t value) { _ur_x = value; }
  void set_ur_y(int32_t value) { _ur_y = value; }
  // function
  bool isValid() const { return _ll_x < _ur_x && _ll_y < _ur_y; }
  bool contains(const DCRect& rect) const { return rect.isValid() && _ll_x <= rect._ll_x && _ll_y <= rect._ll_y && _ur_x >= rect._ur_x && _ur_y >= rect._ur_y; }
  DCRect intersect(const DCRect& rect) const
  {
    return DCRect(std::max(_ll_x, rect._ll_x), std::max(_ll_y, rect._ll_y), std::min(_ur_x, rect._ur_x), std::min(_ur_y, rect._ur_y));
  }

 private:
  int32_t _ll_x = 0;
  int32_t _ll_y = 0;
  int32_t _ur_x = 0;
  int32_t _ur_y = 0;
};

}  // namespace imj
