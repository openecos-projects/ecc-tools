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

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "ProcessEffectType.hpp"
#include "ProcessEvaluationIssue.hpp"
#include "ProcessResult.hpp"
#include "ProcessTable1D.hpp"
#include "ProcessViaEtchTable.hpp"
#include "ViaEtch.hpp"

namespace ircx {

enum class ViaResistanceModel { kReferenceArea, kReferenceCut, kAreaTable };

class ProcessVia
{
 public:
  ProcessVia() = default;
  ~ProcessVia() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  std::string& get_from_layer_name() { return _from_layer_name; }
  std::string& get_to_layer_name() { return _to_layer_name; }
  const std::string& get_layer_name() const { return _layer_name; }
  std::vector<ProcessEvaluationIssue>& get_evaluation_issues() { return _evaluation_issues; }
  const std::vector<ProcessEvaluationIssue>& get_evaluation_issues() const { return _evaluation_issues; }
  double get_area() const { return _area; }
  double get_resistance() const { return _resistance; }
  double get_resistivity() const { return _resistivity; }
  bool get_has_nominal_tmpr() const { return _has_nominal_tmpr; }
  double get_nominal_tmpr() const { return _nominal_tmpr; }
  double get_tmpr_coefficient1() const { return _tmpr_coefficient1; }
  double get_tmpr_coefficient2() const { return _tmpr_coefficient2; }
  ProcessTable1D& get_resistance_by_area_table() { return _resistance_by_area_table; }
  const ProcessTable1D& get_resistance_by_area_table() const { return _resistance_by_area_table; }
  ProcessTable1D& get_tmpr_coefficient1_by_area_table() { return _tmpr_coefficient1_by_area_table; }
  const ProcessTable1D& get_tmpr_coefficient1_by_area_table() const { return _tmpr_coefficient1_by_area_table; }
  ProcessTable1D& get_tmpr_coefficient2_by_area_table() { return _tmpr_coefficient2_by_area_table; }
  const ProcessTable1D& get_tmpr_coefficient2_by_area_table() const { return _tmpr_coefficient2_by_area_table; }
  std::vector<ProcessViaEtchTable>& get_etch_table_list() { return _etch_table_list; }
  const std::vector<ProcessViaEtchTable>& get_etch_table_list() const { return _etch_table_list; }
  ViaResistanceModel get_resistance_model() const { return _resistance_model; }
  void set_resistance_model(ViaResistanceModel model) { _resistance_model = model; }
  const std::string& get_from_layer_name() const { return _from_layer_name; }
  const std::string& get_to_layer_name() const { return _to_layer_name; }
  // setter
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_from_layer_name(const std::string& from_layer_name) { _from_layer_name = from_layer_name; }
  void set_to_layer_name(const std::string& to_layer_name) { _to_layer_name = to_layer_name; }
  void set_area(double area) { _area = area; }
  void set_resistance(double resistance) { _resistance = resistance; }
  void set_resistivity(double resistivity) { _resistivity = resistivity; }
  void set_nominal_tmpr(double nominal_tmpr)
  {
    _nominal_tmpr = nominal_tmpr;
    _has_nominal_tmpr = true;
  }
  void set_tmpr_coefficient1(double tmpr_coefficient1) { _tmpr_coefficient1 = tmpr_coefficient1; }
  void set_tmpr_coefficient2(double tmpr_coefficient2) { _tmpr_coefficient2 = tmpr_coefficient2; }
  // function
  ProcessResult<double> query_resistance(double area) const
  {
    if (const auto* issue = findEvaluationIssue(_evaluation_issues, ProcessEffectType::kResistance))
      return "cannot evaluate via " + _layer_name + ": " + issue->field + ": " + issue->message;
    if (!std::isfinite(area) || area <= 0.0) {
      return "invalid resistance cut area for via " + _layer_name;
    }
    if (_resistance_model == ViaResistanceModel::kAreaTable) {
      auto resistance = _resistance_by_area_table.query(area);
      if (!resistance)
        return "missing or out-of-range via resistance table: " + _layer_name;
      return *resistance;
    }
    if (_resistance > 0.0) {
      if (!std::isfinite(_area) || _area <= 0.0) {
        return "RPV requires a positive finite AREA for via " + _layer_name;
      }
      if (_resistance_model == ViaResistanceModel::kReferenceCut) {
        if (std::abs(area - _area) > 1e-9 * std::max(area, _area))
          return "via resistance is only defined at its reference cut area: " + _layer_name;
        return _resistance;
      }
      // The scalar resistance describes a cut of the reference AREA.
      // The caller supplies the physical area after scaling and resistance etch.
      const double resistance = _resistance * (_area / area);
      if (!std::isfinite(resistance) || resistance <= 0.0) {
        return "invalid area-scaled resistance for via " + _layer_name;
      }
      return resistance;
    }
    auto resistance = _resistance_by_area_table.query(area);
    if (!resistance)
      return "missing via resistance table: " + _layer_name;
    return *resistance;
  }

