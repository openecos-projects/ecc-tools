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
#include "DelayCalculator.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"

namespace ista {
double DelayCalculator::inputPortSlew(std::string pin, AnalysisType analysis, TransType edge)
{
  const auto found = _database.sdc().get_port_constraint_map().find(pin);
  if (found == _database.sdc().get_port_constraint_map().end())
    return 0.;
  const auto& constraint = found->second;
  // Explicit transition overrides driving-cell slew, including zero on an
  // unspecified lane. Driving-cell delay is queried separately.
  if (constraint.get_has_input_transition())
    return constraint.get_input_transition(analysis, edge);
  const auto driver = inputPortTiming(pin, analysis, edge);
  return driver ? driver->get_slew() : 0.;
}

std::optional<DelayResult> DelayCalculator::inputPortTiming(std::string& start_point, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  std::map<std::string, TimingPortConstraint>& port_constraint_map = database.sdc().get_port_constraint_map();
  if (!port_constraint_map.contains(start_point)) {
    return std::nullopt;
  }
  TimingDrivingCell* driving_cell = port_constraint_map.at(start_point).get_driving_cell(analysis_type, trans_type);
  if (driving_cell == nullptr) {
    return std::nullopt;
  }
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (!timing_cell_map.contains(driving_cell->get_cell_name())) {
    return std::nullopt;
  }
  LibertyCell& timing_cell = timing_cell_map.at(driving_cell->get_cell_name());
  if (timing_cell.get_library_name() != driving_cell->get_library_name()) {
    return std::nullopt;
  }
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.isThreeStateArc() || timing_cell_arc.get_is_clear_preset_arc())
      continue;
    if (timing_cell_arc.get_source_port() != driving_cell->get_from_pin() || timing_cell_arc.get_sink_port() != driving_cell->get_to_pin()) {
      continue;
    }
    DelayResult timing_result;
    if (calculateDrivingCell(start_point, timing_cell_arc, analysis_type, trans_type, driving_cell->get_input_transition_rise(),
                             driving_cell->get_input_transition_fall(), timing_result)) {
      return timing_result;
    }
  }
  return std::nullopt;
}

void DelayCalculator::initialize(Graph& graph)
{
  for (Arc& arc : graph.arcs()) {
    initialize(arc);
  }
}

void DelayCalculator::completeCellArcDelays(Graph& graph)
{
  for (auto& arc : graph.arcs()) {
    if (arc.get_type() != ArcType::kCell)
      continue;
    auto* family = _selection.cellArc(arc);
    if (!family)
      continue;
    std::set<int32_t> missing;
    for (auto& variant : family->get_timing_arc_list())
      if (!variant.get_sdf_cond().empty() && !arc.get_timing_arc_delay_map().contains(variant.get_arc_idx()))
        missing.insert(variant.get_arc_idx());
    if (missing.empty())
      continue;

    const auto source = graph.points().find(arc.get_source_pin());
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
      for (auto input : {TransType::kRise, TransType::kFall}) {
        if (!_selection.isClockArcTriggerTrans(*family, input))
          continue;
        std::set<double> slews;
        if (source != graph.points().end()) {
          const auto& data = source->second.get_data_slew_map();
          const auto lane = data.find(mode);
          if (lane != data.end() && lane->second.contains(input))
            slews.insert(lane->second.at(input));
          for (const auto& [clock, state] : source->second.get_clock_state_map())
            for (const auto& [edge, wave] : state.waveforms)
              if (const auto slew = wave.physicalSlew(mode, input))
                slews.insert(*slew);
        }
        if (slews.empty())
          slews.insert(0.);
        for (auto output : _selection.getOutputTransTypeList(*family, input)) {
          // No instance name: evaluate the variant itself without case pruning.
          // This pass runs after analysis; only the per-variant export cache is filled.
          for (auto* variant : _selection.cellCandidates(*family, input, output)) {
            if (!missing.contains(variant->get_arc_idx()) || !variant->get_delay_table_map().contains(output))
              continue;
            const double load = _load.arcLoad(arc, mode, output);
            for (double slew : slews) {
              const double delay = _cell.calcTimingArcDelay(arc.get_sink_pin(), *variant, mode, output, slew, load);
              arc.update_timing_arc_delay(variant->get_arc_idx(), mode, input, output, delay, false);
            }
          }
        }
      }
    }
  }
}

