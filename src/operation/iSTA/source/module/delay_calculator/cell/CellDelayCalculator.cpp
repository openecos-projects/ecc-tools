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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "CellDelayCalculator.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <ranges>
#include <string>
#include <vector>

#include "TimingTolerance.hpp"

namespace ista {

bool CellDelayCalculator::calculateDrivingCell(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType output_trans_type,
                                               double input_transition_rise, double input_transition_fall, DelayResult& timing_result)
{
  timing_result.set_output_trans_type(output_trans_type);
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    timing_result.set_delay(0.0);
    timing_result.set_slew(output_trans_type == TransType::kRise ? input_transition_rise : input_transition_fall);
    return true;
  }

  const double output_load = _load.pinLoad(output_pin, analysis_type, output_trans_type);
  TimingArc* selected_arc = nullptr;
  double selected_input_slew = 0.;
  double selected_table_delay = 0.;
  for (TransType input_trans_type : {TransType::kRise, TransType::kFall}) {
    const double input_slew = input_trans_type == TransType::kRise ? input_transition_rise : input_transition_fall;
    for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
      if (!timing_arc->get_delay_table_map().contains(output_trans_type) && !timing_arc->get_slew_table_map().contains(output_trans_type))
        continue;
      // Choose the external drive condition from its library load delay.
      // A fitted waveform has a small delay residual, which can reverse the
      // ordering of weak/strong conditional arcs after intrinsic subtraction.
      const double load_delay = calcTimingArcDelayByLoad(*timing_arc, output_trans_type, input_slew, output_load)
                                - calcTimingArcDelayByLoad(*timing_arc, output_trans_type, input_slew, 0.);
      if (!selected_arc || (analysis_type == AnalysisType::kMin && load_delay < selected_table_delay)
          || (analysis_type == AnalysisType::kMax && load_delay > selected_table_delay)) {
        selected_arc = timing_arc;
        selected_input_slew = input_slew;
        selected_table_delay = load_delay;
      }
    }
  }
  if (!selected_arc)
    return false;
  double load_delay = 0.;
  if (selected_arc->get_delay_table_map().contains(output_trans_type)) {
    const double loaded_delay = calcTimingArcDelay(output_pin, *selected_arc, analysis_type, output_trans_type, selected_input_slew, output_load);
    const double intrinsic_delay = calcTimingArcDelayByLoad(*selected_arc, output_trans_type, selected_input_slew, 0.);
    load_delay = loaded_delay - intrinsic_delay;
  }
  timing_result.set_delay(load_delay);
  const double output_slew = selected_arc->get_slew_table_map().contains(output_trans_type)
                                 ? calcTimingArcSlew(output_pin, *selected_arc, analysis_type, output_trans_type, selected_input_slew, output_load)
                                 : selected_input_slew;
  timing_result.set_slew(output_slew);
  return true;
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type)
{
  return calcTimingCellArcDelay(arc, timing_cell_arc, analysis_type, TransType::kRise);
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  TransType output_trans_type = _selection.getOutputTransType(timing_cell_arc, input_trans_type);
  arc.get_trans_type_map()[input_trans_type] = output_trans_type;
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double input_slew = 0.0;
  double raw_output_load = _load.arcLoad(arc, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin())) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double delay = calcTimingArcDelay(arc.get_sink_pin(), *timing_arc, analysis_type, output_trans_type, input_slew, raw_output_load);
    updateTimingArcDelay(arc, *timing_arc, analysis_type, input_trans_type, output_trans_type, delay, false);
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                   TransType output_trans_type)
{
  return calcTimingCellArcDelay(arc, timing_cell_arc, analysis_type, input_trans_type, output_trans_type, 0.0);
}

double CellDelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                   TransType output_trans_type, double input_slew, bool is_initialization)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double raw_output_load = _load.arcLoad(arc, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin())) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double delay = calcTimingArcDelay(arc.get_sink_pin(), *timing_arc, analysis_type, output_trans_type, input_slew, raw_output_load);
    updateTimingArcDelay(arc, *timing_arc, analysis_type, input_trans_type, output_trans_type, delay, is_initialization);
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

