// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "IdbEnum.h"
#include "IdbGeometry.h"
#include "IdbOrientTransform.h"
#include "MJHeader.hpp"

namespace imj {

class DFTransform
{
 public:
  DFTransform() { set_identity(); }
  ~DFTransform() = default;
  // getter
  int32_t get_xx() const { return _xx; }
  int32_t get_xy() const { return _xy; }
  int32_t get_yx() const { return _yx; }
  int32_t get_yy() const { return _yy; }
  int32_t get_x_offset() const { return _x_offset; }
  int32_t get_y_offset() const { return _y_offset; }
  // setter
  void set_xx(int32_t xx) { _xx = xx; }
  void set_xy(int32_t xy) { _xy = xy; }
  void set_yx(int32_t yx) { _yx = yx; }
  void set_yy(int32_t yy) { _yy = yy; }
  void set_x_offset(int32_t x_offset) { _x_offset = x_offset; }
  void set_y_offset(int32_t y_offset) { _y_offset = y_offset; }
  // function
  void set_identity()
  {
    _xx = 1;
    _xy = 0;
    _yx = 0;
    _yy = 1;
    _x_offset = 0;
    _y_offset = 0;
  }
  void set_instance_transform(idb::IdbOrient orient, int32_t origin_x, int32_t origin_y, int32_t width, int32_t height,
                              int32_t child_die_ll_x, int32_t child_die_ll_y)
  {
    idb::IdbCoordinate<int32_t> origin(origin_x, origin_y);
    idb::IdbOrientTransform orient_transform(orient, &origin, width, height);
    idb::IdbCoordinate<int32_t> point_0(origin_x - child_die_ll_x, origin_y - child_die_ll_y);
    idb::IdbCoordinate<int32_t> point_x(origin_x - child_die_ll_x + 1, origin_y - child_die_ll_y);
    idb::IdbCoordinate<int32_t> point_y(origin_x - child_die_ll_x, origin_y - child_die_ll_y + 1);
    orient_transform.transformCoordinate(&point_0);
    orient_transform.transformCoordinate(&point_x);
    orient_transform.transformCoordinate(&point_y);
    _xx = point_x.get_x() - point_0.get_x();
    _xy = point_y.get_x() - point_0.get_x();
    _yx = point_x.get_y() - point_0.get_y();
    _yy = point_y.get_y() - point_0.get_y();
    _x_offset = point_0.get_x();
    _y_offset = point_0.get_y();
  }
  DFTransform get_composed(const DFTransform& inner_transform) const
  {
    DFTransform transform;
    transform.set_xx(_xx * inner_transform.get_xx() + _xy * inner_transform.get_yx());
    transform.set_xy(_xx * inner_transform.get_xy() + _xy * inner_transform.get_yy());
    transform.set_yx(_yx * inner_transform.get_xx() + _yy * inner_transform.get_yx());
    transform.set_yy(_yx * inner_transform.get_xy() + _yy * inner_transform.get_yy());
    transform.set_x_offset(_xx * inner_transform.get_x_offset() + _xy * inner_transform.get_y_offset() + _x_offset);
    transform.set_y_offset(_yx * inner_transform.get_x_offset() + _yy * inner_transform.get_y_offset() + _y_offset);
    return transform;
  }
  idb::IdbCoordinate<int32_t> get_transformed_coordinate(int32_t x, int32_t y) const
  {
    return idb::IdbCoordinate<int32_t>(_xx * x + _xy * y + _x_offset, _yx * x + _yy * y + _y_offset);
  }
  idb::IdbRect get_transformed_rect(const idb::IdbRect& rect) const
  {
    idb::IdbCoordinate<int32_t> lower = get_transformed_coordinate(rect.get_low_x(), rect.get_low_y());
    idb::IdbCoordinate<int32_t> upper = get_transformed_coordinate(rect.get_high_x(), rect.get_high_y());
    return idb::IdbRect(std::min(lower.get_x(), upper.get_x()), std::min(lower.get_y(), upper.get_y()),
                        std::max(lower.get_x(), upper.get_x()), std::max(lower.get_y(), upper.get_y()));
  }
  idb::IdbOrient get_composed_orient(idb::IdbOrient orient) const
  {
    DFTransform orient_transform;
    orient_transform.set_instance_transform(orient, 0, 0, 1, 1, 0, 0);
    return get_composed(orient_transform).get_orient();
  }
  idb::IdbOrient get_orient() const
  {
    if (_xx == 1 && _xy == 0 && _yx == 0 && _yy == 1) {
      return idb::IdbOrient::kN_R0;
    }
    if (_xx == 0 && _xy == -1 && _yx == 1 && _yy == 0) {
      return idb::IdbOrient::kW_R90;
    }
    if (_xx == -1 && _xy == 0 && _yx == 0 && _yy == -1) {
      return idb::IdbOrient::kS_R180;
    }
    if (_xx == 0 && _xy == 1 && _yx == -1 && _yy == 0) {
      return idb::IdbOrient::kE_R270;
    }
    if (_xx == -1 && _xy == 0 && _yx == 0 && _yy == 1) {
      return idb::IdbOrient::kFN_MY;
    }
    if (_xx == 0 && _xy == -1 && _yx == -1 && _yy == 0) {
      return idb::IdbOrient::kFE_MY90;
    }
    if (_xx == 1 && _xy == 0 && _yx == 0 && _yy == -1) {
      return idb::IdbOrient::kFS_MX;
    }
    if (_xx == 0 && _xy == 1 && _yx == 1 && _yy == 0) {
      return idb::IdbOrient::kFW_MX90;
    }
    return idb::IdbOrient::kNone;
  }

 private:
  int32_t _xx = 1;
  int32_t _xy = 0;
  int32_t _yx = 0;
  int32_t _yy = 1;
  int32_t _x_offset = 0;
  int32_t _y_offset = 0;
};

}  // namespace imj
