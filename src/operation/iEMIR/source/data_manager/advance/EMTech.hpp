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

namespace iemir {

class EMMetalRule
{
 public:
  EMMetalRule() = default;
  ~EMMetalRule() = default;
  // getter
  std::string& get_name() { return _name; }
  double get_em_limit_ma_per_um() { return _em_limit_ma_per_um; }
  double get_em_adjust_um() { return _em_adjust_um; }
  // setter
  void set_name(const std::string& name) { _name = name; }
  void set_em_limit_ma_per_um(double em_limit_ma_per_um) { _em_limit_ma_per_um = em_limit_ma_per_um; }
  void set_em_adjust_um(double em_adjust_um) { _em_adjust_um = em_adjust_um; }
  // function

 private:
  std::string _name;
  double _em_limit_ma_per_um = 0.0;
  double _em_adjust_um = 0.0;
};

class EMViaRule
{
 public:
  EMViaRule() = default;
  ~EMViaRule() = default;
  // getter
  std::string& get_name() { return _name; }
  double get_em_limit_ma() { return _em_limit_ma; }
  double get_reference_area_um2() { return _reference_area_um2; }
  // setter
  void set_name(const std::string& name) { _name = name; }
  void set_em_limit_ma(double em_limit_ma) { _em_limit_ma = em_limit_ma; }
  void set_reference_area_um2(double reference_area_um2) { _reference_area_um2 = reference_area_um2; }
  // function

 private:
  std::string _name;
  double _em_limit_ma = 0.0;
  double _reference_area_um2 = 0.0;
};

class EMTech
{
 public:
  EMTech() = default;
  ~EMTech() = default;
  // getter
  std::string& get_source_file_path() { return _source_file_path; }
  std::map<std::string, EMMetalRule>& get_metal_rule_map() { return _metal_rule_map; }
  std::map<std::string, EMViaRule>& get_via_rule_map() { return _via_rule_map; }
  double get_half_node_scale_factor() { return _half_node_scale_factor; }
  bool get_has_rule() { return !_metal_rule_map.empty() || !_via_rule_map.empty(); }
  // setter
  void set_source_file_path(const std::string& source_file_path) { _source_file_path = source_file_path; }
  void set_half_node_scale_factor(double half_node_scale_factor) { _half_node_scale_factor = half_node_scale_factor; }
  // function
  void clear()
  {
    _source_file_path.clear();
    _half_node_scale_factor = 1.0;
    _metal_rule_map.clear();
    _via_rule_map.clear();
  }

 private:
  std::string _source_file_path;
  double _half_node_scale_factor = 1.0;
  std::map<std::string, EMMetalRule> _metal_rule_map;
  std::map<std::string, EMViaRule> _via_rule_map;
};

}  // namespace iemir
