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

#include "EMIRHeader.hpp"
#include "PowerEdgeType.hpp"

namespace iemir {

class PowerEdge
{
 public:
  PowerEdge() = default;
  ~PowerEdge() = default;
  // getter
  std::size_t get_edge_id() { return _edge_id; }
  PowerEdgeType get_type() { return _type; }
  std::size_t get_first_node_id() { return _first_node_id; }
  std::size_t get_second_node_id() { return _second_node_id; }
  int32_t get_layer_idx() { return _layer_idx; }
  std::string& get_layer_name() { return _layer_name; }
  std::string& get_via_name() { return _via_name; }
  int32_t get_width() { return _width; }
  int32_t get_length() { return _length; }
  int32_t get_cut_num() { return _cut_num; }
  double get_cut_area_um2() { return _cut_area_um2; }
  int32_t get_cut_low_x() { return _cut_low_x; }
  int32_t get_cut_low_y() { return _cut_low_y; }
  int32_t get_cut_high_x() { return _cut_high_x; }
  int32_t get_cut_high_y() { return _cut_high_y; }
  double get_resistance() { return _resistance; }
  double get_current() { return _current; }
  double get_current_density() { return _current_density; }
  double get_em_limit() { return _em_limit; }
  double get_em_ratio_percent() { return _em_ratio_percent; }
  std::string& get_em_rule_name() { return _em_rule_name; }
  bool get_has_em_limit() { return _has_em_limit; }
  bool get_is_violation() { return _is_violation; }
  bool get_is_em_checkable() { return _is_em_checkable; }
  // setter
  void set_edge_id(std::size_t edge_id) { _edge_id = edge_id; }
  void set_type(PowerEdgeType type) { _type = type; }
  void set_first_node_id(std::size_t first_node_id) { _first_node_id = first_node_id; }
  void set_second_node_id(std::size_t second_node_id) { _second_node_id = second_node_id; }
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_via_name(const std::string& via_name) { _via_name = via_name; }
  void set_width(int32_t width) { _width = width; }
  void set_length(int32_t length) { _length = length; }
  void set_cut_num(int32_t cut_num) { _cut_num = cut_num; }
  void set_cut_area_um2(double cut_area_um2) { _cut_area_um2 = cut_area_um2; }
  void set_cut_low_x(int32_t cut_low_x) { _cut_low_x = cut_low_x; }
  void set_cut_low_y(int32_t cut_low_y) { _cut_low_y = cut_low_y; }
  void set_cut_high_x(int32_t cut_high_x) { _cut_high_x = cut_high_x; }
  void set_cut_high_y(int32_t cut_high_y) { _cut_high_y = cut_high_y; }
  void set_resistance(double resistance) { _resistance = resistance; }
  void set_current(double current) { _current = current; }
  void set_current_density(double current_density) { _current_density = current_density; }
  void set_em_limit(double em_limit) { _em_limit = em_limit; }
  void set_em_ratio_percent(double em_ratio_percent) { _em_ratio_percent = em_ratio_percent; }
  void set_em_rule_name(const std::string& em_rule_name) { _em_rule_name = em_rule_name; }
  void set_has_em_limit(bool has_em_limit) { _has_em_limit = has_em_limit; }
  void set_is_violation(bool is_violation) { _is_violation = is_violation; }
  void set_is_em_checkable(bool is_em_checkable) { _is_em_checkable = is_em_checkable; }
  // function

 private:
  std::size_t _edge_id = 0;
  PowerEdgeType _type = PowerEdgeType::kNone;
  std::size_t _first_node_id = 0;
  std::size_t _second_node_id = 0;
  int32_t _layer_idx = -1;
  std::string _layer_name;
  std::string _via_name;
  int32_t _width = 0;
  int32_t _length = 0;
  int32_t _cut_num = 0;
  double _cut_area_um2 = 0.0;
  int32_t _cut_low_x = 0;
  int32_t _cut_low_y = 0;
  int32_t _cut_high_x = 0;
  int32_t _cut_high_y = 0;
  double _resistance = 0.0;
  double _current = 0.0;
  double _current_density = 0.0;
  double _em_limit = 0.0;
  double _em_ratio_percent = 0.0;
  std::string _em_rule_name;
  bool _has_em_limit = false;
  bool _is_violation = false;
  bool _is_em_checkable = true;
};

}  // namespace iemir