void DelayCalculator::initializeArcTiming(Arc& arc)
{
  arc.clearRcFailures();
  initializeAnalysisArcTiming(arc, AnalysisType::kMax);
  initializeAnalysisArcTiming(arc, AnalysisType::kMin);
  arc.set_delay_max(std::max(arc.get_trans_delay_map()[AnalysisType::kMax][TransType::kRise], arc.get_trans_delay_map()[AnalysisType::kMax][TransType::kFall]));
  arc.set_delay_min(std::min(arc.get_trans_delay_map()[AnalysisType::kMin][TransType::kRise], arc.get_trans_delay_map()[AnalysisType::kMin][TransType::kFall]));
  arc.set_delay(arc.get_delay_max());
}

void DelayCalculator::initializeAnalysisArcTiming(Arc& arc, AnalysisType analysis_type)
{
  initializeTransArcTiming(arc, analysis_type, TransType::kRise);
  initializeTransArcTiming(arc, analysis_type, TransType::kFall);
}

void DelayCalculator::initializeTransArcTiming(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
{
  if (arc.get_type() == ArcType::kNet) {
    double delay = _wire.calcNetArcDelay(arc, analysis_type, input_trans_type);
    arc.get_input_output_delay_map()[analysis_type][input_trans_type][input_trans_type] = delay;
    arc.get_trans_delay_map()[analysis_type][input_trans_type] = delay;
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    return;
  }

  TimingCellArc* timing_cell_arc = _selection.cellArc(arc);
  if (timing_cell_arc == nullptr || timing_cell_arc->get_timing_arc_list().empty()) {
    double delay = calcCellArcDelay(arc, analysis_type, input_trans_type);
    arc.get_input_output_delay_map()[analysis_type][input_trans_type][input_trans_type] = delay;
    arc.get_trans_delay_map()[analysis_type][input_trans_type] = delay;
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    return;
  }
  if (!_selection.isClockArcTriggerTrans(*timing_cell_arc, input_trans_type)) {
    return;
  }

  for (TransType output_trans_type : _selection.getOutputTransTypeList(*timing_cell_arc, input_trans_type)) {
    if (!timing_cell_arc->get_timing_arc_list().empty()
        && _selection.cellCandidates(*timing_cell_arc, input_trans_type, output_trans_type, arc.get_sink_pin()).empty()) {
      continue;
    }
    double delay = _cell.calcTimingCellArcDelay(arc, *timing_cell_arc, analysis_type, input_trans_type, output_trans_type, 0.0, true);
    arc.get_input_output_delay_map()[analysis_type][input_trans_type][output_trans_type] = delay;
    if (arc.get_trans_delay_map()[analysis_type].count(input_trans_type) == 0
        || (analysis_type == AnalysisType::kMin && delay < arc.get_trans_delay_map()[analysis_type][input_trans_type])
        || (analysis_type == AnalysisType::kMax && delay > arc.get_trans_delay_map()[analysis_type][input_trans_type])) {
      arc.get_trans_delay_map()[analysis_type][input_trans_type] = delay;
      arc.get_trans_type_map()[input_trans_type] = output_trans_type;
    }
  }
}

double DelayCalculator::calcArcSlew(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (arc.get_type() == ArcType::kNet) {
    return _wire.calcNetArcSlew(arc, analysis_type, output_trans_type, input_slew);
  }
  TimingCellArc* timing_cell_arc = _selection.cellArc(arc);
  if (timing_cell_arc != nullptr) {
    return _cell.calcTimingCellArcSlew(arc, *timing_cell_arc, analysis_type, input_trans_type, output_trans_type, input_slew);
  }
  return input_slew;
}

double DelayCalculator::calcArcDelay(Arc& arc)
{
  if (arc.get_type() == ArcType::kCell) {
    return calcCellArcDelay(arc, AnalysisType::kMax);
  }
  if (arc.get_type() == ArcType::kNet) {
    return _wire.calcNetArcDelay(arc);
  }
  return 0.0;
}

double DelayCalculator::calcCellArcDelay(Arc& arc, AnalysisType analysis_type)
{
  return calcCellArcDelay(arc, analysis_type, TransType::kRise);
}

double DelayCalculator::calcCellArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type)
{
  if (arc.get_type() == ArcType::kNet) {
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    return _wire.calcNetArcDelay(arc, analysis_type, input_trans_type);
  }
  TimingCellArc* timing_cell_arc = _selection.cellArc(arc);
  if (timing_cell_arc != nullptr) {
    return _cell.calcTimingCellArcDelay(arc, *timing_cell_arc, analysis_type, input_trans_type);
  }
  arc.get_trans_type_map()[input_trans_type] = input_trans_type;
  return 1.0;
}

