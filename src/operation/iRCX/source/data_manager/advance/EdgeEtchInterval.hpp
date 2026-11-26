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
#include "ProcessGeometry.hpp"

namespace ircx {

class EdgeEtchInterval
{
 public:
  EdgeEtchInterval() = default;
  ~EdgeEtchInterval() = default;
  // getter
  double get_start_coord() const { return _start_coord; }
  double get_end_coord() const { return _end_coord; }
  double get_center() const { return _center; }
  double get_width() const { return _width; }
  double get_lower_spacing() const { return _lower_spacing; }
  double get_upper_spacing() const { return _upper_spacing; }
  double get_thickness() const { return _thickness; }
  double get_height() const { return _height; }
  const std::vector<EffectiveWireSegment>& get_resistance_segments() const { return _resistance_segments; }
  void set_resistance_segments(std::vector<EffectiveWireSegment> segments) { _resistance_segments = std::move(segments); }
  // setter
  void set_start_coord(double start_coord) { _start_coord = start_coord; }
  void set_end_coord(double end_coord) { _end_coord = end_coord; }
  void set_center(double center) { _center = center; }
  void set_width(double width) { _width = width; }
  void set_lower_spacing(double spacing) { _lower_spacing = spacing; }
  void set_upper_spacing(double spacing) { _upper_spacing = spacing; }
  void set_thickness(double thickness) { _thickness = thickness; }
  void set_height(double height) { _height = height; }
  // function

 private:
  double _start_coord = -1.0;
  double _end_coord = -1.0;
  double _center = -1.0;
  double _width = -1.0;
  double _lower_spacing = -1.0;
  double _upper_spacing = -1.0;
  double _thickness = -1.0;
  double _height = -1.0;
  // Drawn coordinates above remain immutable during process evaluation.
  // Effective segments below use scaled physical coordinates in micrometres.
  std::vector<EffectiveWireSegment> _resistance_segments;
};

}  // namespace ircx