void CellDelayCalculator::updateTimingArcDelay(Arc& arc, TimingArc& timing_arc, AnalysisType analysis_type, TransType input_trans_type,
                                               TransType output_trans_type, double delay, bool is_initialization)
{
  arc.update_timing_arc_delay(timing_arc.get_arc_idx(), analysis_type, input_trans_type, output_trans_type, delay, is_initialization);
}

double CellDelayCalculator::calcTimingCellArcSlew(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                  TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = _load.arcLoad(arc, analysis_type, output_trans_type);
  std::vector<TimingArc*> candidate_arc_list = _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin());
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : candidate_arc_list) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlew(arc.get_sink_pin(), *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

double CellDelayCalculator::calcTimingCellArcDelay(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                   TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type)) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double delay = calcTimingArcDelayByLoad(*timing_arc, output_trans_type, input_slew, output_load);
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

double CellDelayCalculator::calcTimingCellArcSlew(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                  TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  std::vector<TimingArc*> candidate_arc_list = _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type);
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : candidate_arc_list) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlewByLoad(*timing_arc, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

double CellDelayCalculator::convertOutputLoad(TimingArc& timing_arc, double output_load)
{
  if (std::abs(timing_arc.get_cap_unit_scale() - 1.0) < ista::kTimingTolerance) {
    return output_load;
  }
  return static_cast<int>(std::ceil(output_load * timing_arc.get_cap_unit_scale()));
}

double CellDelayCalculator::calcTimingArcDelay(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                               double input_slew, double output_load)
{
  DriverResponse& timing_result = _arnoldi.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  if (timing_result.get_is_valid()) {
    return timing_result.get_gate_delay();
  }
  DriverResponse& dmp_timing_result = _dmp.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (dmp_timing_result.get_is_valid()) {
    return dmp_timing_result.get_gate_delay();
  }
  return calcTimingArcDelayByLoad(timing_arc, output_trans_type, input_slew, output_load);
}

double CellDelayCalculator::calcTimingArcSlew(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                              double input_slew, double output_load)
{
  DriverResponse& timing_result = _arnoldi.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  if (timing_result.get_is_valid()) {
    return timing_result.get_driver_slew();
  }
  DriverResponse& dmp_timing_result = _dmp.response(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (dmp_timing_result.get_is_valid()) {
    return dmp_timing_result.get_driver_slew();
  }
  return calcTimingArcSlewByLoad(timing_arc, output_trans_type, input_slew, output_load);
}

double CellDelayCalculator::calcTimingArcDelayByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_delay_table_map().count(output_trans_type) == 0) {
    return 0.0;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double delay = timing_arc.get_delay_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return delay / timing_arc.get_time_unit_scale();
}

double CellDelayCalculator::calcTimingArcSlewByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_slew_table_map().count(output_trans_type) == 0) {
    return input_slew;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double slew = timing_arc.get_slew_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return slew / timing_arc.get_time_unit_scale();
}

double CellDelayCalculator::calcTimingCellArcDelay(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                   TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double output_load = _load.pinLoad(output_pin, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
    if (timing_arc->get_delay_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double delay = calcTimingArcDelay(output_pin, *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    delay_list.push_back(delay);
  }
  if (delay_list.empty()) {
    return timing_cell_arc.get_delay();
  }
  std::ranges::sort(delay_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return delay_list.back();
  }
  return delay_list.front();
}

double CellDelayCalculator::calcTimingCellArcSlew(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                  TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!_selection.isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = _load.pinLoad(output_pin, analysis_type, output_trans_type);
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : _selection.cellCandidates(timing_cell_arc, input_trans_type, output_trans_type, output_pin)) {
    if (timing_arc->get_slew_table_map().count(output_trans_type) == 0) {
      continue;
    }
    double slew = calcTimingArcSlew(output_pin, *timing_arc, analysis_type, output_trans_type, input_slew, output_load);
    slew_list.push_back(slew);
  }
  if (slew_list.empty()) {
    return input_slew;
  }
  std::ranges::sort(slew_list, std::greater<double>());
  if (analysis_type == AnalysisType::kMin) {
    return slew_list.back();
  }
  return slew_list.front();
}

}  // namespace ista
