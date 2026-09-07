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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "MIRect.hpp"

namespace izh {

class MIDensityWindow
{
 public:
  MIDensityWindow() = default;
  MIDensityWindow(const MIRect& rect, double metal_area) : _rect(rect), _metal_area(metal_area) {}
  ~MIDensityWindow() = default;
  // getter
  MIRect& get_rect() { return _rect; }
  double get_metal_area() const { return _metal_area; }
  double get_density() const { return _rect.get_area() <= 0.0 ? 0.0 : _metal_area / _rect.get_area(); }
  // const getter
  const MIRect& get_rect() const { return _rect; }
  // setter
  void set_rect(const MIRect& rect) { _rect = rect; }
  void set_metal_area(double metal_area) { _metal_area = metal_area; }
  // function
  void add_metal_area(double metal_area) { _metal_area += metal_area; }

 private:
  MIRect _rect;
  double _metal_area = 0.0;
};

}  // namespace izh
