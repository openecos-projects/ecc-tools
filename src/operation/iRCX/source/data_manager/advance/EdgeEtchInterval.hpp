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
  double get_resistance_center() const { return _resistance_center; }
  double get_resistance_width() const { return _resistance_width; }
  double get_resistance_lower_spacing() const { return _resistance_lower_spacing; }
  double get_resistance_upper_spacing() const { return _resistance_upper_spacing; }
  double get_resistance_thickness() const { return _resistance_thickness; }
  double get_capacitance_center() const { return _capacitance_center; }
  double get_capacitance_width() const { return _capacitance_width; }
  double get_capacitance_lower_spacing() const { return _capacitance_lower_spacing; }
  double get_capacitance_upper_spacing() const { return _capacitance_upper_spacing; }
  double get_capacitance_thickness() const { return _capacitance_thickness; }
  // setter
  void set_start_coord(double start_coord) { _start_coord = start_coord; }
  void set_end_coord(double end_coord) { _end_coord = end_coord; }
  void set_center(double center)
  {
    _center = center;
    _resistance_center = center;
    _capacitance_center = center;
  }
  void set_width(double width)
  {
    _width = width;
    _resistance_width = width;
    _capacitance_width = width;
  }
  void set_lower_spacing(double lower_spacing)
  {
    _lower_spacing = lower_spacing;
    _resistance_lower_spacing = lower_spacing;
    _capacitance_lower_spacing = lower_spacing;
  }
  void set_upper_spacing(double upper_spacing)
  {
    _upper_spacing = upper_spacing;
    _resistance_upper_spacing = upper_spacing;
    _capacitance_upper_spacing = upper_spacing;
  }
  void set_thickness(double thickness)
  {
    _thickness = thickness;
    _resistance_thickness = thickness;
    _capacitance_thickness = thickness;
  }
  void set_height(double height) { _height = height; }
  void set_resistance_center(double resistance_center) { _resistance_center = resistance_center; }
  void set_resistance_width(double resistance_width) { _resistance_width = resistance_width; }
  void set_resistance_lower_spacing(double resistance_lower_spacing) { _resistance_lower_spacing = resistance_lower_spacing; }
  void set_resistance_upper_spacing(double resistance_upper_spacing) { _resistance_upper_spacing = resistance_upper_spacing; }
  void set_resistance_thickness(double resistance_thickness) { _resistance_thickness = resistance_thickness; }
  void set_capacitance_center(double capacitance_center) { _capacitance_center = capacitance_center; }
  void set_capacitance_width(double capacitance_width) { _capacitance_width = capacitance_width; }
  void set_capacitance_lower_spacing(double capacitance_lower_spacing) { _capacitance_lower_spacing = capacitance_lower_spacing; }
  void set_capacitance_upper_spacing(double capacitance_upper_spacing) { _capacitance_upper_spacing = capacitance_upper_spacing; }
  void set_capacitance_thickness(double capacitance_thickness) { _capacitance_thickness = capacitance_thickness; }
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
  double _resistance_center = -1.0;
  double _resistance_width = -1.0;
  double _resistance_lower_spacing = -1.0;
  double _resistance_upper_spacing = -1.0;
  double _resistance_thickness = -1.0;
  double _capacitance_center = -1.0;
  double _capacitance_width = -1.0;
  double _capacitance_lower_spacing = -1.0;
  double _capacitance_upper_spacing = -1.0;
  double _capacitance_thickness = -1.0;
};

}  // namespace ircx
