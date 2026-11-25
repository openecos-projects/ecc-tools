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

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "ProcessEffectType.hpp"
#include "ProcessEtchTable.hpp"
#include "ProcessEvaluationIssue.hpp"
#include "ProcessTable1D.hpp"
#include "ProcessTable2D.hpp"

namespace ircx {

enum class WireResistanceModel { kNone, kSheet, kResistivity, kSheetByWidth, kSheetByWidthSpacing,
                                 kResistivityByWidthSpacing, kResistivityByWidthThickness };

class ProcessConductor
{
 public:
  ProcessConductor() = default;
  ~ProcessConductor() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  const std::string& get_layer_name() const { return _layer_name; }
  const std::string& get_layer_type() const { return _layer_type; }
  std::vector<ProcessEvaluationIssue>& get_evaluation_issues() { return _evaluation_issues; }
  const std::vector<ProcessEvaluationIssue>& get_evaluation_issues() const { return _evaluation_issues; }
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
  const ProcessTable1D& get_sheet_resistance_by_width_table() const { return _sheet_resistance_by_width_table; }
  ProcessTable1D& get_tmpr_coefficient1_by_width_table() { return _tmpr_coefficient1_by_width_table; }
  const ProcessTable1D& get_tmpr_coefficient1_by_width_table() const { return _tmpr_coefficient1_by_width_table; }
  ProcessTable1D& get_tmpr_coefficient2_by_width_table() { return _tmpr_coefficient2_by_width_table; }
  const ProcessTable1D& get_tmpr_coefficient2_by_width_table() const { return _tmpr_coefficient2_by_width_table; }
  ProcessTable2D& get_sheet_resistance_by_width_spacing_table() { return _sheet_resistance_by_width_spacing_table; }
  const ProcessTable2D& get_sheet_resistance_by_width_spacing_table() const { return _sheet_resistance_by_width_spacing_table; }
  ProcessTable2D& get_resistivity_by_width_thickness_table() { return _resistivity_by_width_thickness_table; }
  const ProcessTable2D& get_resistivity_by_width_thickness_table() const { return _resistivity_by_width_thickness_table; }
  ProcessTable2D& get_resistivity_by_width_spacing_table() { return _resistivity_by_width_spacing_table; }
  const ProcessTable2D& get_resistivity_by_width_spacing_table() const { return _resistivity_by_width_spacing_table; }
  std::vector<ProcessEtchTable>& get_etch_table_list() { return _etch_table_list; }
  std::vector<ProcessEtchTable>& get_thickness_change_table_list() { return _thickness_change_table_list; }
  const std::vector<ProcessEtchTable>& get_etch_table_list() const { return _etch_table_list; }
  const std::vector<ProcessEtchTable>& get_thickness_change_table_list() const { return _thickness_change_table_list; }
  WireResistanceModel get_resistance_model() const { return _resistance_model; }
  void set_resistance_model(WireResistanceModel model) { _resistance_model = model; }
  // setter
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_layer_type(const std::string& layer_type) { _layer_type = layer_type; }
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

 private:
  WireResistanceModel _resistance_model = WireResistanceModel::kNone;
  std::string _layer_name;
  std::string _layer_type;
  std::vector<ProcessEvaluationIssue> _evaluation_issues;
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