  void query_tmpr_coefficient(double area, double& tmpr_coefficient1, double& tmpr_coefficient2) const
  {
    tmpr_coefficient1 = _tmpr_coefficient1;
    tmpr_coefficient2 = _tmpr_coefficient2;
    std::optional<double> coefficient1 = _tmpr_coefficient1_by_area_table.query(area);
    std::optional<double> coefficient2 = _tmpr_coefficient2_by_area_table.query(area);
    if (coefficient1.has_value()) {
      tmpr_coefficient1 = coefficient1.value();
    }
    if (coefficient2.has_value()) {
      tmpr_coefficient2 = coefficient2.value();
    }
  }

  ProcessResult<ViaEtch> query_etch(ProcessEffectType effect_type, double width, double length) const
  {
    if (effect_type != ProcessEffectType::kResistance && effect_type != ProcessEffectType::kCapacitance)
      return std::string("via geometry query requires one effect domain");
    if (const auto* issue = findEvaluationIssue(_evaluation_issues, effect_type))
      return "cannot evaluate via " + _layer_name + ": " + issue->field + ": " + issue->message;
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(length) || length <= 0)
      return "invalid etch query dimensions for via " + _layer_name;
    double length_etch = 0.0;
    double width_etch = 0.0;
    for (const ProcessViaEtchTable& etch_table : _etch_table_list) {
      if (!get_effect_is_applied(etch_table.get_effect_type(), effect_type)) {
        continue;
      }
      std::optional<double> table_length_etch = etch_table.get_length_table().query(width, length);
      std::optional<double> table_width_etch = etch_table.get_width_table().query(width, length);
      if (!table_length_etch || !table_width_etch)
        return "missing or out-of-range via etch table: " + _layer_name;
      length_etch += *table_length_etch;
      width_etch += *table_width_etch;
    }
    if (!std::isfinite(length_etch) || !std::isfinite(width_etch))
      return "nonfinite etch for via " + _layer_name;
    return ViaEtch(length_etch, width_etch);
  }

 private:
  bool get_effect_is_applied(ProcessEffectType table_effect_type, ProcessEffectType query_effect_type) const
  {
    return table_effect_type == ProcessEffectType::kBoth || table_effect_type == query_effect_type;
  }

  ViaResistanceModel _resistance_model = ViaResistanceModel::kReferenceArea;
  std::string _layer_name;
  std::vector<ProcessEvaluationIssue> _evaluation_issues;
  std::string _from_layer_name;
  std::string _to_layer_name;
  double _area = -1.0;
  double _resistance = -1.0;
  double _resistivity = -1.0;
  bool _has_nominal_tmpr = false;
  double _nominal_tmpr = -1.0;
  double _tmpr_coefficient1 = 0.0;
  double _tmpr_coefficient2 = 0.0;
  ProcessTable1D _resistance_by_area_table;
  ProcessTable1D _tmpr_coefficient1_by_area_table;
  ProcessTable1D _tmpr_coefficient2_by_area_table;
  std::vector<ProcessViaEtchTable> _etch_table_list;
};

}  // namespace ircx
