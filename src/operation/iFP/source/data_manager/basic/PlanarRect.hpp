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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PlanarCoord.hpp"

namespace ifp {

class PlanarRect
{
 public:
  PlanarRect() = default;
  PlanarRect(const PlanarCoord& ll, const PlanarCoord& ur)
  {
    _ll = ll;
    _ur = ur;
  }
  PlanarRect(int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
  {
    set_ll(ll_x, ll_y);
    set_ur(ur_x, ur_y);
  }
  ~PlanarRect() = default;
  bool operator==(const PlanarRect& other) const { return (_ll == other._ll && _ur == other._ur); }
  bool operator!=(const PlanarRect& other) const { return !((*this) == other); }
  // getter
  PlanarCoord& get_ll() { return _ll; }
  PlanarCoord& get_ur() { return _ur; }
  int32_t get_ll_x() const { return _ll.get_x(); }
  int32_t get_ll_y() const { return _ll.get_y(); }
  int32_t get_ur_x() const { return _ur.get_x(); }
  int32_t get_ur_y() const { return _ur.get_y(); }
  int32_t get_width() const { return get_ur_x() - get_ll_x(); }
  int32_t get_height() const { return get_ur_y() - get_ll_y(); }

  // const getter
  const PlanarCoord& get_ll() const { return _ll; }
  const PlanarCoord& get_ur() const { return _ur; }

  // setter
  void set_ll(const PlanarCoord& ll) { _ll = ll; }
  void set_ur(const PlanarCoord& ur) { _ur = ur; }
  void set_ll_x(int32_t ll_x) { _ll.set_x(ll_x); }
  void set_ll_y(int32_t ll_y) { _ll.set_y(ll_y); }
  void set_ur_x(int32_t ur_x) { _ur.set_x(ur_x); }
  void set_ur_y(int32_t ur_y) { _ur.set_y(ur_y); }
  void set_ll(int32_t ll_x, int32_t ll_y) { _ll.set_coord(ll_x, ll_y); }
  void set_ur(int32_t ur_x, int32_t ur_y) { _ur.set_coord(ur_x, ur_y); }
  void set_rect(int32_t ll_x, int32_t ll_y, int32_t ur_x, int32_t ur_y)
  {
    set_ll(ll_x, ll_y);
    set_ur(ur_x, ur_y);
  }
  // function

 private:
  PlanarCoord _ll;
  PlanarCoord _ur;
};

}  // namespace ifp