double DelayCalculator::calcArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (arc.get_type() == ArcType::kNet) {
    return _wire.calcNetArcDelay(arc, analysis_type, output_trans_type, input_slew);
  }
  TimingCellArc* timing_cell_arc = _selection.cellArc(arc);
  if (timing_cell_arc != nullptr) {
    return _cell.calcTimingCellArcDelay(arc, *timing_cell_arc, analysis_type, input_trans_type, output_trans_type, input_slew);
  }
  if (arc.get_input_output_delay_map().count(analysis_type) > 0 && arc.get_input_output_delay_map()[analysis_type].count(input_trans_type) > 0
      && arc.get_input_output_delay_map()[analysis_type][input_trans_type].count(output_trans_type) > 0) {
    return arc.get_input_output_delay_map()[analysis_type][input_trans_type][output_trans_type];
  }
  if (arc.get_trans_delay_map().count(analysis_type) > 0 && arc.get_trans_delay_map()[analysis_type].count(input_trans_type) > 0) {
    return arc.get_trans_delay_map()[analysis_type][input_trans_type];
  }
  if (analysis_type == AnalysisType::kMin) {
    return arc.get_delay_min();
  }
  return arc.get_delay_max();
}

namespace {
std::unique_ptr<DelayCalculator> calculator;
}

void DelayCalculator::initInst()
{
  if (!calculator)
    calculator = std::make_unique<DelayCalculator>(STADM.getDatabase(), STADM.getConfig());
}
DelayCalculator& DelayCalculator::getInst()
{
  if (!calculator)
    throw std::logic_error("ArcDelayCalculator is not initialized");
  return *calculator;
}
void DelayCalculator::destroyInst()
{
  calculator.reset();
}

std::optional<DelayResult> DelayCalculator::arcDelay(Arc& arc, AnalysisType analysis, TransType input, TransType output, double slew,
                                                     std::optional<double> output_slew_input)
{
  TimingCellArc* cell_arc = _selection.cellArc(arc);
  if (cell_arc && !_selection.isClockArcTriggerTrans(*cell_arc, input))
    return std::nullopt;
  if (cell_arc && !cell_arc->get_timing_arc_list().empty() && _selection.cellCandidates(*cell_arc, input, output, arc.get_sink_pin()).empty())
    return std::nullopt;
  DelayResult result;
  result.set_output_trans_type(output);
  result.set_delay(calcArcDelay(arc, analysis, input, output, slew));
  result.set_slew(calcArcSlew(arc, analysis, input, output, output_slew_input.value_or(slew)));
  if (arc.rcFailed(analysis, output)) {
    // Keep the path, with the failed RC state still attached to the arc.
    // The zero delay is a placeholder, not a successful RC solution.
    result.set_delay(0.);
    result.set_slew(output_slew_input.value_or(slew));
  }
  return result;
}

std::optional<DelayResult> DelayCalculator::cellDelay(std::string& pin, TimingCellArc& arc, AnalysisType analysis, TransType input, TransType output,
                                                      double slew)
{
  if (!_selection.isClockArcTriggerTrans(arc, input))
    return std::nullopt;
  if (!arc.get_timing_arc_list().empty() && _selection.cellCandidates(arc, input, output, pin).empty())
    return std::nullopt;
  DelayResult result;
  result.set_output_trans_type(output);
  result.set_delay(_cell.calcTimingCellArcDelay(pin, arc, analysis, input, output, slew));
  result.set_slew(_cell.calcTimingCellArcSlew(pin, arc, analysis, input, output, slew));
  return result;
}

double DelayCalculator::checkDelay(TimingCheckArc& arc, AnalysisType analysis, TransType clock, TransType data, double clock_slew, double data_slew)
{
  return _check.calculate(arc, analysis, clock, data, clock_slew, data_slew);
}

}  // namespace ista
