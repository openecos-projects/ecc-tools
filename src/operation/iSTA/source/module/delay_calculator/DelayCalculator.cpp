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
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"

namespace ista {
void DelayCalculator::initialize(Graph& graph)
{
  auto measurement = _execution_stats.measure(TimingPhase::kDelayGraphInitialization);
  for (Arc& arc : graph.arcs()) {
    initialize(arc);
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
