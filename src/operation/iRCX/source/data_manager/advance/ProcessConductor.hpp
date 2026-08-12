// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "ProcessEffectType.hpp"
#include "ProcessEtchTable.hpp"
#include "ProcessTable1D.hpp"
#include "ProcessTable2D.hpp"

namespace ircx {

class ProcessConductor
{
 public:
  ProcessConductor() = default;
  ~ProcessConductor() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  const std::string& get_layer_name() const { return _layer_name; }
  double get_thickness() const { return _thickness; }
  double get_sheet_resistance() const { return _sheet_resistance; }
  double get_resistivity() const { return _resistivity; }
  bool get_has_nominal_tmpr() const { return _has_nominal_tmpr; }
  double get_nominal_tmpr() const { return _nominal_tmpr; }
  double get_tmpr_coefficient1() const { return _tmpr_coefficient1; }
  double get_tmpr_coefficient2() const { return _tmpr_coefficient2; }
  double get_etch() const { return _etch; }
  double get_resistive_only_etch() const { return _resistive_only_etch; }
  double get_capacitive_only_etch() const { return _capacitive_only_etch; }
  ProcessTable1D& get_sheet_resistance_by_width_table() { return _sheet_resistance_by_width_table; }
  ProcessTable1D& get_tmpr_coefficient1_by_width_table() { return _tmpr_coefficient1_by_width_table; }
  ProcessTable1D& get_tmpr_coefficient2_by_width_table() { return _tmpr_coefficient2_by_width_table; }
  ProcessTable2D& get_sheet_resistance_by_width_spacing_table() { return _sheet_resistance_by_width_spacing_table; }
  ProcessTable2D& get_resistivity_by_width_thickness_table() { return _resistivity_by_width_thickness_table; }
  ProcessTable2D& get_resistivity_by_width_spacing_table() { return _resistivity_by_width_spacing_table; }
  std::vector<ProcessEtchTable>& get_etch_table_list() { return _etch_table_list; }
  std::vector<ProcessEtchTable>& get_thickness_change_table_list() { return _thickness_change_table_list; }
  const std::vector<ProcessEtchTable>& get_etch_table_list() const { return _etch_table_list; }
  const std::vector<ProcessEtchTable>& get_thickness_change_table_list() const { return _thickness_change_table_list; }
  // setter
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_thickness(double thickness) { _thickness = thickness; }
  void set_sheet_resistance(double sheet_resistance) { _sheet_resistance = sheet_resistance; }
  void set_resistivity(double resistivity) { _resistivity = resistivity; }
  void set_nominal_tmpr(double nominal_tmpr)
  {
    _nominal_tmpr = nominal_tmpr;
    _has_nominal_tmpr = true;
  }
  void set_tmpr_coefficient1(double tmpr_coefficient1) { _tmpr_coefficient1 = tmpr_coefficient1; }
  void set_tmpr_coefficient2(double tmpr_coefficient2) { _tmpr_coefficient2 = tmpr_coefficient2; }
  void set_etch(double etch) { _etch = etch; }
  void set_resistive_only_etch(double resistive_only_etch) { _resistive_only_etch = resistive_only_etch; }
  void set_capacitive_only_etch(double capacitive_only_etch) { _capacitive_only_etch = capacitive_only_etch; }
  // function
  std::optional<double> query_sheet_resistance(double width, double lower_spacing, double upper_spacing) const
  {
    double spacing = std::min(lower_spacing, upper_spacing);
    std::optional<double> sheet_resistance = _sheet_resistance_by_width_spacing_table.query(width, spacing);
    if (sheet_resistance.has_value()) {
      return sheet_resistance;
    }
    sheet_resistance = _sheet_resistance_by_width_table.query(width);
    if (sheet_resistance.has_value()) {
      return sheet_resistance;
    }
    if (_sheet_resistance > 0.0) {
      return _sheet_resistance;
    }
    return std::nullopt;
  }

  std::optional<double> query_resistivity(double width, double thickness, double lower_spacing, double upper_spacing) const
  {
    std::optional<double> resistivity = _resistivity_by_width_thickness_table.query(thickness, width);
    if (resistivity.has_value()) {
      return resistivity;
    }
    double spacing = std::min(lower_spacing, upper_spacing);
    resistivity = _resistivity_by_width_spacing_table.query(width, spacing);
    if (resistivity.has_value()) {
      return resistivity;
    }
    if (_resistivity > 0.0) {
      return _resistivity;
    }
    return std::nullopt;
  }

  void query_tmpr_coefficient(double width, double& tmpr_coefficient1, double& tmpr_coefficient2) const
  {
    tmpr_coefficient1 = _tmpr_coefficient1;
    tmpr_coefficient2 = _tmpr_coefficient2;
    std::optional<double> coefficient1 = _tmpr_coefficient1_by_width_table.query(width);
    std::optional<double> coefficient2 = _tmpr_coefficient2_by_width_table.query(width);
    if (coefficient1.has_value()) {
      tmpr_coefficient1 = coefficient1.value();
    }
    if (coefficient2.has_value()) {
      tmpr_coefficient2 = coefficient2.value();
    }
  }

  double query_etch(ProcessEffectType effect_type, double width, double spacing) const
  {
    double etch = _etch;
    if (effect_type == ProcessEffectType::kResistance) {
      etch += _resistive_only_etch;
    } else if (effect_type == ProcessEffectType::kCapacitance) {
      etch += _capacitive_only_etch;
    }
    for (const ProcessEtchTable& etch_table : _etch_table_list) {
      if (!get_effect_is_applied(etch_table.get_effect_type(), effect_type)) {
        continue;
      }
      std::optional<double> table_etch = etch_table.get_table().query(width, spacing);
      if (table_etch.has_value()) {
        etch += table_etch.value();
      }
    }
    return etch;
  }

  double query_thickness_change(ProcessEffectType effect_type, double width, double spacing) const
  {
    double thickness_change = 0.0;
    for (const ProcessEtchTable& thickness_change_table : _thickness_change_table_list) {
      if (!get_effect_is_applied(thickness_change_table.get_effect_type(), effect_type)) {
        continue;
      }
      std::optional<double> table_thickness_change = thickness_change_table.get_table().query(width, spacing);
      if (table_thickness_change.has_value()) {
        thickness_change += table_thickness_change.value();
      }
    }
    return thickness_change;
  }

 private:
  bool get_effect_is_applied(ProcessEffectType table_effect_type, ProcessEffectType query_effect_type) const
  {
    return table_effect_type == ProcessEffectType::kBoth || table_effect_type == query_effect_type;
  }

  std::string _layer_name;
  double _thickness = -1.0;
  double _sheet_resistance = -1.0;
  double _resistivity = -1.0;
  bool _has_nominal_tmpr = false;
  double _nominal_tmpr = -1.0;
  double _tmpr_coefficient1 = 0.0;
  double _tmpr_coefficient2 = 0.0;
  double _etch = 0.0;
  double _resistive_only_etch = 0.0;
  double _capacitive_only_etch = 0.0;
  ProcessTable1D _sheet_resistance_by_width_table;
  ProcessTable1D _tmpr_coefficient1_by_width_table;
  ProcessTable1D _tmpr_coefficient2_by_width_table;
  ProcessTable2D _sheet_resistance_by_width_spacing_table;
  ProcessTable2D _resistivity_by_width_thickness_table;
  ProcessTable2D _resistivity_by_width_spacing_table;
  std::vector<ProcessEtchTable> _etch_table_list;
  std::vector<ProcessEtchTable> _thickness_change_table_list;
};

}  // namespace ircx
