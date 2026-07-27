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

#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCore>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ista {

// public

void DelayCalculator::initInst()
{
  if (_dc_instance == nullptr) {
    _dc_instance = new DelayCalculator();
  }
}

DelayCalculator& DelayCalculator::getInst()
{
  if (_dc_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dc_instance;
}

void DelayCalculator::destroyInst()
{
  if (_dc_instance != nullptr) {
    delete _dc_instance;
    _dc_instance = nullptr;
  }
}

// function

void DelayCalculator::init()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  clearParasiticCache();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DelayCalculator::calculate(DCTask& dc_task)
{
  dc_task.set_is_valid(false);
  if (dc_task.get_proc_type() == DCProcType::kInitialize) {
    initializeArcTiming(dc_task);
    return;
  }
  if (dc_task.get_proc_type() == DCProcType::kCalculate) {
    if (dc_task.get_arc() != nullptr) {
      calculateArc(dc_task);
      return;
    }
    if (dc_task.get_timing_cell_arc() != nullptr) {
      calculateTimingCellArc(dc_task);
      return;
    }
    if (dc_task.get_timing_check_arc() != nullptr) {
      calculateTimingCheckArc(dc_task);
    }
  }
}

void DelayCalculator::destroy()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  clearParasiticCache();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

double DelayCalculator::getPowerOutputLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type)
{
  return getOutputPinLoad(output_pin, analysis_type, output_trans_type);
}

// private

DelayCalculator* DelayCalculator::_dc_instance = nullptr;

void DelayCalculator::clearParasiticCache()
{
  _parasitic_resistor_map_cache.clear();
  _parasitic_load_map_cache.clear();
  _parasitic_delay_map_cache.clear();
  _parasitic_impulse_map_cache.clear();
  _parasitic_dmp_model_cache.clear();
  _parasitic_dmp_timing_result_cache.clear();
  _parasitic_dmp_driver_result_cache.clear();
  _parasitic_arnoldi_model_cache.clear();
  _parasitic_arnoldi_timing_result_cache.clear();
  _parasitic_arnoldi_driver_result_cache.clear();
}

void DelayCalculator::initializeArcTiming(DCTask& dc_task)
{
  Arc* arc = dc_task.get_arc();
  if (arc != nullptr) {
    initializeArcTiming(*arc);
    dc_task.set_is_valid(true);
  }
}

void DelayCalculator::initializeArcTiming(Arc& arc)
{
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
    double delay = calcNetArcDelay(arc, analysis_type, input_trans_type);
    arc.get_input_output_delay_map()[analysis_type][input_trans_type][input_trans_type] = delay;
    arc.get_trans_delay_map()[analysis_type][input_trans_type] = delay;
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    return;
  }

  TimingCellArc* timing_cell_arc = getTimingCellArc(arc);
  if (timing_cell_arc == nullptr || timing_cell_arc->get_timing_arc_list().empty()) {
    double delay = calcCellArcDelay(arc, analysis_type, input_trans_type);
    arc.get_input_output_delay_map()[analysis_type][input_trans_type][input_trans_type] = delay;
    arc.get_trans_delay_map()[analysis_type][input_trans_type] = delay;
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    return;
  }
  if (!isClockArcTriggerTrans(*timing_cell_arc, input_trans_type)) {
    return;
  }

  for (TransType output_trans_type : getOutputTransTypeList(*timing_cell_arc, input_trans_type)) {
    double delay = calcTimingCellArcDelay(arc, *timing_cell_arc, analysis_type, input_trans_type, output_trans_type, 0.0, true);
    arc.get_input_output_delay_map()[analysis_type][input_trans_type][output_trans_type] = delay;
    if (arc.get_trans_delay_map()[analysis_type].count(input_trans_type) == 0
        || (analysis_type == AnalysisType::kMin && delay < arc.get_trans_delay_map()[analysis_type][input_trans_type])
        || (analysis_type == AnalysisType::kMax && delay > arc.get_trans_delay_map()[analysis_type][input_trans_type])) {
      arc.get_trans_delay_map()[analysis_type][input_trans_type] = delay;
      arc.get_trans_type_map()[input_trans_type] = output_trans_type;
    }
  }
}

void DelayCalculator::calculateArc(DCTask& dc_task)
{
  Arc* arc = dc_task.get_arc();
  if (arc == nullptr) {
    return;
  }
  TimingCellArc* timing_cell_arc = getTimingCellArc(*arc);
  if (timing_cell_arc != nullptr && !isClockArcTriggerTrans(*timing_cell_arc, dc_task.get_input_trans_type())) {
    return;
  }

  DCTimingResult timing_result;
  timing_result.set_output_trans_type(dc_task.get_output_trans_type());
  timing_result.set_delay(calcArcDelay(*arc, dc_task.get_analysis_type(), dc_task.get_input_trans_type(), dc_task.get_output_trans_type(),
                                       dc_task.get_input_slew()));
  timing_result.set_slew(calcArcSlew(*arc, dc_task.get_analysis_type(), dc_task.get_input_trans_type(), dc_task.get_output_trans_type(),
                                     dc_task.get_input_slew()));
  dc_task.set_timing_result(timing_result);
  dc_task.set_is_valid(true);
}

void DelayCalculator::calculateTimingCellArc(DCTask& dc_task)
{
  TimingCellArc* timing_cell_arc = dc_task.get_timing_cell_arc();
  if (timing_cell_arc == nullptr || !isClockArcTriggerTrans(*timing_cell_arc, dc_task.get_input_trans_type())) {
    return;
  }

  DCTimingResult timing_result;
  timing_result.set_output_trans_type(dc_task.get_output_trans_type());
  timing_result.set_delay(calcTimingCellArcDelay(dc_task.get_output_pin(), *timing_cell_arc, dc_task.get_analysis_type(),
                                                 dc_task.get_input_trans_type(), dc_task.get_output_trans_type(), dc_task.get_input_slew()));
  timing_result.set_slew(calcTimingCellArcSlew(dc_task.get_output_pin(), *timing_cell_arc, dc_task.get_analysis_type(),
                                               dc_task.get_input_trans_type(), dc_task.get_output_trans_type(), dc_task.get_input_slew()));
  dc_task.set_timing_result(timing_result);
  dc_task.set_is_valid(true);
}

void DelayCalculator::calculateTimingCheckArc(DCTask& dc_task)
{
  TimingCheckArc* timing_check_arc = dc_task.get_timing_check_arc();
  if (timing_check_arc == nullptr) {
    return;
  }

  double check_time = timing_check_arc->get_check_time();
  std::vector<TimingArc*> candidate_arc_list
      = getCandidateTimingCheckArcList(*timing_check_arc, dc_task.get_clock_trans_type(), dc_task.get_data_trans_type());
  if (!candidate_arc_list.empty()) {
    std::vector<double> delay_list;
    for (TimingArc* timing_arc : candidate_arc_list) {
      if (timing_arc->get_check_table_map().count(dc_task.get_data_trans_type()) == 0) {
        continue;
      }
      double delay = timing_arc->get_check_table_map()[dc_task.get_data_trans_type()].findValue(
          dc_task.get_clock_slew() * timing_arc->get_time_unit_scale(), dc_task.get_data_slew() * timing_arc->get_time_unit_scale());
      delay_list.push_back(delay / timing_arc->get_time_unit_scale());
    }
    if (!delay_list.empty()) {
      std::ranges::sort(delay_list, std::greater<double>());
      check_time = dc_task.get_analysis_type() == AnalysisType::kMin ? delay_list.back() : delay_list.front();
    }
  }

  DCTimingResult timing_result;
  timing_result.set_delay(check_time);
  dc_task.set_timing_result(timing_result);
  dc_task.set_is_valid(true);
}

bool DelayCalculator::isClockArcTriggerTrans(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  if (!timing_cell_arc.get_is_clock_arc()) {
    return true;
  }
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return true;
  }
  TimingArc& timing_arc = timing_cell_arc.get_timing_arc_list().front();
  if (timing_arc.get_trigger_trans_type() == TransType::kFall) {
    return input_trans_type == TransType::kFall;
  }
  if (timing_arc.get_trigger_trans_type() == TransType::kRise) {
    return input_trans_type == TransType::kRise;
  }
  return input_trans_type == TransType::kRise;
}

double DelayCalculator::calcArcDelay(Arc& arc)
{
  if (arc.get_type() == ArcType::kCell) {
    return calcCellArcDelay(arc, AnalysisType::kMax);
  }
  if (arc.get_type() == ArcType::kNet) {
    return calcNetArcDelay(arc);
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
    return calcNetArcDelay(arc, analysis_type, input_trans_type);
  }
  TimingCellArc* timing_cell_arc = getTimingCellArc(arc);
  if (timing_cell_arc != nullptr) {
    return calcTimingCellArcDelay(arc, *timing_cell_arc, analysis_type, input_trans_type);
  }
  arc.get_trans_type_map()[input_trans_type] = input_trans_type;
  return 1.0;
}

TimingCellArc* DelayCalculator::getTimingCellArc(Arc& arc)
{
  Database& database = STADM.getDatabase();
  if (arc.get_timing_cell_arc() != nullptr) {
    return arc.get_timing_cell_arc();
  }
  if (database.get_instance_map().count(arc.get_owner_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.get_instance_map()[arc.get_owner_name()];
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_source_port() == arc.get_library_source_port() && timing_cell_arc.get_sink_port() == arc.get_library_sink_port()) {
      return &timing_cell_arc;
    }
  }
  return nullptr;
}

double DelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type)
{
  return calcTimingCellArcDelay(arc, timing_cell_arc, analysis_type, TransType::kRise);
}

double DelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                TransType input_trans_type)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    arc.get_trans_type_map()[input_trans_type] = input_trans_type;
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  TransType output_trans_type = getOutputTransType(timing_cell_arc, input_trans_type);
  arc.get_trans_type_map()[input_trans_type] = output_trans_type;
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double input_slew = 0.0;
  double raw_output_load = getArcOutputLoad(arc, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type)) {
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

double DelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                TransType input_trans_type, TransType output_trans_type)
{
  return calcTimingCellArcDelay(arc, timing_cell_arc, analysis_type, input_trans_type, output_trans_type, 0.0);
}

double DelayCalculator::calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                TransType input_trans_type, TransType output_trans_type, double input_slew,
                                                bool is_initialization)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double raw_output_load = getArcOutputLoad(arc, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type)) {
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

void DelayCalculator::updateTimingArcDelay(Arc& arc, TimingArc& timing_arc, AnalysisType analysis_type, TransType input_trans_type,
                                            TransType output_trans_type, double delay, bool is_initialization)
{
  arc.update_timing_arc_delay(timing_arc.get_arc_idx(), analysis_type, input_trans_type, output_trans_type, delay, is_initialization);
}

double DelayCalculator::calcTimingCellArcSlew(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                               TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = getArcOutputLoad(arc, analysis_type, output_trans_type);
  std::vector<TimingArc*> candidate_arc_list = getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type);
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

double DelayCalculator::calcTimingCellArcDelay(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                                TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type)) {
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

double DelayCalculator::calcTimingCellArcSlew(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                               TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  std::vector<TimingArc*> candidate_arc_list = getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type);
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

TransType DelayCalculator::getOutputTransType(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  if (isNegativeArc(timing_cell_arc)) {
    return input_trans_type == TransType::kRise ? TransType::kFall : TransType::kRise;
  }
  return input_trans_type;
}

std::vector<TransType> DelayCalculator::getOutputTransTypeList(TimingCellArc& timing_cell_arc, TransType input_trans_type)
{
  std::vector<TransType> output_trans_type_list;
  if (!isUnateArc(timing_cell_arc) || isTwoTypeSenseArcSet(timing_cell_arc) || timing_cell_arc.get_is_clock_arc()) {
    for (TransType output_trans_type : {TransType::kRise, TransType::kFall}) {
      if (isMatchTimingType(timing_cell_arc, output_trans_type)) {
        output_trans_type_list.push_back(output_trans_type);
      }
    }
    return output_trans_type_list;
  }

  TransType output_trans_type = getOutputTransType(timing_cell_arc, input_trans_type);
  if (isMatchTimingType(timing_cell_arc, output_trans_type)) {
    output_trans_type_list.push_back(output_trans_type);
  }
  return output_trans_type_list;
}

std::vector<TimingArc*> DelayCalculator::getCandidateTimingArcList(TimingCellArc& timing_cell_arc, TransType input_trans_type, TransType output_trans_type)
{
  bool is_flip = input_trans_type != output_trans_type;
  std::vector<TimingArc*> candidate_arc_list;
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (is_flip && isPositiveArc(timing_arc)) {
      continue;
    }
    if (!is_flip && isNegativeArc(timing_arc)) {
      continue;
    }
    if (!isMatchTimingType(timing_arc, output_trans_type)) {
      continue;
    }
    candidate_arc_list.push_back(&timing_arc);
  }
  return candidate_arc_list;
}

std::vector<TimingArc*> DelayCalculator::getCandidateTimingCheckArcList(TimingCheckArc& timing_check_arc, TransType clock_trans_type,
                                                                         TransType data_trans_type)
{
  std::vector<TimingArc*> candidate_arc_list;
  for (TimingArc& timing_arc : timing_check_arc.get_timing_arc_list()) {
    if (!isMatchTimingType(timing_arc, data_trans_type)) {
      continue;
    }
    candidate_arc_list.push_back(&timing_arc);
  }
  return candidate_arc_list;
}

bool DelayCalculator::isMatchTimingType(TimingArc& timing_arc, TransType trans_type)
{
  return timing_arc.get_delay_table_map().count(trans_type) > 0 || timing_arc.get_slew_table_map().count(trans_type) > 0
         || timing_arc.get_check_table_map().count(trans_type) > 0;
}

bool DelayCalculator::isPositiveArc(TimingArc& timing_arc)
{
  return timing_arc.get_sense() == TimingArcSense::kPositive || timing_arc.get_sense() == TimingArcSense::kNone;
}

bool DelayCalculator::isNegativeArc(TimingArc& timing_arc)
{
  return timing_arc.get_sense() == TimingArcSense::kNegative;
}

bool DelayCalculator::isUnateArc(TimingCellArc& timing_cell_arc)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (timing_arc.get_sense() == TimingArcSense::kNonUnate) {
      return false;
    }
  }
  return true;
}

bool DelayCalculator::isNegativeArc(TimingCellArc& timing_cell_arc)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (!isNegativeArc(timing_arc)) {
      return false;
    }
  }
  return true;
}

bool DelayCalculator::isTwoTypeSenseArcSet(TimingCellArc& timing_cell_arc)
{
  bool has_positive = false;
  bool has_negative = false;
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (isPositiveArc(timing_arc)) {
      has_positive = true;
    } else if (isNegativeArc(timing_arc)) {
      has_negative = true;
    }
  }
  return has_positive && has_negative;
}

bool DelayCalculator::isMatchTimingType(TimingCellArc& timing_cell_arc, TransType trans_type)
{
  for (TimingArc& timing_arc : timing_cell_arc.get_timing_arc_list()) {
    if (isMatchTimingType(timing_arc, trans_type)) {
      return true;
    }
  }
  return false;
}

double DelayCalculator::convertOutputLoad(TimingArc& timing_arc, double output_load)
{
  if (std::abs(timing_arc.get_cap_unit_scale() - 1.0) < STA_ERROR) {
    return output_load;
  }
  return static_cast<int>(std::ceil(output_load * timing_arc.get_cap_unit_scale()));
}

double DelayCalculator::calcTimingArcDelay(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                            double input_slew, double output_load)
{
  ParasiticArnoldiTimingResult& timing_result
      = getParasiticArnoldiTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (timing_result.get_is_valid()) {
    return timing_result.get_gate_delay();
  }
  ParasiticDmpTimingResult& dmp_timing_result
      = getParasiticDmpTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (dmp_timing_result.get_is_valid()) {
    return dmp_timing_result.get_gate_delay();
  }
  return calcTimingArcDelayByLoad(timing_arc, output_trans_type, input_slew, output_load);
}

double DelayCalculator::calcTimingArcSlew(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type,
                                           double input_slew, double output_load)
{
  ParasiticArnoldiTimingResult& timing_result
      = getParasiticArnoldiTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (timing_result.get_is_valid()) {
    return timing_result.get_driver_slew();
  }
  ParasiticDmpTimingResult& dmp_timing_result
      = getParasiticDmpTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  if (dmp_timing_result.get_is_valid()) {
    return dmp_timing_result.get_driver_slew();
  }
  return calcTimingArcSlewByLoad(timing_arc, output_trans_type, input_slew, output_load);
}

double DelayCalculator::calcTimingArcDelayByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_delay_table_map().count(output_trans_type) == 0) {
    return 0.0;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double delay = timing_arc.get_delay_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return delay / timing_arc.get_time_unit_scale();
}

double DelayCalculator::calcTimingArcSlewByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_slew_table_map().count(output_trans_type) == 0) {
    return input_slew;
  }
  double converted_output_load = convertOutputLoad(timing_arc, output_load);
  double slew = timing_arc.get_slew_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return slew / timing_arc.get_time_unit_scale();
}

double DelayCalculator::calcTimingArcDelayByRawLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_delay_table_map().count(output_trans_type) == 0) {
    return 0.0;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double delay = timing_arc.get_delay_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return delay / timing_arc.get_time_unit_scale();
}

double DelayCalculator::calcTimingArcSlewByRawLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load)
{
  if (timing_arc.get_slew_table_map().count(output_trans_type) == 0) {
    return input_slew;
  }
  double converted_output_load = output_load * timing_arc.get_cap_unit_scale();
  double slew = timing_arc.get_slew_table_map()[output_trans_type].findValue(input_slew * timing_arc.get_time_unit_scale(), converted_output_load);
  return slew / timing_arc.get_time_unit_scale();
}

double DelayCalculator::getArcOutputLoad(Arc& arc, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = STADM.getDatabase();
  std::string& sink_pin_name = arc.get_sink_pin();
  if (database.get_pin_map().count(sink_pin_name) == 0) {
    return 0.0;
  }
  Pin& sink_pin = database.get_pin_map()[sink_pin_name];
  if (sink_pin.get_net_name().empty() || database.get_net_map().count(sink_pin.get_net_name()) == 0) {
    return 0.0;
  }
  Net& net = database.get_net_map()[sink_pin.get_net_name()];
  return getNetOutputLoad(net, analysis_type, output_trans_type);
}

double DelayCalculator::getOutputPinLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(output_pin) == 0) {
    return 0.0;
  }
  Pin& pin = database.get_pin_map()[output_pin];
  if (pin.get_net_name().empty() || database.get_net_map().count(pin.get_net_name()) == 0) {
    return 0.0;
  }
  Net& net = database.get_net_map()[pin.get_net_name()];
  return getNetOutputLoad(net, analysis_type, output_trans_type);
}

double DelayCalculator::getNetOutputLoad(Net& net, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = STADM.getDatabase();
  if (database.get_parasitic_library().get_net_map().count(net.get_net_name()) > 0) {
    ParasiticNet& parasitic_net = database.get_parasitic_library().get_net_map()[net.get_net_name()];
    return getParasiticNetOutputLoad(net, parasitic_net, analysis_type, output_trans_type);
  }

  double output_load = 0.0;
  for (std::string& load_pin_name : net.get_load_pin_list()) {
    output_load += getPinCapacitance(load_pin_name, analysis_type, output_trans_type);
  }
  return output_load;
}

double DelayCalculator::getParasiticNetOutputLoad(Net& net, ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type)
{
  std::string source_node_name = getParasiticNodeName(parasitic_net, net.get_driver_pin());
  if (source_node_name.empty()) {
    return getParasiticTotalLoad(parasitic_net, analysis_type, trans_type);
  }

  buildParasiticDelayMap(parasitic_net, source_node_name, analysis_type, trans_type);
  return _parasitic_load_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type][source_node_name];
}

double DelayCalculator::getPinCapacitance(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.get_timing_constraint().get_port_constraint_map();
    if (port_constraint_map.count(pin_name) > 0 && port_constraint_map[pin_name].get_has_load()) {
      return port_constraint_map[pin_name].get_load();
    }
    return 0.0;
  }
  if (database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return 0.0;
  }
  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_port_map().count(pin.get_pin_name()) == 0) {
    return 0.0;
  }
  TimingCellPort& timing_cell_port = timing_cell.get_port_map()[pin.get_pin_name()];
  if (timing_cell_port.get_trans_capacitance_map().count(analysis_type) > 0
      && timing_cell_port.get_trans_capacitance_map()[analysis_type].count(trans_type) > 0) {
    return timing_cell_port.get_trans_capacitance_map()[analysis_type][trans_type];
  }
  return timing_cell_port.get_capacitance();
}

double DelayCalculator::calcNetArcDelay(Arc& arc)
{
  return calcNetArcDelay(arc, AnalysisType::kMax, TransType::kRise);
}

double DelayCalculator::calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type)
{
  return calcNetArcDelay(arc, analysis_type, trans_type, std::numeric_limits<double>::quiet_NaN());
}

double DelayCalculator::calcNetArcDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = STADM.getDatabase();
  if (database.get_parasitic_library().get_net_map().count(arc.get_owner_name()) > 0) {
    return calcParasiticDelay(arc, analysis_type, trans_type, input_slew);
  }
  return 0.0;
}

double DelayCalculator::calcParasiticDelay(Arc& arc)
{
  return calcParasiticDelay(arc, AnalysisType::kMax, TransType::kRise);
}

double DelayCalculator::calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type)
{
  return calcParasiticDelay(arc, analysis_type, trans_type, std::numeric_limits<double>::quiet_NaN());
}

double DelayCalculator::calcParasiticDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = STADM.getDatabase();
  ParasiticNet& parasitic_net = database.get_parasitic_library().get_net_map()[arc.get_owner_name()];
  std::string source_node_name = getParasiticNodeName(parasitic_net, arc.get_source_pin());
  std::string sink_node_name = getParasiticNodeName(parasitic_net, arc.get_sink_pin());
  if (source_node_name.empty() || sink_node_name.empty()) {
    double source_capacitance = getParasiticNodeCapacitance(parasitic_net, arc.get_source_pin());
    double sink_capacitance = getParasiticNodeCapacitance(parasitic_net, arc.get_sink_pin());
    double resistance = getParasiticTotalResistance(parasitic_net);
    return resistance * (source_capacitance + sink_capacitance) * 0.5 * 1E-3;
  }

  std::optional<double> cached_wire_delay = getParasiticArnoldiCachedWireDelay(arc, analysis_type, trans_type, input_slew);
  if (cached_wire_delay) {
    return *cached_wire_delay;
  }
  cached_wire_delay = getParasiticDmpCachedWireDelay(arc, analysis_type, trans_type, input_slew);
  if (cached_wire_delay) {
    return *cached_wire_delay;
  }
  std::optional<double> input_port_delay = calcParasiticArnoldiInputPortDelay(parasitic_net, source_node_name, sink_node_name, analysis_type, trans_type);
  if (input_port_delay) {
    return *input_port_delay;
  }

  buildParasiticDelayMap(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_delay_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type].count(sink_node_name) == 0) {
    return 0.0;
  }
  return _parasitic_delay_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type][sink_node_name];
}

double DelayCalculator::getParasiticNodeCapacitance(ParasiticNet& parasitic_net, std::string& pin_name)
{
  std::string spef_pin_name = pin_name;
  std::replace(spef_pin_name.begin(), spef_pin_name.end(), ':', '/');
  if (parasitic_net.get_node_map().count(spef_pin_name) > 0) {
    return parasitic_net.get_node_map()[spef_pin_name].get_capacitance();
  }
  if (parasitic_net.get_node_map().count(pin_name) > 0) {
    return parasitic_net.get_node_map()[pin_name].get_capacitance();
  }
  return parasitic_net.get_lumped_capacitance();
}

double DelayCalculator::getParasiticNodeLoad(ParasiticNet& parasitic_net, std::string& node_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = STADM.getDatabase();
  double node_load = 0.0;
  if (parasitic_net.get_node_map().count(node_name) > 0) {
    node_load += parasitic_net.get_node_map()[node_name].get_capacitance();
  }

  std::string pin_name = getPinNameByParasiticNodeName(node_name);
  if (database.get_pin_map().count(pin_name) > 0) {
    node_load += getPinCapacitance(pin_name, analysis_type, trans_type);
  }
  return node_load;
}

void DelayCalculator::buildParasiticDelayMap(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                              TransType trans_type)
{
  std::string& net_name = parasitic_net.get_net_name();
  if (_parasitic_delay_map_cache[net_name][analysis_type][trans_type].count(source_node_name) > 0
      && _parasitic_impulse_map_cache[net_name][analysis_type][trans_type].count(source_node_name) > 0) {
    return;
  }

  if (_parasitic_resistor_map_cache.count(net_name) == 0) {
    buildParasiticResistorMap(parasitic_net, _parasitic_resistor_map_cache[net_name]);
  }
  std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map = _parasitic_resistor_map_cache[net_name];

  std::string parent_node_name;
  std::set<std::string> load_visited_node_set;
  updateParasiticLoadMap(parasitic_net, source_node_name, parent_node_name, resistor_map, load_visited_node_set, analysis_type, trans_type);

  _parasitic_delay_map_cache[net_name][analysis_type][trans_type][source_node_name] = 0.0;
  std::set<std::string> delay_visited_node_set;
  updateParasiticDelayMap(parasitic_net, source_node_name, parent_node_name, resistor_map, delay_visited_node_set, analysis_type, trans_type);

  std::map<std::string, double> load_delay_map;
  std::set<std::string> load_delay_visited_node_set;
  updateParasiticLoadDelayMap(parasitic_net, source_node_name, parent_node_name, resistor_map, load_delay_visited_node_set, analysis_type, trans_type,
                              load_delay_map);

  std::map<std::string, double> beta_map;
  beta_map[source_node_name] = 0.0;
  std::set<std::string> impulse_visited_node_set;
  updateParasiticImpulseMap(parasitic_net, source_node_name, parent_node_name, resistor_map, impulse_visited_node_set, analysis_type, trans_type,
                            load_delay_map, beta_map);
}

double DelayCalculator::updateParasiticLoadMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                                                std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                                                std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type)
{
  if (visited_node_set.count(node_name) > 0) {
    return 0.0;
  }
  visited_node_set.insert(node_name);

  double subtree_load = getParasiticNodeLoad(parasitic_net, node_name, analysis_type, trans_type);
  if (resistor_map.count(node_name) == 0) {
    _parasitic_load_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type][node_name] = subtree_load;
    return subtree_load;
  }

  for (std::pair<std::string, double>& next_node_pair : resistor_map[node_name]) {
    if (next_node_pair.first == parent_node_name) {
      continue;
    }
    subtree_load += updateParasiticLoadMap(parasitic_net, next_node_pair.first, node_name, resistor_map, visited_node_set, analysis_type, trans_type);
  }
  _parasitic_load_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type][node_name] = subtree_load;
  return subtree_load;
}

void DelayCalculator::updateParasiticDelayMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                                               std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                                               std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type)
{
  if (visited_node_set.count(node_name) > 0) {
    return;
  }
  visited_node_set.insert(node_name);
  if (resistor_map.count(node_name) == 0) {
    return;
  }

  std::string& net_name = parasitic_net.get_net_name();
  for (std::pair<std::string, double>& next_node_pair : resistor_map[node_name]) {
    if (next_node_pair.first == parent_node_name || visited_node_set.count(next_node_pair.first) > 0) {
      continue;
    }
    double node_delay = _parasitic_delay_map_cache[net_name][analysis_type][trans_type][node_name];
    double next_node_load = _parasitic_load_map_cache[net_name][analysis_type][trans_type][next_node_pair.first];
    _parasitic_delay_map_cache[net_name][analysis_type][trans_type][next_node_pair.first] = node_delay + next_node_pair.second * next_node_load * 1E-3;
    updateParasiticDelayMap(parasitic_net, next_node_pair.first, node_name, resistor_map, visited_node_set, analysis_type, trans_type);
  }
}

double DelayCalculator::updateParasiticLoadDelayMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                                                     std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                                                     std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type,
                                                     std::map<std::string, double>& load_delay_map)
{
  if (visited_node_set.count(node_name) > 0) {
    return 0.0;
  }
  visited_node_set.insert(node_name);

  double load_delay = getParasiticNodeLoad(parasitic_net, node_name, analysis_type, trans_type)
                      * _parasitic_delay_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type][node_name];
  if (resistor_map.count(node_name) == 0) {
    load_delay_map[node_name] = load_delay;
    return load_delay;
  }

  for (std::pair<std::string, double>& next_node_pair : resistor_map[node_name]) {
    if (next_node_pair.first == parent_node_name) {
      continue;
    }
    load_delay += updateParasiticLoadDelayMap(parasitic_net, next_node_pair.first, node_name, resistor_map, visited_node_set, analysis_type, trans_type,
                                              load_delay_map);
  }
  load_delay_map[node_name] = load_delay;
  return load_delay;
}

void DelayCalculator::updateParasiticImpulseMap(ParasiticNet& parasitic_net, std::string& node_name, std::string& parent_node_name,
                                                 std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map,
                                                 std::set<std::string>& visited_node_set, AnalysisType analysis_type, TransType trans_type,
                                                 std::map<std::string, double>& load_delay_map, std::map<std::string, double>& beta_map)
{
  if (visited_node_set.count(node_name) > 0) {
    return;
  }
  visited_node_set.insert(node_name);

  std::string& net_name = parasitic_net.get_net_name();
  if (resistor_map.count(node_name) > 0) {
    for (std::pair<std::string, double>& next_node_pair : resistor_map[node_name]) {
      if (next_node_pair.first == parent_node_name || visited_node_set.count(next_node_pair.first) > 0) {
        continue;
      }
      beta_map[next_node_pair.first] = beta_map[node_name] + next_node_pair.second * load_delay_map[next_node_pair.first] * 1E-3;
      updateParasiticImpulseMap(parasitic_net, next_node_pair.first, node_name, resistor_map, visited_node_set, analysis_type, trans_type, load_delay_map,
                                beta_map);
    }
  }

  double node_delay = _parasitic_delay_map_cache[net_name][analysis_type][trans_type][node_name];
  double impulse = 2.0 * beta_map[node_name] - std::pow(node_delay, 2);
  _parasitic_impulse_map_cache[net_name][analysis_type][trans_type][node_name] = std::max(0.0, impulse);
}

double DelayCalculator::getParasiticTotalLoad(ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type)
{
  double total_load = 0.0;
  for (std::pair<const std::string, ParasiticNode>& node_pair : parasitic_net.get_node_map()) {
    std::string node_name = node_pair.first;
    total_load += getParasiticNodeLoad(parasitic_net, node_name, analysis_type, trans_type);
  }
  return total_load;
}

void DelayCalculator::buildParasiticResistorMap(ParasiticNet& parasitic_net,
                                                 std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map)
{
  resistor_map.clear();
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    resistor_map[parasitic_resistor.get_source_node()].push_back(
        std::make_pair(parasitic_resistor.get_sink_node(), parasitic_resistor.get_resistance()));
    resistor_map[parasitic_resistor.get_sink_node()].push_back(
        std::make_pair(parasitic_resistor.get_source_node(), parasitic_resistor.get_resistance()));
  }
}

std::string DelayCalculator::getParasiticNodeName(ParasiticNet& parasitic_net, std::string& pin_name)
{
  if (parasitic_net.get_node_map().count(pin_name) > 0) {
    return pin_name;
  }

  std::string spef_pin_name = pin_name;
  std::replace(spef_pin_name.begin(), spef_pin_name.end(), ':', '/');
  if (parasitic_net.get_node_map().count(spef_pin_name) > 0) {
    return spef_pin_name;
  }
  return "";
}

std::string DelayCalculator::getPinNameByParasiticNodeName(std::string& node_name)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(node_name) > 0) {
    return node_name;
  }

  std::string pin_name = node_name;
  std::replace(pin_name.begin(), pin_name.end(), '/', ':');
  if (database.get_pin_map().count(pin_name) > 0) {
    return pin_name;
  }
  return node_name;
}

ParasiticDmpTimingResult& DelayCalculator::getParasiticDmpTimingResult(std::string& output_pin, TimingArc& timing_arc,
                                                                       AnalysisType analysis_type, TransType output_trans_type,
                                                                       double input_slew, double output_load)
{
  std::string timing_result_key = getParasiticDmpTimingResultKey(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  if (_parasitic_dmp_timing_result_cache.count(timing_result_key) == 0) {
    _parasitic_dmp_timing_result_cache[timing_result_key]
        = calcParasiticDmpTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  }
  return _parasitic_dmp_timing_result_cache[timing_result_key];
}

std::string DelayCalculator::getParasiticDmpTimingResultKey(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type,
                                                             TransType output_trans_type, double input_slew)
{
  std::stringstream key_stream;
  key_stream << output_pin << "|" << reinterpret_cast<std::uintptr_t>(&timing_arc) << "|" << static_cast<int32_t>(analysis_type) << "|"
             << static_cast<int32_t>(output_trans_type) << "|" << std::setprecision(17) << input_slew;
  return key_stream.str();
}

ParasiticDmpTimingResult DelayCalculator::calcParasiticDmpTimingResult(std::string& output_pin, TimingArc& timing_arc,
                                                                       AnalysisType analysis_type, TransType output_trans_type,
                                                                       double input_slew, double output_load)
{
  ParasiticDmpTimingResult timing_result;
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(output_pin) == 0) {
    return timing_result;
  }
  Pin& output_pin_data = database.get_pin_map()[output_pin];
  if (output_pin_data.get_net_name().empty() || database.get_parasitic_library().get_net_map().count(output_pin_data.get_net_name()) == 0) {
    return timing_result;
  }

  ParasiticNet& parasitic_net = database.get_parasitic_library().get_net_map()[output_pin_data.get_net_name()];
  std::string source_node_name = getParasiticNodeName(parasitic_net, output_pin);
  if (source_node_name.empty()) {
    return timing_result;
  }

  ParasiticDmpModel& dmp_model = getParasiticDmpModel(parasitic_net, source_node_name, analysis_type, output_trans_type);
  if (!dmp_model.get_is_valid()) {
    return timing_result;
  }

  double gate_delay = 0.0;
  double driver_slew = 0.0;
  double effective_capacitance = output_load;
  if (!calcParasiticDmpCeff(timing_arc, output_trans_type, input_slew, dmp_model, gate_delay, driver_slew, effective_capacitance)) {
    return timing_result;
  }

  timing_result.set_is_valid(true);
  timing_result.set_effective_capacitance(effective_capacitance);
  timing_result.set_gate_delay(gate_delay);
  timing_result.set_driver_slew(driver_slew);
  timing_result.get_wire_delay_map()[source_node_name] = 0.0;
  timing_result.get_wire_delay_map()[output_pin] = 0.0;
  timing_result.get_load_slew_map()[source_node_name] = driver_slew;
  timing_result.get_load_slew_map()[output_pin] = driver_slew;

  for (std::pair<const std::string, ParasiticDmpLoadModel>& load_model_pair : dmp_model.get_load_model_map()) {
    std::string load_node_name = load_model_pair.first;
    std::string load_pin_name = getPinNameByParasiticNodeName(load_node_name);
    double wire_delay = 0.0;
    double load_slew = driver_slew;
    if (!calcParasiticDmpLoadDelay(load_model_pair.second, timing_arc, output_trans_type, driver_slew, wire_delay, load_slew)) {
      continue;
    }
    adjustParasiticLoadThreshold(timing_arc, load_pin_name, output_trans_type, wire_delay, load_slew);
    timing_result.get_wire_delay_map()[load_node_name] = wire_delay;
    timing_result.get_wire_delay_map()[load_pin_name] = wire_delay;
    timing_result.get_load_slew_map()[load_node_name] = load_slew;
    timing_result.get_load_slew_map()[load_pin_name] = load_slew;
  }
  cacheParasiticDmpDriverResult(output_pin, analysis_type, output_trans_type, driver_slew, timing_result);
  return timing_result;
}

bool DelayCalculator::calcParasiticDmpCeff(TimingArc& timing_arc, TransType trans_type, double input_slew, ParasiticDmpModel& dmp_model,
                                  double& gate_delay, double& driver_slew, double& effective_capacitance)
{
  if (!initParasiticDmpCeff(timing_arc, trans_type, input_slew, dmp_model)) {
    return false;
  }

  _driver_resistance = calcGateResistance();
  if (!std::isfinite(_driver_resistance) || _driver_resistance < 0.0) {
    return false;
  }
  if (_driver_resistance < kCapacitiveDriverResistance || _pi_resistance < _driver_resistance * 1E-3 || _load_capacitance == 0.0
      || _load_capacitance < _driver_capacitance * 1E-3 || _pi_resistance == 0.0) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }
  if (_driver_capacitance < _load_capacitance * 1E-3) {
    return calcZeroC2(gate_delay, driver_slew, effective_capacitance);
  }
  return calcPi(gate_delay, driver_slew, effective_capacitance);
}

bool DelayCalculator::initParasiticDmpCeff(TimingArc& timing_arc, TransType trans_type, double input_slew, ParasiticDmpModel& dmp_model)
{
  if (!dmp_model.get_is_valid() || timing_arc.get_delay_table_map().count(trans_type) == 0
      || timing_arc.get_slew_table_map().count(trans_type) == 0) {
    return false;
  }

  _timing_arc = &timing_arc;
  _trans_type = trans_type;
  _input_slew = input_slew;
  _driver_capacitance = dmp_model.get_driver_capacitance();
  _pi_resistance = dmp_model.get_pi_resistance();
  _load_capacitance = dmp_model.get_load_capacitance();
  _threshold = getNormalizedThreshold(trans_type == TransType::kFall ? timing_arc.get_output_threshold_pct_fall()
                                                                     : timing_arc.get_output_threshold_pct_rise());
  _lower_threshold = getNormalizedThreshold(trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall()
                                                                           : timing_arc.get_slew_lower_threshold_pct_rise());
  _upper_threshold = getNormalizedThreshold(trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall()
                                                                           : timing_arc.get_slew_upper_threshold_pct_rise());
  _slew_derate = timing_arc.get_slew_derate();
  _is_pi = false;
  _is_zero_c2 = false;
  _newton_order = 0;
  _parameter_list.fill(0.0);
  _function_list.fill(0.0);
  _scale_list.fill(0.0);
  _delta_list.fill(0.0);
  _index_list.fill(0);
  for (std::array<double, 3>& row : _jacobian) {
    row.fill(0.0);
  }
  return std::isfinite(_input_slew) && _driver_capacitance >= 0.0 && _pi_resistance >= 0.0 && _load_capacitance >= 0.0
         && _threshold > 0.0 && _threshold < 1.0 && _lower_threshold >= 0.0 && _upper_threshold <= 1.0
         && _upper_threshold > _lower_threshold && _slew_derate > 0.0;
}

double DelayCalculator::getNormalizedThreshold(double threshold)
{
  if (threshold > 1.0) {
    return threshold * 0.01;
  }
  return threshold;
}

double DelayCalculator::calcGateResistance()
{
  double capacitance1 = _driver_capacitance + _load_capacitance;
  double capacitance2 = capacitance1 + kGateResistanceCapacitanceStep;
  double delay1 = 0.0;
  double slew1 = 0.0;
  double delay2 = 0.0;
  double slew2 = 0.0;
  if (!getGateDelaySlew(capacitance1, delay1, slew1) || !getGateDelaySlew(capacitance2, delay2, slew2)) {
    return 0.0;
  }
  return -std::log(_threshold) * std::abs(delay1 - delay2) / (capacitance2 - capacitance1);
}

bool DelayCalculator::getGateDelaySlew(double capacitance, double& gate_delay, double& gate_slew)
{
  if (_timing_arc == nullptr || _timing_arc->get_delay_table_map().count(_trans_type) == 0
      || _timing_arc->get_slew_table_map().count(_trans_type) == 0) {
    return false;
  }
  double converted_slew = _input_slew * _timing_arc->get_time_unit_scale();
  double converted_capacitance = capacitance * _timing_arc->get_cap_unit_scale();
  gate_delay = _timing_arc->get_delay_table_map()[_trans_type].findValue(converted_slew, converted_capacitance)
               / _timing_arc->get_time_unit_scale();
  gate_slew = _timing_arc->get_slew_table_map()[_trans_type].findValue(converted_slew, converted_capacitance)
              / _timing_arc->get_time_unit_scale();
  return std::isfinite(gate_delay) && std::isfinite(gate_slew) && gate_slew >= 0.0;
}

bool DelayCalculator::calcCap(double& gate_delay, double& driver_slew, double& effective_capacitance)
{
  effective_capacitance = _driver_capacitance + _load_capacitance;
  return getGateDelaySlew(effective_capacitance, gate_delay, driver_slew);
}

bool DelayCalculator::calcPi(double& gate_delay, double& driver_slew, double& effective_capacitance)
{
  if (!initPi()) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }

  if (!findDriverParams(_driver_capacitance + _load_capacitance) && !findDriverParams(_driver_capacitance)) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }

  effective_capacitance = _parameter_list[kEffectiveCapacitanceIndex];
  double table_slew = 0.0;
  if (!getGateDelaySlew(effective_capacitance, gate_delay, table_slew)) {
    return false;
  }
  double waveform_delay = 0.0;
  if (!findDriverDelaySlew(waveform_delay, driver_slew)) {
    driver_slew = table_slew;
  }
  return std::isfinite(gate_delay) && std::isfinite(driver_slew);
}

bool DelayCalculator::initPi()
{
  _is_pi = true;
  _is_zero_c2 = false;
  _newton_order = 3;
  double denominator = _pi_resistance * _driver_resistance * _load_capacitance * _driver_capacitance;
  double coefficient = _driver_resistance * (_load_capacitance + _driver_capacitance) + _pi_resistance * _load_capacitance;
  double discriminant = coefficient * coefficient - 4.0 * denominator;
  if (!(denominator > 0.0) || discriminant < 0.0) {
    return false;
  }

  _pi_zero = 1.0 / (_pi_resistance * _load_capacitance);
  _pi_scale = 1.0 / (_driver_resistance * _driver_capacitance);
  double root = std::sqrt(discriminant);
  _pi_pole1 = (coefficient + root) / (2.0 * denominator);
  _pi_pole2 = (coefficient - root) / (2.0 * denominator);
  double pole_product = _pi_pole1 * _pi_pole2;
  if (!(_pi_pole1 > 0.0) || !(_pi_pole2 > 0.0) || !(pole_product > 0.0)
      || std::abs(_pi_pole2 - _pi_pole1) < kTinyNumber) {
    return false;
  }

  _pi_constant2 = _pi_zero / pole_product;
  _pi_constant1 = (1.0 - _pi_constant2 * (_pi_pole1 + _pi_pole2)) / pole_product;
  _pi_residue2 = (_pi_constant1 * _pi_pole1 + _pi_constant2) / (_pi_pole2 - _pi_pole1);
  _pi_residue1 = -_pi_constant1 - _pi_residue2;
  double current_zero = (_load_capacitance + _driver_capacitance)
                        / (_pi_resistance * _load_capacitance * _driver_capacitance);
  _pi_current_constant = current_zero / pole_product;
  _pi_current_residue1 = (current_zero - _pi_pole1) / (_pi_pole1 * (_pi_pole1 - _pi_pole2));
  _pi_current_residue2 = (current_zero - _pi_pole2) / (_pi_pole2 * (_pi_pole2 - _pi_pole1));
  return std::isfinite(_pi_residue1) && std::isfinite(_pi_residue2) && std::isfinite(_pi_current_constant)
         && std::isfinite(_pi_current_residue1) && std::isfinite(_pi_current_residue2);
}

bool DelayCalculator::calcZeroC2(double& gate_delay, double& driver_slew, double& effective_capacitance)
{
  if (!initZeroC2()) {
    return calcCap(gate_delay, driver_slew, effective_capacitance);
  }

  effective_capacitance = _load_capacitance;
  if (findDriverParams(effective_capacitance) && findDriverDelaySlew(gate_delay, driver_slew)) {
    return true;
  }
  return getGateDelaySlew(effective_capacitance, gate_delay, driver_slew);
}

bool DelayCalculator::initZeroC2()
{
  _is_pi = false;
  _is_zero_c2 = true;
  _newton_order = 2;
  if (!(_pi_resistance > 0.0) || !(_load_capacitance > 0.0) || !(_driver_resistance > 0.0)) {
    return false;
  }

  _zero_zero = 1.0 / (_pi_resistance * _load_capacitance);
  _zero_pole = 1.0 / (_load_capacitance * (_driver_resistance + _pi_resistance));
  _zero_scale = _zero_pole / _zero_zero;
  if (!(_zero_scale > 0.0) || !(_zero_pole > 0.0)) {
    return false;
  }
  _zero_constant2 = 1.0 / _zero_scale;
  _zero_constant1 = (_zero_pole - _zero_zero) / (_zero_pole * _zero_pole);
  _zero_residue = -_zero_constant1;
  return std::isfinite(_zero_constant1) && std::isfinite(_zero_constant2) && std::isfinite(_zero_residue);
}

bool DelayCalculator::findDriverParams(double effective_capacitance)
{
  if (_newton_order == 3) {
    _parameter_list[kEffectiveCapacitanceIndex] = effective_capacitance;
  }
  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(effective_capacitance, threshold_delay, lower_delay, measured_slew)) {
    return false;
  }

  double threshold_span = _upper_threshold - _lower_threshold;
  double transition_time = measured_slew / threshold_span;
  double start_time = threshold_delay + std::log(1.0 - _threshold) * _driver_resistance * effective_capacitance
                      - _threshold * transition_time;
  _parameter_list[kTransitionTimeIndex] = transition_time;
  _parameter_list[kStartTimeIndex] = start_time;
  if (!newtonRaphson()) {
    return false;
  }
  _start_time = _parameter_list[kStartTimeIndex];
  _transition_time = _parameter_list[kTransitionTimeIndex];
  return std::isfinite(_start_time) && std::isfinite(_transition_time) && _transition_time > 0.0;
}

bool DelayCalculator::getGateDelays(double effective_capacitance, double& threshold_delay, double& lower_delay, double& measured_slew)
{
  double table_slew = 0.0;
  if (!getGateDelaySlew(effective_capacitance, threshold_delay, table_slew)) {
    return false;
  }
  measured_slew = table_slew * _slew_derate;
  double threshold_span = _upper_threshold - _lower_threshold;
  if (!(threshold_span > 0.0)) {
    return false;
  }
  lower_delay = threshold_delay - measured_slew * (_threshold - _lower_threshold) / threshold_span;
  return measured_slew > 0.0;
}

bool DelayCalculator::newtonRaphson()
{
  for (int32_t iteration = 0; iteration < kMaxNewtonIteration; iteration++) {
    if (!evalDmpEqns()) {
      return false;
    }
    for (int32_t index = 0; index < _newton_order; index++) {
      _delta_list[index] = -_function_list[index];
    }
    if (!decomposeJacobian()) {
      return false;
    }
    solveJacobian();

    bool is_converged = true;
    for (int32_t index = 0; index < _newton_order; index++) {
      if (!std::isfinite(_delta_list[index])
          || std::abs(_delta_list[index]) > std::abs(_parameter_list[index]) * kDriverParameterTolerance) {
        is_converged = false;
      }
      _parameter_list[index] += _delta_list[index];
    }
    if (is_converged) {
      return evalDmpEqns();
    }
  }
  return false;
}

bool DelayCalculator::evalDmpEqns()
{
  if (_is_pi) {
    return evalPiEqns();
  }
  if (_is_zero_c2) {
    return evalOnePoleEqns();
  }
  return false;
}

bool DelayCalculator::evalPiEqns()
{
  double start_time = _parameter_list[kStartTimeIndex];
  double transition_time = _parameter_list[kTransitionTimeIndex];
  double effective_capacitance = _parameter_list[kEffectiveCapacitanceIndex];
  double total_capacitance = _load_capacitance + _driver_capacitance;
  if (!(effective_capacitance > 0.0) || effective_capacitance > total_capacitance || !(transition_time > 0.0)) {
    return false;
  }

  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(effective_capacitance, threshold_delay, lower_delay, measured_slew)) {
    return false;
  }
  double effective_capacitance_time = measured_slew / (_upper_threshold - _lower_threshold);
  effective_capacitance_time = std::min(effective_capacitance_time, 1.4 * transition_time);

  double threshold_voltage = 0.0;
  double lower_voltage = 0.0;
  calcCapacitiveWaveform(threshold_delay, start_time, transition_time, effective_capacitance, threshold_voltage);
  calcCapacitiveWaveform(lower_delay, start_time, transition_time, effective_capacitance, lower_voltage);
  _function_list[kCurrentIndex] = calcPiCurrentDifference(transition_time, effective_capacitance_time, effective_capacitance);
  _function_list[kThresholdVoltageIndex] = threshold_voltage - _threshold;
  _function_list[kLowerVoltageIndex] = lower_voltage - _lower_threshold;

  double exp_pole1 = calcDmpExp(-_pi_pole1 * transition_time);
  double exp_pole2 = calcDmpExp(-_pi_pole2 * transition_time);
  double exp_effective = calcDmpExp(-transition_time / (_driver_resistance * effective_capacitance));
  _jacobian[kCurrentIndex][kStartTimeIndex] = 0.0;
  _jacobian[kCurrentIndex][kTransitionTimeIndex]
      = (-_pi_current_constant * transition_time + _pi_current_residue1 * transition_time * exp_pole1
         - (2.0 * _pi_current_residue1 / _pi_pole1) * (1.0 - exp_pole1)
         + _pi_current_residue2 * transition_time * exp_pole2
         - (2.0 * _pi_current_residue2 / _pi_pole2) * (1.0 - exp_pole2)
         + _driver_resistance * effective_capacitance
               * (transition_time + transition_time * exp_effective
                  - 2.0 * _driver_resistance * effective_capacitance * (1.0 - exp_effective)))
        / (_driver_resistance * transition_time * transition_time * transition_time);
  _jacobian[kCurrentIndex][kEffectiveCapacitanceIndex]
      = (2.0 * _driver_resistance * effective_capacitance - transition_time
         - (2.0 * _driver_resistance * effective_capacitance + transition_time) * exp_effective)
        / (transition_time * transition_time);

  calcCapacitiveWaveformDerivative(lower_delay, start_time, transition_time, effective_capacitance,
                                   _jacobian[kLowerVoltageIndex][kStartTimeIndex],
                                   _jacobian[kLowerVoltageIndex][kTransitionTimeIndex],
                                   _jacobian[kLowerVoltageIndex][kEffectiveCapacitanceIndex]);
  calcCapacitiveWaveformDerivative(threshold_delay, start_time, transition_time, effective_capacitance,
                                   _jacobian[kThresholdVoltageIndex][kStartTimeIndex],
                                   _jacobian[kThresholdVoltageIndex][kTransitionTimeIndex],
                                   _jacobian[kThresholdVoltageIndex][kEffectiveCapacitanceIndex]);
  return std::isfinite(_function_list[kCurrentIndex]) && std::isfinite(_function_list[kThresholdVoltageIndex])
         && std::isfinite(_function_list[kLowerVoltageIndex]);
}

double DelayCalculator::calcPiCurrentDifference(double transition_time, double effective_capacitance_time, double effective_capacitance)
{
  double exp_pole1 = calcDmpExp(-_pi_pole1 * effective_capacitance_time);
  double exp_pole2 = calcDmpExp(-_pi_pole2 * effective_capacitance_time);
  double exp_effective = calcDmpExp(-effective_capacitance_time / (_driver_resistance * effective_capacitance));
  double pi_current = (_pi_current_constant * effective_capacitance_time
                       + (_pi_current_residue1 / _pi_pole1) * (1.0 - exp_pole1)
                       + (_pi_current_residue2 / _pi_pole2) * (1.0 - exp_pole2))
                      / (_driver_resistance * effective_capacitance_time * transition_time);
  double effective_current
      = (_driver_resistance * effective_capacitance * effective_capacitance_time
         - std::pow(_driver_resistance * effective_capacitance, 2) * (1.0 - exp_effective))
        / (_driver_resistance * effective_capacitance_time * transition_time);
  return pi_current - effective_current;
}

bool DelayCalculator::evalOnePoleEqns()
{
  double start_time = _parameter_list[kStartTimeIndex];
  double transition_time = _parameter_list[kTransitionTimeIndex];
  double threshold_delay = 0.0;
  double lower_delay = 0.0;
  double measured_slew = 0.0;
  if (!getGateDelays(_load_capacitance, threshold_delay, lower_delay, measured_slew) || !(transition_time > 0.0)) {
    return false;
  }

  double threshold_voltage = 0.0;
  double lower_voltage = 0.0;
  calcCapacitiveWaveform(threshold_delay, start_time, transition_time, _load_capacitance, threshold_voltage);
  calcCapacitiveWaveform(lower_delay, start_time, transition_time, _load_capacitance, lower_voltage);
  _function_list[kThresholdVoltageIndex] = threshold_voltage - _threshold;
  _function_list[kLowerVoltageIndex] = lower_voltage - _lower_threshold;

  double ignored_capacitance_derivative = 0.0;
  calcCapacitiveWaveformDerivative(lower_delay, start_time, transition_time, _load_capacitance,
                                   _jacobian[kLowerVoltageIndex][kStartTimeIndex],
                                   _jacobian[kLowerVoltageIndex][kTransitionTimeIndex], ignored_capacitance_derivative);
  calcCapacitiveWaveformDerivative(threshold_delay, start_time, transition_time, _load_capacitance,
                                   _jacobian[kThresholdVoltageIndex][kStartTimeIndex],
                                   _jacobian[kThresholdVoltageIndex][kTransitionTimeIndex], ignored_capacitance_derivative);
  return std::isfinite(_function_list[kThresholdVoltageIndex]) && std::isfinite(_function_list[kLowerVoltageIndex]);
}

void DelayCalculator::calcCapacitiveWaveform(double time, double start_time, double transition_time, double capacitance, double& voltage)
{
  double shifted_time = time - start_time;
  if (shifted_time <= 0.0) {
    voltage = 0.0;
  } else if (shifted_time <= transition_time) {
    voltage = calcCapacitiveUnitRamp(shifted_time, capacitance) / transition_time;
  } else {
    voltage = (calcCapacitiveUnitRamp(shifted_time, capacitance)
               - calcCapacitiveUnitRamp(shifted_time - transition_time, capacitance))
              / transition_time;
  }
}

double DelayCalculator::calcCapacitiveUnitRamp(double time, double capacitance)
{
  double time_constant = _driver_resistance * capacitance;
  if (!(time_constant > 0.0)) {
    return time;
  }
  return time - time_constant * (1.0 - calcDmpExp(-time / time_constant));
}

void DelayCalculator::calcCapacitiveWaveformDerivative(double time, double start_time, double transition_time, double capacitance,
                                                         double& start_derivative, double& transition_derivative,
                                                         double& capacitance_derivative)
{
  double shifted_time = time - start_time;
  if (shifted_time <= 0.0) {
    start_derivative = 0.0;
    transition_derivative = 0.0;
    capacitance_derivative = 0.0;
  } else if (shifted_time <= transition_time) {
    start_derivative = -calcCapacitiveUnitRampTimeDerivative(shifted_time, capacitance) / transition_time;
    transition_derivative = -calcCapacitiveUnitRamp(shifted_time, capacitance) / (transition_time * transition_time);
    capacitance_derivative = calcCapacitiveUnitRampCapDerivative(shifted_time, capacitance) / transition_time;
  } else {
    start_derivative = -(calcCapacitiveUnitRampTimeDerivative(shifted_time, capacitance)
                         - calcCapacitiveUnitRampTimeDerivative(shifted_time - transition_time, capacitance))
                       / transition_time;
    transition_derivative
        = -(calcCapacitiveUnitRamp(shifted_time, capacitance) + calcCapacitiveUnitRamp(shifted_time - transition_time, capacitance))
              / (transition_time * transition_time)
          + calcCapacitiveUnitRampTimeDerivative(shifted_time - transition_time, capacitance) / transition_time;
    capacitance_derivative = (calcCapacitiveUnitRampCapDerivative(shifted_time, capacitance)
                              - calcCapacitiveUnitRampCapDerivative(shifted_time - transition_time, capacitance))
                             / transition_time;
  }
}

double DelayCalculator::calcCapacitiveUnitRampTimeDerivative(double time, double capacitance)
{
  double time_constant = _driver_resistance * capacitance;
  if (!(time_constant > 0.0)) {
    return 1.0;
  }
  return 1.0 - calcDmpExp(-time / time_constant);
}

double DelayCalculator::calcCapacitiveUnitRampCapDerivative(double time, double capacitance)
{
  double time_constant = _driver_resistance * capacitance;
  if (!(time_constant > 0.0)) {
    return 0.0;
  }
  return _driver_resistance * ((1.0 + time / time_constant) * calcDmpExp(-time / time_constant) - 1.0);
}

bool DelayCalculator::decomposeJacobian()
{
  for (int32_t row = 0; row < _newton_order; row++) {
    double largest_value = 0.0;
    for (int32_t column = 0; column < _newton_order; column++) {
      largest_value = std::max(largest_value, std::abs(_jacobian[row][column]));
    }
    if (largest_value == 0.0 || !std::isfinite(largest_value)) {
      return false;
    }
    _scale_list[row] = 1.0 / largest_value;
  }

  int32_t last_index = _newton_order - 1;
  for (int32_t column = 0; column < _newton_order; column++) {
    for (int32_t row = 0; row < column; row++) {
      double value = _jacobian[row][column];
      for (int32_t index = 0; index < row; index++) {
        value -= _jacobian[row][index] * _jacobian[index][column];
      }
      _jacobian[row][column] = value;
    }

    double largest_value = 0.0;
    int32_t pivot_row = column;
    for (int32_t row = column; row < _newton_order; row++) {
      double value = _jacobian[row][column];
      for (int32_t index = 0; index < column; index++) {
        value -= _jacobian[row][index] * _jacobian[index][column];
      }
      _jacobian[row][column] = value;
      double scaled_value = _scale_list[row] * std::abs(value);
      if (scaled_value >= largest_value) {
        largest_value = scaled_value;
        pivot_row = row;
      }
    }
    if (column != pivot_row) {
      std::swap(_jacobian[pivot_row], _jacobian[column]);
      _scale_list[pivot_row] = _scale_list[column];
    }
    _index_list[column] = pivot_row;
    if (_jacobian[column][column] == 0.0) {
      _jacobian[column][column] = kTinyNumber;
    }
    if (column != last_index) {
      double inverse_pivot = 1.0 / _jacobian[column][column];
      for (int32_t row = column + 1; row < _newton_order; row++) {
        _jacobian[row][column] *= inverse_pivot;
      }
    }
  }
  return true;
}

void DelayCalculator::solveJacobian()
{
  int32_t first_nonzero = -1;
  for (int32_t row = 0; row < _newton_order; row++) {
    int32_t pivot_row = _index_list[row];
    double value = _delta_list[pivot_row];
    _delta_list[pivot_row] = _delta_list[row];
    if (first_nonzero != -1) {
      for (int32_t column = first_nonzero; column < row; column++) {
        value -= _jacobian[row][column] * _delta_list[column];
      }
    } else if (value != 0.0) {
      first_nonzero = row;
    }
    _delta_list[row] = value;
  }

  for (int32_t row = _newton_order - 1; row >= 0; row--) {
    double value = _delta_list[row];
    for (int32_t column = row + 1; column < _newton_order; column++) {
      value -= _jacobian[row][column] * _delta_list[column];
    }
    _delta_list[row] = value / _jacobian[row][row];
  }
}

bool DelayCalculator::findDriverDelaySlew(double& driver_delay, double& driver_slew)
{
  double upper_bound = getOutputCrossingUpperBound();
  double lower_crossing = 0.0;
  double upper_crossing = 0.0;
  if (!(upper_bound > _start_time) || !findOutputCrossing(_threshold, _start_time, upper_bound, driver_delay)
      || !findOutputCrossing(_lower_threshold, _start_time, driver_delay, lower_crossing)
      || !findOutputCrossing(_upper_threshold, driver_delay, upper_bound, upper_crossing)) {
    return false;
  }
  driver_slew = (upper_crossing - lower_crossing) / _slew_derate;
  return std::isfinite(driver_delay) && std::isfinite(driver_slew) && driver_slew >= 0.0;
}

bool DelayCalculator::findOutputCrossing(double threshold, double lower_time, double upper_time, double& crossing_time)
{
  std::function<void(double, double&, double&)> waveform_function = [this, threshold](double time, double& value, double& derivative) {
    calcOutputWaveform(time, value, derivative);
    value -= threshold;
  };
  return findRoot(waveform_function, lower_time, upper_time, crossing_time);
}

bool DelayCalculator::findRoot(std::function<void(double, double&, double&)>& function, double lower_time, double upper_time, double& root)
{
  double lower_value = 0.0;
  double upper_value = 0.0;
  double ignored_derivative = 0.0;
  function(lower_time, lower_value, ignored_derivative);
  function(upper_time, upper_value, ignored_derivative);
  if (!std::isfinite(lower_value) || !std::isfinite(upper_value) || lower_value * upper_value > 0.0) {
    return false;
  }
  if (lower_value == 0.0) {
    root = lower_time;
    return true;
  }
  if (upper_value == 0.0) {
    root = upper_time;
    return true;
  }
  if (lower_value > 0.0) {
    std::swap(lower_time, upper_time);
  }

  root = 0.5 * (lower_time + upper_time);
  double previous_delta = std::abs(upper_time - lower_time);
  double delta = previous_delta;
  double value = 0.0;
  double derivative = 0.0;
  function(root, value, derivative);
  for (int32_t iteration = 0; iteration < kMaxRootIteration; iteration++) {
    bool use_bisection = derivative == 0.0
                          || (((root - upper_time) * derivative - value) * ((root - lower_time) * derivative - value) > 0.0)
                          || std::abs(2.0 * value) > std::abs(previous_delta * derivative);
    if (use_bisection) {
      previous_delta = delta;
      delta = 0.5 * (upper_time - lower_time);
      root = lower_time + delta;
    } else {
      previous_delta = delta;
      delta = value / derivative;
      root -= delta;
    }
    if (std::abs(delta) <= kThresholdTimeTolerance * std::abs(root)) {
      return true;
    }

    function(root, value, derivative);
    if (!std::isfinite(value) || !std::isfinite(derivative)) {
      return false;
    }
    if (value < 0.0) {
      lower_time = root;
    } else {
      upper_time = root;
    }
  }
  return false;
}

void DelayCalculator::calcOutputWaveform(double time, double& voltage, double& derivative)
{
  double shifted_time = time - _start_time;
  if (shifted_time <= 0.0) {
    voltage = 0.0;
    derivative = 0.0;
    return;
  }

  double unit_voltage = 0.0;
  double unit_derivative = 0.0;
  if (shifted_time <= _transition_time) {
    if (_is_pi) {
      calcPiUnitRamp(shifted_time, unit_voltage, unit_derivative);
    } else {
      calcZeroC2UnitRamp(shifted_time, unit_voltage, unit_derivative);
    }
    voltage = unit_voltage / _transition_time;
    derivative = unit_derivative / _transition_time;
    return;
  }

  double delayed_voltage = 0.0;
  double delayed_derivative = 0.0;
  if (_is_pi) {
    calcPiUnitRamp(shifted_time, unit_voltage, unit_derivative);
    calcPiUnitRamp(shifted_time - _transition_time, delayed_voltage, delayed_derivative);
  } else {
    calcZeroC2UnitRamp(shifted_time, unit_voltage, unit_derivative);
    calcZeroC2UnitRamp(shifted_time - _transition_time, delayed_voltage, delayed_derivative);
  }
  voltage = (unit_voltage - delayed_voltage) / _transition_time;
  derivative = (unit_derivative - delayed_derivative) / _transition_time;
}

void DelayCalculator::calcPiUnitRamp(double time, double& voltage, double& derivative)
{
  double exp_pole1 = calcDmpExp(-_pi_pole1 * time);
  double exp_pole2 = calcDmpExp(-_pi_pole2 * time);
  voltage = _pi_scale * (_pi_constant1 + _pi_constant2 * time + _pi_residue1 * exp_pole1 + _pi_residue2 * exp_pole2);
  derivative = _pi_scale
               * (_pi_constant2 - _pi_residue1 * _pi_pole1 * exp_pole1 - _pi_residue2 * _pi_pole2 * exp_pole2);
}

void DelayCalculator::calcZeroC2UnitRamp(double time, double& voltage, double& derivative)
{
  double exp_pole = calcDmpExp(-_zero_pole * time);
  voltage = _zero_scale * (_zero_constant1 + _zero_constant2 * time + _zero_residue * exp_pole);
  derivative = _zero_scale * (_zero_constant2 - _zero_residue * _zero_pole * exp_pole);
}

double DelayCalculator::getOutputCrossingUpperBound()
{
  if (_is_pi) {
    return _start_time + _transition_time
           + (_load_capacitance + _driver_capacitance) * (_driver_resistance + _pi_resistance) * 2.0;
  }
  if (_is_zero_c2) {
    return _start_time + _transition_time + _load_capacitance * (_driver_resistance + _pi_resistance) * 2.0;
  }
  return _start_time + _transition_time;
}

double DelayCalculator::calcDmpExp(double value)
{
  if (value < -12.0) {
    return 0.0;
  }
  double result = 1.0 + value / 4096.0;
  for (int32_t iteration = 0; iteration < 12; iteration++) {
    result *= result;
  }
  return result;
}


void DelayCalculator::cacheParasiticDmpDriverResult(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type,
                                                     double driver_slew, ParasiticDmpTimingResult& timing_result)
{
  std::string driver_result_key = getParasiticDmpDriverResultKey(output_pin, analysis_type, output_trans_type, driver_slew);
  _parasitic_dmp_driver_result_cache[driver_result_key] = timing_result;
}

std::string DelayCalculator::getParasiticDmpDriverResultKey(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type,
                                                             double driver_slew)
{
  std::stringstream key_stream;
  key_stream << output_pin << "|" << static_cast<int32_t>(analysis_type) << "|" << static_cast<int32_t>(output_trans_type) << "|"
             << std::setprecision(17) << driver_slew;
  return key_stream.str();
}

std::optional<double> DelayCalculator::getParasiticDmpCachedWireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type,
                                                                      double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  std::string driver_result_key = getParasiticDmpDriverResultKey(source_pin, analysis_type, trans_type, input_slew);
  if (_parasitic_dmp_driver_result_cache.count(driver_result_key) == 0) {
    return std::nullopt;
  }
  ParasiticDmpTimingResult& timing_result = _parasitic_dmp_driver_result_cache[driver_result_key];
  std::string& sink_pin = arc.get_sink_pin();
  if (timing_result.get_wire_delay_map().count(sink_pin) == 0) {
    return std::nullopt;
  }
  return timing_result.get_wire_delay_map()[sink_pin];
}

std::optional<double> DelayCalculator::getParasiticDmpCachedLoadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type,
                                                                     double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  std::string driver_result_key = getParasiticDmpDriverResultKey(source_pin, analysis_type, trans_type, input_slew);
  if (_parasitic_dmp_driver_result_cache.count(driver_result_key) == 0) {
    return std::nullopt;
  }
  ParasiticDmpTimingResult& timing_result = _parasitic_dmp_driver_result_cache[driver_result_key];
  std::string& sink_pin = arc.get_sink_pin();
  if (timing_result.get_load_slew_map().count(sink_pin) == 0) {
    return std::nullopt;
  }
  return timing_result.get_load_slew_map()[sink_pin];
}

ParasiticDmpModel& DelayCalculator::getParasiticDmpModel(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                          AnalysisType analysis_type, TransType trans_type)
{
  std::string dmp_model_key = getParasiticDmpModelKey(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_dmp_model_cache.count(dmp_model_key) == 0) {
    _parasitic_dmp_model_cache[dmp_model_key] = buildParasiticDmpModel(parasitic_net, source_node_name, analysis_type, trans_type);
  }
  return _parasitic_dmp_model_cache[dmp_model_key];
}

std::string DelayCalculator::getParasiticDmpModelKey(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                      TransType trans_type)
{
  std::stringstream key_stream;
  key_stream << parasitic_net.get_net_name() << "|" << source_node_name << "|" << static_cast<int32_t>(analysis_type) << "|"
             << static_cast<int32_t>(trans_type);
  return key_stream.str();
}

ParasiticDmpModel DelayCalculator::buildParasiticDmpModel(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                           AnalysisType analysis_type, TransType trans_type)
{
  ParasiticDmpModel dmp_model;
  std::vector<std::string> node_name_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  initParasiticArnoldiTree(parasitic_net, source_node_name, analysis_type, trans_type, node_name_list, parent_idx_list, resistance_list,
                           capacitance_list);
  if (node_name_list.empty()) {
    return dmp_model;
  }

  buildParasiticDmpPiModel(dmp_model, parent_idx_list, resistance_list, capacitance_list);
  if (!dmp_model.get_is_valid()) {
    return dmp_model;
  }
  buildParasiticDmpLoadModelList(dmp_model, node_name_list, parent_idx_list, resistance_list, capacitance_list, source_node_name);
  return dmp_model;
}

void DelayCalculator::buildParasiticDmpPiModel(ParasiticDmpModel& dmp_model, std::vector<int32_t>& parent_idx_list,
                                                std::vector<double>& resistance_list, std::vector<double>& capacitance_list)
{
  if (parent_idx_list.size() != resistance_list.size() || resistance_list.size() != capacitance_list.size() || capacitance_list.empty()) {
    return;
  }

  std::vector<double> admittance1_list = capacitance_list;
  std::vector<double> admittance2_list(capacitance_list.size(), 0.0);
  std::vector<double> admittance3_list(capacitance_list.size(), 0.0);
  for (std::size_t node_idx = capacitance_list.size() - 1; node_idx > 0; node_idx--) {
    int32_t parent_idx = parent_idx_list[node_idx];
    if (parent_idx < 0) {
      return;
    }
    double resistance = resistance_list[node_idx];
    double admittance1 = admittance1_list[node_idx];
    double admittance2 = admittance2_list[node_idx];
    double admittance3 = admittance3_list[node_idx];
    admittance1_list[parent_idx] += admittance1;
    admittance2_list[parent_idx] += admittance2 - resistance * admittance1 * admittance1;
    admittance3_list[parent_idx]
        += admittance3 - 2.0 * resistance * admittance1 * admittance2 + resistance * resistance * admittance1 * admittance1 * admittance1;
  }

  double admittance1 = admittance1_list.front();
  double admittance2 = admittance2_list.front();
  double admittance3 = admittance3_list.front();
  double driver_capacitance = 0.0;
  double pi_resistance = 0.0;
  double load_capacitance = 0.0;
  if (admittance2 == 0.0 && admittance3 == 0.0) {
    load_capacitance = admittance1;
  } else {
    if (admittance3 == 0.0 || admittance2 == 0.0) {
      return;
    }
    load_capacitance = admittance2 * admittance2 / admittance3;
    driver_capacitance = admittance1 - load_capacitance;
    pi_resistance = -admittance3 * admittance3 / (admittance2 * admittance2 * admittance2);
  }
  if (!std::isfinite(driver_capacitance) || !std::isfinite(pi_resistance) || !std::isfinite(load_capacitance)
      || driver_capacitance < 0.0 || pi_resistance < 0.0 || load_capacitance < 0.0
      || driver_capacitance + load_capacitance <= 0.0) {
    return;
  }

  dmp_model.set_is_valid(true);
  dmp_model.set_driver_capacitance(driver_capacitance);
  dmp_model.set_pi_resistance(pi_resistance);
  dmp_model.set_load_capacitance(load_capacitance);
}

void DelayCalculator::buildParasiticDmpLoadModelList(ParasiticDmpModel& dmp_model, std::vector<std::string>& node_name_list,
                                                      std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                                      std::vector<double>& capacitance_list, std::string& source_node_name)
{
  Database& database = STADM.getDatabase();
  std::vector<std::vector<double>> moment_list(4, std::vector<double>(node_name_list.size(), 0.0));
  for (int32_t moment_idx = 1; moment_idx < 4; moment_idx++) {
    std::vector<double> branch_current_list(node_name_list.size(), 0.0);
    for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
      double previous_moment = moment_idx == 1 ? 1.0 : moment_list[moment_idx - 1][node_idx];
      branch_current_list[node_idx] = capacitance_list[node_idx] * previous_moment;
    }
    for (std::size_t node_idx = node_name_list.size() - 1; node_idx > 0; node_idx--) {
      int32_t parent_idx = parent_idx_list[node_idx];
      if (parent_idx < 0) {
        return;
      }
      branch_current_list[parent_idx] += branch_current_list[node_idx];
    }
    moment_list[moment_idx].front() = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_name_list.size(); node_idx++) {
      int32_t parent_idx = parent_idx_list[node_idx];
      moment_list[moment_idx][node_idx] = moment_list[moment_idx][parent_idx] - resistance_list[node_idx] * branch_current_list[node_idx];
    }
  }

  for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
    std::string& node_name = node_name_list[node_idx];
    if (node_name == source_node_name) {
      continue;
    }
    std::string pin_name = getPinNameByParasiticNodeName(node_name);
    if (database.get_pin_map().count(pin_name) == 0) {
      continue;
    }
    ParasiticDmpLoadModel load_model
        = buildParasiticDmpLoadModel(moment_list[1][node_idx], moment_list[2][node_idx], moment_list[3][node_idx]);
    if (load_model.get_is_valid()) {
      dmp_model.get_load_model_map()[node_name] = load_model;
    }
  }
}

ParasiticDmpLoadModel DelayCalculator::buildParasiticDmpLoadModel(double moment1, double moment2, double moment3)
{
  ParasiticDmpLoadModel load_model;
  if (!std::isfinite(moment1) || !std::isfinite(moment2) || !std::isfinite(moment3) || moment1 == 0.0) {
    return load_model;
  }

  double pole1 = moment3 == 0.0 ? 0.0 : -moment2 / moment3;
  double ratio_denominator = moment2 == 0.0 || moment3 == 0.0 ? 0.0 : moment1 / moment2 - moment2 / moment3;
  double pole2 = 0.0;
  if (pole1 > 0.0 && ratio_denominator != 0.0 && moment2 != 0.0) {
    pole2 = pole1 * (1.0 / moment1 - moment1 / moment2) / ratio_denominator;
  }
  if (!(pole1 > 0.0) || !(pole2 > 0.0) || pole1 == pole2 || ratio_denominator == 0.0 || !std::isfinite(pole1)
      || !std::isfinite(pole2)) {
    pole1 = -1.0 / moment1;
    if (!(pole1 > 0.0) || !std::isfinite(pole1)) {
      return load_model;
    }
    load_model.set_is_valid(true);
    load_model.get_pole_list().push_back(pole1);
    load_model.get_residue_list().push_back(1.0);
    return load_model;
  }

  double residue1 = pole1 * pole1 * (1.0 + moment1 * pole2) / (pole1 - pole2);
  double residue2 = -pole2 * pole2 * (1.0 + moment1 * pole1) / (pole1 - pole2);
  if (!std::isfinite(residue1) || !std::isfinite(residue2)) {
    return load_model;
  }
  if (residue1 < 0.0 && residue2 > 0.0) {
    std::swap(pole1, pole2);
    std::swap(residue1, residue2);
  }
  load_model.set_is_valid(true);
  load_model.get_pole_list().push_back(pole1);
  load_model.get_pole_list().push_back(pole2);
  load_model.get_residue_list().push_back(residue1);
  load_model.get_residue_list().push_back(residue2);
  return load_model;
}

bool DelayCalculator::calcParasiticDmpLoadDelay(ParasiticDmpLoadModel& load_model, TimingArc& timing_arc, TransType trans_type,
                                                 double driver_slew, double& wire_delay, double& load_slew)
{
  if (!load_model.get_is_valid() || load_model.get_pole_list().empty() || load_model.get_residue_list().empty()) {
    return false;
  }
  double pole1 = load_model.get_pole_list().front();
  if (!(pole1 > 0.0)) {
    return false;
  }
  wire_delay = 1.0 / pole1;
  load_slew = driver_slew;
  if (load_model.get_pole_list().size() < 2 || load_model.get_residue_list().size() < 2 || std::abs(driver_slew) <= STA_ERROR) {
    return true;
  }

  double pole2 = load_model.get_pole_list()[1];
  double residue1 = load_model.get_residue_list()[0];
  double residue2 = load_model.get_residue_list()[1];
  if (!(pole2 > 0.0)) {
    return true;
  }
  double threshold = getNormalizedThreshold(timing_arc.get_output_threshold_pct_rise());
  double lower_threshold = getNormalizedThreshold(timing_arc.get_slew_lower_threshold_pct_rise());
  double upper_threshold = getNormalizedThreshold(timing_arc.get_slew_upper_threshold_pct_rise());
  if (trans_type == TransType::kFall) {
    threshold = getNormalizedThreshold(timing_arc.get_output_threshold_pct_fall());
    lower_threshold = getNormalizedThreshold(timing_arc.get_slew_lower_threshold_pct_fall());
    upper_threshold = getNormalizedThreshold(timing_arc.get_slew_upper_threshold_pct_fall());
  }
  double slew_derate = timing_arc.get_slew_derate();
  if (!(upper_threshold > lower_threshold) || !(slew_derate > 0.0)) {
    return true;
  }

  double residue_pole1 = residue1 / (pole1 * pole1);
  double residue_pole2 = residue2 / (pole2 * pole2);
  double constant = residue_pole1 + residue_pole2;
  double transition_time = driver_slew * slew_derate / (upper_threshold - lower_threshold);
  if (!(transition_time > 0.0)) {
    return true;
  }
  double transition_voltage = (transition_time - constant + residue_pole1 * std::exp(-pole1 * transition_time)
                               + residue_pole2 * std::exp(-pole2 * transition_time))
                              / transition_time;
  double threshold_time = calcParasiticDmpLoadTime(threshold, pole1, pole2, residue1, residue2, constant, residue_pole1,
                                                   residue_pole2, transition_time, transition_voltage);
  double lower_time = calcParasiticDmpLoadTime(lower_threshold, pole1, pole2, residue1, residue2, constant, residue_pole1,
                                               residue_pole2, transition_time, transition_voltage);
  double upper_time = calcParasiticDmpLoadTime(upper_threshold, pole1, pole2, residue1, residue2, constant, residue_pole1,
                                               residue_pole2, transition_time, transition_voltage);
  double dmp_wire_delay = threshold_time - transition_time * threshold;
  double dmp_load_slew = (upper_time - lower_time) / slew_derate;
  if (std::isfinite(dmp_wire_delay) && std::isfinite(dmp_load_slew) && dmp_load_slew >= 0.0) {
    wire_delay = dmp_wire_delay;
    load_slew = dmp_load_slew;
  }
  return true;
}

double DelayCalculator::calcParasiticDmpLoadTime(double threshold, double pole1, double pole2, double residue1, double residue2,
                                                  double constant, double residue_pole1, double residue_pole2, double transition_time,
                                                  double transition_voltage)
{
  if (transition_voltage < threshold) {
    double logarithm_argument
        = residue1 * (std::exp(pole1 * transition_time) - 1.0) / ((1.0 - threshold) * pole1 * pole1 * transition_time);
    if (!(logarithm_argument > 0.0)) {
      return transition_time * threshold;
    }
    double time = std::log(logarithm_argument) / pole1;
    double exp_pole1_time = std::exp(-pole1 * time);
    double exp_pole2_time = std::exp(-pole2 * time);
    double exp_pole1_delta = std::exp(-pole1 * (time - transition_time));
    double exp_pole2_delta = std::exp(-pole2 * (time - transition_time));
    double voltage = (transition_time - residue_pole1 * (exp_pole1_delta - exp_pole1_time)
                      - residue_pole2 * (exp_pole2_delta - exp_pole2_time))
                     / transition_time;
    double derivative = (residue1 / pole1 * (exp_pole1_delta - exp_pole1_time)
                         - residue2 / pole2 * (exp_pole2_delta - exp_pole2_time))
                        / transition_time;
    if (std::abs(derivative) <= STA_ERROR) {
      return time;
    }
    return time - (voltage - threshold) / derivative;
  }

  double time = threshold * transition_time / transition_voltage;
  double exp_pole1_time = std::exp(-pole1 * time);
  double exp_pole2_time = std::exp(-pole2 * time);
  double voltage = (time - constant + residue_pole1 * exp_pole1_time + residue_pole2 * exp_pole1_time) / transition_time;
  double derivative = (1.0 - residue1 / pole1 * exp_pole1_time - residue2 / pole2 * exp_pole2_time) / transition_time;
  if (std::abs(derivative) <= STA_ERROR) {
    return time;
  }
  return time - (voltage - threshold) / derivative;
}

ParasiticArnoldiTimingResult& DelayCalculator::getParasiticArnoldiTimingResult(std::string& output_pin, TimingArc& timing_arc,
                                                                                AnalysisType analysis_type, TransType output_trans_type,
                                                                                double input_slew, double output_load)
{
  ParasiticArnoldiTimingResultKey timing_result_key
      = getParasiticArnoldiTimingResultKey(output_pin, timing_arc, analysis_type, output_trans_type, input_slew);
  if (_parasitic_arnoldi_timing_result_cache.count(timing_result_key) == 0) {
    _parasitic_arnoldi_timing_result_cache[timing_result_key]
        = calcParasiticArnoldiTimingResult(output_pin, timing_arc, analysis_type, output_trans_type, input_slew, output_load);
  }
  return _parasitic_arnoldi_timing_result_cache[timing_result_key];
}

ParasiticArnoldiTimingResultKey DelayCalculator::getParasiticArnoldiTimingResultKey(std::string& output_pin, TimingArc& timing_arc,
                                                                                     AnalysisType analysis_type, TransType output_trans_type,
                                                                                     double input_slew)
{
  return std::make_tuple(output_pin, reinterpret_cast<std::uintptr_t>(&timing_arc), analysis_type, output_trans_type, input_slew);
}

ParasiticArnoldiTimingResult DelayCalculator::calcParasiticArnoldiTimingResult(std::string& output_pin, TimingArc& timing_arc,
                                                                                AnalysisType analysis_type, TransType output_trans_type,
                                                                                double input_slew, double output_load)
{
  ParasiticArnoldiTimingResult timing_result;
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(output_pin) == 0) {
    return timing_result;
  }
  Pin& output_pin_data = database.get_pin_map()[output_pin];
  if (output_pin_data.get_net_name().empty() || database.get_parasitic_library().get_net_map().count(output_pin_data.get_net_name()) == 0) {
    return timing_result;
  }

  ParasiticNet& parasitic_net = database.get_parasitic_library().get_net_map()[output_pin_data.get_net_name()];
  std::string source_node_name = getParasiticNodeName(parasitic_net, output_pin);
  if (source_node_name.empty()) {
    return timing_result;
  }

  ParasiticArnoldiModel& arnoldi_model = getParasiticArnoldiModel(parasitic_net, source_node_name, analysis_type, output_trans_type);
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0 || arnoldi_model.get_total_capacitance() <= 0.0) {
    return timing_result;
  }
  double total_capacitance = arnoldi_model.get_total_capacitance();
  double delay_at_total_capacitance = calcTimingArcDelayByRawLoad(timing_arc, output_trans_type, input_slew, total_capacitance);
  double delay_at_half_capacitance = calcTimingArcDelayByRawLoad(timing_arc, output_trans_type, input_slew, 0.5 * total_capacitance);
  double delay_resistance = 0.0;
  if (total_capacitance > STA_ERROR) {
    delay_resistance = (delay_at_total_capacitance - delay_at_half_capacitance) / (0.5 * total_capacitance);
  }
  if (!(delay_resistance > 0.0)) {
    delay_resistance = 1.0;
  }

  double slew_derate = 1.0;
  double lower_threshold = 0.1;
  double upper_threshold = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  calcParasiticArnoldiThreshold(timing_arc, output_trans_type, slew_derate, lower_threshold, upper_threshold, voltage_log, min_slew_factor, x1,
                                y1);

  double drive_resistance
      = calcParasiticArnoldiTableResistance(timing_arc, output_trans_type, input_slew, total_capacitance, voltage_log, slew_derate, delay_resistance);
  if (!(drive_resistance > 0.0 && drive_resistance < 100.0)) {
    drive_resistance = 1.0;
  }
  bool bad_drive_resistance = drive_resistance < delay_resistance;
  double driver_ramp = calcParasiticArnoldiTableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, total_capacitance,
                                                     lower_threshold, upper_threshold, voltage_log, slew_derate, min_slew_factor);
  if (!(driver_ramp > 0.0 && driver_ramp < 100.0)) {
    driver_ramp = 0.5;
  }

  ParasiticArnoldiPoleResidue pole_residue = calcParasiticArnoldiPoleResidue(arnoldi_model, drive_resistance);
  if (!pole_residue.get_is_valid()) {
    return timing_result;
  }

  double effective_capacitance = total_capacitance;
  if (!bad_drive_resistance) {
    for (int32_t iter = 0; iter < 3; iter++) {
      effective_capacitance = calcParasiticArnoldiEffectiveCapacitance(driver_ramp, drive_resistance, pole_residue, driver_ramp);
      if (!std::isfinite(effective_capacitance) || effective_capacitance < 1E-8) {
        effective_capacitance = total_capacitance;
      }
      driver_ramp = calcParasiticArnoldiTableRamp(timing_arc, output_trans_type, input_slew, drive_resistance, effective_capacitance,
                                                  lower_threshold, upper_threshold, voltage_log, slew_derate, min_slew_factor);
      if (!(driver_ramp > 0.0 && driver_ramp < 100.0)) {
        driver_ramp = 0.5;
      }
    }
  }

  double table_delay = calcTimingArcDelayByRawLoad(timing_arc, output_trans_type, input_slew, effective_capacitance);
  double reference_time = solveParasiticArnoldiRampTime(1.0 / (drive_resistance * effective_capacitance), driver_ramp, 0.5);
  std::vector<double> delay_list;
  std::vector<double> slew_list;
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    double upper_time = 0.0;
    double middle_time = 0.0;
    double lower_time = 0.0;
    solveParasiticArnoldiWaveformTime(driver_ramp, pole_residue, term_idx, upper_threshold, upper_time, 0.5, middle_time, lower_threshold, lower_time);
    delay_list.push_back(middle_time + table_delay - reference_time);
    slew_list.push_back((lower_time - upper_time) / slew_derate);
  }
  if (delay_list.empty() || slew_list.empty()) {
    return timing_result;
  }

  timing_result.set_is_valid(true);
  timing_result.set_effective_capacitance(effective_capacitance);
  timing_result.set_gate_delay(delay_list.front());
  timing_result.set_driver_slew(slew_list.front());
  for (std::size_t term_idx = 0; term_idx < arnoldi_model.get_term_node_list().size(); term_idx++) {
    std::string& term_node_name = arnoldi_model.get_term_node_list()[term_idx];
    double wire_delay = delay_list[term_idx] - delay_list.front();
    double load_slew = slew_list[term_idx];
    std::string pin_name = getPinNameByParasiticNodeName(term_node_name);
    if (term_idx > 0) {
      adjustParasiticLoadThreshold(timing_arc, pin_name, output_trans_type, wire_delay, load_slew);
    }
    timing_result.get_wire_delay_map()[term_node_name] = wire_delay;
    timing_result.get_load_slew_map()[term_node_name] = load_slew;
    timing_result.get_wire_delay_map()[pin_name] = wire_delay;
    timing_result.get_load_slew_map()[pin_name] = load_slew;
  }
  cacheParasiticArnoldiDriverResult(output_pin, analysis_type, output_trans_type, timing_result.get_driver_slew(), timing_result);
  return timing_result;
}

void DelayCalculator::adjustParasiticLoadThreshold(TimingArc& timing_arc, std::string& load_pin, TransType trans_type, double& wire_delay,
                                                    double& load_slew)
{
  TimingCell* load_timing_cell = getThresholdTimingCell(load_pin);
  if (load_timing_cell == nullptr || load_timing_cell->get_library_name() == timing_arc.get_library_name()) {
    return;
  }

  double driver_output_threshold = trans_type == TransType::kFall ? timing_arc.get_output_threshold_pct_fall()
                                                                  : timing_arc.get_output_threshold_pct_rise();
  double driver_slew_lower_threshold = trans_type == TransType::kFall ? timing_arc.get_slew_lower_threshold_pct_fall()
                                                                      : timing_arc.get_slew_lower_threshold_pct_rise();
  double driver_slew_upper_threshold = trans_type == TransType::kFall ? timing_arc.get_slew_upper_threshold_pct_fall()
                                                                      : timing_arc.get_slew_upper_threshold_pct_rise();
  double driver_slew_derate = timing_arc.get_slew_derate();
  double load_input_threshold = getTimingCellInputThreshold(*load_timing_cell, trans_type);
  double load_slew_lower_threshold = getTimingCellSlewLowerThreshold(*load_timing_cell, trans_type);
  double load_slew_upper_threshold = getTimingCellSlewUpperThreshold(*load_timing_cell, trans_type);
  double load_slew_derate = load_timing_cell->get_slew_derate_from_library();

  driver_output_threshold = getNormalizedThreshold(driver_output_threshold);
  driver_slew_lower_threshold = getNormalizedThreshold(driver_slew_lower_threshold);
  driver_slew_upper_threshold = getNormalizedThreshold(driver_slew_upper_threshold);
  load_input_threshold = getNormalizedThreshold(load_input_threshold);
  load_slew_lower_threshold = getNormalizedThreshold(load_slew_lower_threshold);
  load_slew_upper_threshold = getNormalizedThreshold(load_slew_upper_threshold);

  double driver_slew_delta = driver_slew_upper_threshold - driver_slew_lower_threshold;
  double load_slew_delta = load_slew_upper_threshold - load_slew_lower_threshold;
  if (!(driver_slew_delta > STA_ERROR && load_slew_delta > STA_ERROR && driver_slew_derate > STA_ERROR && load_slew_derate > STA_ERROR)) {
    return;
  }

  double wire_delay_delta = load_slew * ((load_input_threshold - driver_output_threshold) / driver_slew_delta);
  if (trans_type == TransType::kFall) {
    wire_delay -= wire_delay_delta;
  } else {
    wire_delay += wire_delay_delta;
  }
  load_slew = load_slew * ((load_slew_delta / load_slew_derate) / (driver_slew_delta / driver_slew_derate));
}

TimingCell* DelayCalculator::getThresholdTimingCell(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(pin_name) == 0) {
    return nullptr;
  }
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
    return nullptr;
  }
  return &database.get_timing_library().get_cell_map()[instance.get_cell_name()];
}

double DelayCalculator::getTimingCellSlewLowerThreshold(TimingCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_slew_lower_threshold_pct_fall();
  }
  return timing_cell.get_slew_lower_threshold_pct_rise();
}

double DelayCalculator::getTimingCellSlewUpperThreshold(TimingCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_slew_upper_threshold_pct_fall();
  }
  return timing_cell.get_slew_upper_threshold_pct_rise();
}

double DelayCalculator::getTimingCellInputThreshold(TimingCell& timing_cell, TransType trans_type)
{
  if (trans_type == TransType::kFall) {
    return timing_cell.get_input_threshold_pct_fall();
  }
  return timing_cell.get_input_threshold_pct_rise();
}

void DelayCalculator::cacheParasiticArnoldiDriverResult(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type,
                                                        double driver_slew, ParasiticArnoldiTimingResult& timing_result)
{
  ParasiticArnoldiDriverResultKey driver_result_key
      = getParasiticArnoldiDriverResultKey(output_pin, analysis_type, output_trans_type, driver_slew);
  _parasitic_arnoldi_driver_result_cache[driver_result_key] = timing_result;
}

ParasiticArnoldiDriverResultKey DelayCalculator::getParasiticArnoldiDriverResultKey(std::string& output_pin, AnalysisType analysis_type,
                                                                                     TransType output_trans_type, double driver_slew)
{
  return std::make_tuple(output_pin, analysis_type, output_trans_type, driver_slew);
}

std::optional<double> DelayCalculator::getParasiticArnoldiCachedWireDelay(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  ParasiticArnoldiDriverResultKey driver_result_key = getParasiticArnoldiDriverResultKey(source_pin, analysis_type, trans_type, input_slew);
  if (_parasitic_arnoldi_driver_result_cache.count(driver_result_key) == 0) {
    return std::nullopt;
  }
  ParasiticArnoldiTimingResult& timing_result = _parasitic_arnoldi_driver_result_cache[driver_result_key];
  std::string& sink_pin = arc.get_sink_pin();
  if (timing_result.get_wire_delay_map().count(sink_pin) == 0) {
    return std::nullopt;
  }
  return timing_result.get_wire_delay_map()[sink_pin];
}

std::optional<double> DelayCalculator::getParasiticArnoldiCachedLoadSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  if (!std::isfinite(input_slew)) {
    return std::nullopt;
  }
  std::string& source_pin = arc.get_source_pin();
  ParasiticArnoldiDriverResultKey driver_result_key = getParasiticArnoldiDriverResultKey(source_pin, analysis_type, trans_type, input_slew);
  if (_parasitic_arnoldi_driver_result_cache.count(driver_result_key) == 0) {
    return std::nullopt;
  }
  ParasiticArnoldiTimingResult& timing_result = _parasitic_arnoldi_driver_result_cache[driver_result_key];
  std::string& sink_pin = arc.get_sink_pin();
  if (timing_result.get_load_slew_map().count(sink_pin) == 0) {
    return std::nullopt;
  }
  return timing_result.get_load_slew_map()[sink_pin];
}

ParasiticArnoldiModel& DelayCalculator::getParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                  AnalysisType analysis_type, TransType trans_type)
{
  ParasiticArnoldiModelKey arnoldi_model_key = getParasiticArnoldiModelKey(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_arnoldi_model_cache.count(arnoldi_model_key) == 0) {
    _parasitic_arnoldi_model_cache[arnoldi_model_key] = buildParasiticArnoldiModel(parasitic_net, source_node_name, analysis_type, trans_type);
  }
  return _parasitic_arnoldi_model_cache[arnoldi_model_key];
}

ParasiticArnoldiModelKey DelayCalculator::getParasiticArnoldiModelKey(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                       AnalysisType analysis_type, TransType trans_type)
{
  return std::make_tuple(parasitic_net.get_net_name(), source_node_name, analysis_type, trans_type);
}

ParasiticArnoldiModel DelayCalculator::buildParasiticArnoldiModel(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                   AnalysisType analysis_type, TransType trans_type)
{
  ParasiticArnoldiModel arnoldi_model;
  std::vector<std::string> node_name_list;
  std::vector<int32_t> parent_idx_list;
  std::vector<double> resistance_list;
  std::vector<double> capacitance_list;
  initParasiticArnoldiTree(parasitic_net, source_node_name, analysis_type, trans_type, node_name_list, parent_idx_list, resistance_list, capacitance_list);
  if (node_name_list.empty()) {
    return arnoldi_model;
  }

  initParasiticArnoldiTerm(arnoldi_model, node_name_list, source_node_name);
  if (arnoldi_model.get_term_node_list().empty()) {
    return arnoldi_model;
  }

  std::vector<std::size_t> term_point_idx_list;
  for (std::string& term_node_name : arnoldi_model.get_term_node_list()) {
    for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
      if (node_name_list[node_idx] == term_node_name) {
        term_point_idx_list.push_back(node_idx);
        break;
      }
    }
  }
  if (term_point_idx_list.size() != arnoldi_model.get_term_node_list().size()) {
    return arnoldi_model;
  }

  updateParasiticArnoldiModel(arnoldi_model, parasitic_net, node_name_list, parent_idx_list, resistance_list, capacitance_list,
                              term_point_idx_list);
  return arnoldi_model;
}

void DelayCalculator::initParasiticArnoldiTree(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type,
                                                TransType trans_type, std::vector<std::string>& node_name_list,
                                                std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                                                std::vector<double>& capacitance_list)
{
  std::string& net_name = parasitic_net.get_net_name();
  if (_parasitic_resistor_map_cache.count(net_name) == 0) {
    buildParasiticResistorMap(parasitic_net, _parasitic_resistor_map_cache[net_name]);
  }
  std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map = _parasitic_resistor_map_cache[net_name];

  std::set<std::string> visited_node_set;
  node_name_list.push_back(source_node_name);
  parent_idx_list.push_back(-1);
  resistance_list.push_back(0.0);
  capacitance_list.push_back(getParasiticNodeLoad(parasitic_net, source_node_name, analysis_type, trans_type));
  visited_node_set.insert(source_node_name);

  std::vector<std::size_t> stack_node_idx_list;
  std::vector<std::size_t> stack_next_edge_idx_list;
  stack_node_idx_list.push_back(0);
  stack_next_edge_idx_list.push_back(0);
  while (!stack_node_idx_list.empty()) {
    std::size_t node_idx = stack_node_idx_list.back();
    std::string& node_name = node_name_list[node_idx];
    if (resistor_map.count(node_name) == 0) {
      stack_node_idx_list.pop_back();
      stack_next_edge_idx_list.pop_back();
      continue;
    }

    std::vector<std::pair<std::string, double>>& edge_list = resistor_map[node_name];
    std::size_t& edge_idx = stack_next_edge_idx_list.back();
    if (edge_idx >= edge_list.size()) {
      stack_node_idx_list.pop_back();
      stack_next_edge_idx_list.pop_back();
      continue;
    }

    std::pair<std::string, double>& next_node_pair = edge_list[edge_idx];
    edge_idx++;
    std::string& next_node_name = next_node_pair.first;
    if (visited_node_set.count(next_node_name) > 0) {
      continue;
    }

    visited_node_set.insert(next_node_name);
    node_name_list.push_back(next_node_name);
    parent_idx_list.push_back(static_cast<int32_t>(node_idx));
    resistance_list.push_back(next_node_pair.second * 1E-3);
    capacitance_list.push_back(getParasiticNodeLoad(parasitic_net, next_node_name, analysis_type, trans_type));
    stack_node_idx_list.push_back(node_name_list.size() - 1);
    stack_next_edge_idx_list.push_back(0);
  }
}

void DelayCalculator::initParasiticArnoldiTerm(ParasiticArnoldiModel& arnoldi_model, std::vector<std::string>& node_name_list,
                                                std::string& source_node_name)
{
  Database& database = STADM.getDatabase();
  arnoldi_model.get_term_node_list().push_back(source_node_name);
  arnoldi_model.get_term_pin_list().push_back(getPinNameByParasiticNodeName(source_node_name));
  arnoldi_model.get_term_index_map()[source_node_name] = 0;
  arnoldi_model.get_term_index_map()[arnoldi_model.get_term_pin_list().front()] = 0;
  for (std::string& node_name : node_name_list) {
    if (node_name == source_node_name) {
      continue;
    }
    std::string pin_name = getPinNameByParasiticNodeName(node_name);
    if (database.get_pin_map().count(pin_name) == 0) {
      continue;
    }
    std::size_t term_idx = arnoldi_model.get_term_node_list().size();
    arnoldi_model.get_term_node_list().push_back(node_name);
    arnoldi_model.get_term_pin_list().push_back(pin_name);
    arnoldi_model.get_term_index_map()[node_name] = term_idx;
    arnoldi_model.get_term_index_map()[pin_name] = term_idx;
  }
}

void DelayCalculator::updateParasiticArnoldiModel(ParasiticArnoldiModel& arnoldi_model, ParasiticNet& parasitic_net,
                                                   std::vector<std::string>& node_name_list, std::vector<int32_t>& parent_idx_list,
                                                   std::vector<double>& resistance_list, std::vector<double>& capacitance_list,
                                                   std::vector<std::size_t>& term_point_idx_list)
{
  double total_capacitance = 0.0;
  for (double capacitance : capacitance_list) {
    total_capacitance += capacitance;
  }
  if (total_capacitance <= 0.0) {
    return;
  }

  std::size_t node_num = capacitance_list.size();
  int32_t max_order = 5;
  int32_t order = std::min(static_cast<int32_t>(node_num), max_order);
  double sqrt_total_capacitance = std::sqrt(total_capacitance);
  std::vector<double> current_basis_list(node_num, 1.0 / sqrt_total_capacitance);
  std::vector<double> previous_basis_list(node_num, 0.0);
  std::vector<double> diagonal_list(order, 0.0);
  std::vector<double> off_diagonal_list(order, 0.0);
  std::vector<std::vector<double>> projection_list(order, std::vector<double>(term_point_idx_list.size(), 0.0));
  int32_t final_order = order;

  std::map<std::string, std::size_t> node_idx_map;
  for (std::size_t node_idx = 0; node_idx < node_name_list.size(); node_idx++) {
    node_idx_map[node_name_list[node_idx]] = node_idx;
  }
  std::size_t network_resistance_num = 0;
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    if (node_idx_map.count(parasitic_resistor.get_source_node()) == 0 || node_idx_map.count(parasitic_resistor.get_sink_node()) == 0
        || parasitic_resistor.get_source_node() == parasitic_resistor.get_sink_node()) {
      continue;
    }
    network_resistance_num++;
  }

  bool has_resistance_loop = network_resistance_num + 1 > node_num;
  Eigen::SparseMatrix<double> conductance_matrix;
  Eigen::ConjugateGradient<Eigen::SparseMatrix<double>, Eigen::Lower | Eigen::Upper, Eigen::IncompleteCholesky<double>> conductance_solver;
  if (has_resistance_loop) {
    std::vector<Eigen::Triplet<double>> conductance_triplet_list;
    for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
      if (node_idx_map.count(parasitic_resistor.get_source_node()) == 0 || node_idx_map.count(parasitic_resistor.get_sink_node()) == 0) {
        continue;
      }
      std::size_t source_idx = node_idx_map[parasitic_resistor.get_source_node()];
      std::size_t sink_idx = node_idx_map[parasitic_resistor.get_sink_node()];
      if (source_idx == sink_idx) {
        continue;
      }
      double conductance = 1.0 / (parasitic_resistor.get_resistance() * 1E-3);
      if (source_idx > 0) {
        conductance_triplet_list.emplace_back(source_idx - 1, source_idx - 1, conductance);
      }
      if (sink_idx > 0) {
        conductance_triplet_list.emplace_back(sink_idx - 1, sink_idx - 1, conductance);
      }
      if (source_idx > 0 && sink_idx > 0) {
        conductance_triplet_list.emplace_back(source_idx - 1, sink_idx - 1, -conductance);
        conductance_triplet_list.emplace_back(sink_idx - 1, source_idx - 1, -conductance);
      }
    }
    conductance_matrix.resize(node_num - 1, node_num - 1);
    conductance_matrix.setFromTriplets(conductance_triplet_list.begin(), conductance_triplet_list.end());
    conductance_solver.compute(conductance_matrix);
    has_resistance_loop = conductance_solver.info() == Eigen::Success;
  }

  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    updateParasiticArnoldiProjection(arnoldi_model, current_basis_list, term_point_idx_list, order_idx);
    if (arnoldi_model.get_projection_list().empty()) {
      arnoldi_model.set_projection_list(projection_list);
    } else {
      projection_list = arnoldi_model.get_projection_list();
    }

    std::vector<double> response_list(node_num, 0.0);
    if (has_resistance_loop) {
      Eigen::VectorXd current_load_vector(node_num - 1);
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        current_load_vector[node_idx - 1] = capacitance_list[node_idx] * current_basis_list[node_idx];
      }
      Eigen::VectorXd response_vector = conductance_solver.solve(current_load_vector);
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        response_list[node_idx] = response_vector[node_idx - 1];
      }
    } else {
      std::vector<double> current_load_list(node_num, 0.0);
      for (int32_t node_idx = static_cast<int32_t>(node_num) - 1; node_idx > 0; node_idx--) {
        current_load_list[node_idx] += capacitance_list[node_idx] * current_basis_list[node_idx];
        current_load_list[parent_idx_list[node_idx]] += current_load_list[node_idx];
      }
      current_load_list[0] += capacitance_list[0] * current_basis_list[0];
      for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
        response_list[node_idx] = response_list[parent_idx_list[node_idx]] + resistance_list[node_idx] * current_load_list[node_idx];
      }
    }

    double diagonal = 0.0;
    for (std::size_t node_idx = 1; node_idx < node_num; node_idx++) {
      diagonal += current_basis_list[node_idx] * capacitance_list[node_idx] * response_list[node_idx];
    }
    diagonal_list[order_idx] = diagonal;
    if (order_idx == order - 1) {
      break;
    }
    if (diagonal < STA_ERROR) {
      final_order = order_idx + 1;
      break;
    }

    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      response_list[node_idx] -= diagonal * current_basis_list[node_idx];
      if (order_idx > 0) {
        response_list[node_idx] -= off_diagonal_list[order_idx - 1] * previous_basis_list[node_idx];
      }
    }

    double off_diagonal = 0.0;
    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      off_diagonal += capacitance_list[node_idx] * response_list[node_idx] * response_list[node_idx];
    }
    if (off_diagonal < STA_ERROR * STA_ERROR) {
      final_order = order_idx + 1;
      break;
    }
    off_diagonal = std::sqrt(off_diagonal);
    off_diagonal_list[order_idx] = off_diagonal;

    previous_basis_list = current_basis_list;
    for (std::size_t node_idx = 0; node_idx < node_num; node_idx++) {
      current_basis_list[node_idx] = response_list[node_idx] / off_diagonal;
    }
  }

  diagonal_list.resize(final_order);
  if (final_order > 1) {
    off_diagonal_list.resize(final_order - 1);
  } else {
    off_diagonal_list.clear();
  }
  projection_list.resize(final_order);
  arnoldi_model.set_is_valid(final_order > 0);
  arnoldi_model.set_order(final_order);
  arnoldi_model.set_total_capacitance(total_capacitance);
  arnoldi_model.set_sqrt_total_capacitance(sqrt_total_capacitance);
  arnoldi_model.set_diagonal_list(diagonal_list);
  arnoldi_model.set_off_diagonal_list(off_diagonal_list);
  arnoldi_model.set_projection_list(projection_list);
}

void DelayCalculator::updateParasiticArnoldiProjection(ParasiticArnoldiModel& arnoldi_model, std::vector<double>& basis_list,
                                                        std::vector<std::size_t>& term_point_idx_list, std::size_t order_idx)
{
  if (arnoldi_model.get_projection_list().size() <= order_idx) {
    arnoldi_model.get_projection_list().resize(order_idx + 1);
  }
  arnoldi_model.get_projection_list()[order_idx].clear();
  for (std::size_t term_point_idx : term_point_idx_list) {
    arnoldi_model.get_projection_list()[order_idx].push_back(basis_list[term_point_idx]);
  }
}

double DelayCalculator::calcParasiticArnoldiElmore(ParasiticArnoldiModel& arnoldi_model, std::string& sink_node_name)
{
  if (arnoldi_model.get_term_index_map().count(sink_node_name) == 0) {
    return 0.0;
  }
  return calcParasiticArnoldiElmore(arnoldi_model, arnoldi_model.get_term_index_map()[sink_node_name]);
}

double DelayCalculator::calcParasiticArnoldiElmore(ParasiticArnoldiModel& arnoldi_model, std::size_t term_idx)
{
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0 || arnoldi_model.get_diagonal_list().empty()) {
    return 0.0;
  }
  if (arnoldi_model.get_order() == 1 || arnoldi_model.get_projection_list().size() < 2 || arnoldi_model.get_off_diagonal_list().empty()) {
    return arnoldi_model.get_diagonal_list().front();
  }
  if (term_idx >= arnoldi_model.get_projection_list().front().size() || term_idx >= arnoldi_model.get_projection_list()[1].size()
      || std::abs(arnoldi_model.get_projection_list().front().front()) < STA_ERROR) {
    return arnoldi_model.get_diagonal_list().front();
  }
  return arnoldi_model.get_diagonal_list().front()
         + arnoldi_model.get_off_diagonal_list().front() * arnoldi_model.get_projection_list()[1][term_idx]
               / arnoldi_model.get_projection_list().front().front();
}

std::optional<double> DelayCalculator::calcParasiticArnoldiInputPortDelay(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                          std::string& sink_node_name, AnalysisType analysis_type,
                                                                          TransType trans_type)
{
  Database& database = STADM.getDatabase();
  std::string source_pin_name = getPinNameByParasiticNodeName(source_node_name);
  if (database.get_pin_map().count(source_pin_name) == 0 || !database.get_pin_map()[source_pin_name].get_is_port()) {
    return std::nullopt;
  }
  ParasiticArnoldiModel& arnoldi_model = getParasiticArnoldiModel(parasitic_net, source_node_name, analysis_type, trans_type);
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_term_index_map().count(sink_node_name) == 0) {
    return std::nullopt;
  }
  double elmore = calcParasiticArnoldiElmore(arnoldi_model, sink_node_name);
  return std::log(2.0) * elmore;
}

std::optional<double> DelayCalculator::calcParasiticArnoldiInputPortSlew(ParasiticNet& parasitic_net, std::string& source_node_name,
                                                                         std::string& sink_node_name, AnalysisType analysis_type,
                                                                         TransType trans_type, double input_slew)
{
  Database& database = STADM.getDatabase();
  std::string source_pin_name = getPinNameByParasiticNodeName(source_node_name);
  if (database.get_pin_map().count(source_pin_name) == 0 || !database.get_pin_map()[source_pin_name].get_is_port()) {
    return std::nullopt;
  }
  ParasiticArnoldiModel& arnoldi_model = getParasiticArnoldiModel(parasitic_net, source_node_name, analysis_type, trans_type);
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_term_index_map().count(sink_node_name) == 0) {
    return std::nullopt;
  }
  double elmore = calcParasiticArnoldiElmore(arnoldi_model, sink_node_name);
  double abs_input_slew = std::abs(input_slew);
  double output_slew = abs_input_slew + getParasiticArnoldiSlewScale(trans_type) * elmore;
  if (input_slew < 0.0) {
    return -output_slew;
  }
  return output_slew;
}

double DelayCalculator::getParasiticArnoldiSlewScale(TransType trans_type)
{
  double slew_derate = 1.0;
  double lower_threshold = 0.1;
  double upper_threshold = 0.9;
  double voltage_log = 0.0;
  double min_slew_factor = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  calcParasiticArnoldiThreshold(trans_type, slew_derate, lower_threshold, upper_threshold, voltage_log, min_slew_factor, x1, y1);
  return voltage_log / slew_derate;
}

ParasiticArnoldiPoleResidue DelayCalculator::calcParasiticArnoldiPoleResidue(ParasiticArnoldiModel& arnoldi_model, double drive_resistance)
{
  ParasiticArnoldiPoleResidue pole_residue;
  if (!arnoldi_model.get_is_valid() || arnoldi_model.get_order() <= 0) {
    return pole_residue;
  }

  std::vector<double> diagonal_list = arnoldi_model.get_diagonal_list();
  std::vector<double> off_diagonal_list = arnoldi_model.get_off_diagonal_list();
  diagonal_list.front() += drive_resistance * arnoldi_model.get_total_capacitance();

  std::vector<double> eigenvalue_list;
  std::vector<std::vector<double>> eigenvector_list;
  if (!solveParasiticArnoldiTridiagonalEigen(diagonal_list, off_diagonal_list, eigenvalue_list, eigenvector_list)) {
    return pole_residue;
  }

  int32_t order = arnoldi_model.get_order();
  std::size_t term_num = arnoldi_model.get_term_node_list().size();
  std::vector<double> pole_list(order, 0.0);
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    pole_list[order_idx] = 1.0 / std::max(eigenvalue_list[order_idx], 1E-5);
  }

  std::vector<std::vector<double>> residue_list(term_num, std::vector<double>(order, 0.0));
  for (int32_t order_idx = 0; order_idx < order; order_idx++) {
    double source_projection = arnoldi_model.get_sqrt_total_capacitance() * eigenvector_list[order_idx][0];
    for (std::size_t term_idx = 0; term_idx < term_num; term_idx++) {
      double term_projection = 0.0;
      for (int32_t basis_idx = 0; basis_idx < order; basis_idx++) {
        term_projection += eigenvector_list[order_idx][basis_idx] * arnoldi_model.get_projection_list()[basis_idx][term_idx];
      }
      residue_list[term_idx][order_idx] = source_projection * term_projection;
    }
  }

  pole_residue.set_is_valid(true);
  pole_residue.set_order(order);
  pole_residue.set_pole_list(pole_list);
  pole_residue.set_residue_list(residue_list);
  return pole_residue;
}

bool DelayCalculator::solveParasiticArnoldiTridiagonalEigen(std::vector<double>& diagonal_list, std::vector<double>& off_diagonal_list,
                                                             std::vector<double>& eigenvalue_list,
                                                             std::vector<std::vector<double>>& eigenvector_list)
{
  std::size_t order = diagonal_list.size();
  if (order == 0 || order > 32) {
    return false;
  }
  eigenvalue_list = diagonal_list;
  eigenvector_list.assign(order, std::vector<double>(order, 0.0));
  for (std::size_t order_idx = 0; order_idx < order; order_idx++) {
    eigenvector_list[order_idx][order_idx] = 1.0;
  }

  std::vector<double> diagonal_off_list(order, 0.0);
  for (std::size_t order_idx = 0; order_idx + 1 < order; order_idx++) {
    diagonal_off_list[order_idx + 1] = off_diagonal_list[order_idx];
  }

  for (int32_t high_idx = static_cast<int32_t>(order) - 1; high_idx >= 1; high_idx--) {
    int32_t iter_num = 0;
    while (std::abs(diagonal_off_list[high_idx]) > 1E-9) {
      int32_t low_idx = 0;
      if (iter_num++ == 20) {
        return false;
      }
      double g = (eigenvalue_list[high_idx - 1] - eigenvalue_list[high_idx]) / (2.0 * diagonal_off_list[high_idx]);
      double r = std::sqrt(1.0 + g * g);
      g = eigenvalue_list[low_idx] - eigenvalue_list[high_idx]
          + diagonal_off_list[high_idx] / (g + (g < 0.0 ? -r : r));
      double s = 1.0;
      double c = 1.0;
      double p = 0.0;
      int32_t iter_idx = low_idx + 1;
      for (; iter_idx <= high_idx; iter_idx++) {
        double f = s * diagonal_off_list[iter_idx];
        double b = c * diagonal_off_list[iter_idx];
        diagonal_off_list[iter_idx - 1] = r = std::sqrt(f * f + g * g);
        if (r == 0.0) {
          eigenvalue_list[iter_idx - 1] -= p;
          diagonal_off_list[low_idx] = 0.0;
          break;
        }
        s = f / r;
        c = g / r;
        g = eigenvalue_list[iter_idx - 1] - p;
        r = (eigenvalue_list[iter_idx] - g) * s + 2.0 * c * b;
        eigenvalue_list[iter_idx - 1] = g + (p = s * r);
        g = c * r - b;
        for (std::size_t term_idx = 0; term_idx < order; term_idx++) {
          f = eigenvector_list[iter_idx - 1][term_idx];
          eigenvector_list[iter_idx - 1][term_idx] = s * eigenvector_list[iter_idx][term_idx] + c * f;
          eigenvector_list[iter_idx][term_idx] = c * eigenvector_list[iter_idx][term_idx] - s * f;
        }
      }
      if (r == 0.0 && iter_idx <= high_idx) {
        continue;
      }
      eigenvalue_list[high_idx] -= p;
      diagonal_off_list[high_idx] = g;
      diagonal_off_list[low_idx] = 0.0;
    }
  }

  for (std::size_t order_idx = 0; order_idx + 1 < order; order_idx++) {
    std::size_t max_idx = order_idx;
    double max_value = eigenvalue_list[max_idx];
    for (std::size_t next_idx = order_idx + 1; next_idx < order; next_idx++) {
      if (eigenvalue_list[next_idx] > max_value) {
        max_idx = next_idx;
        max_value = eigenvalue_list[next_idx];
      }
    }
    if (max_idx == order_idx) {
      continue;
    }
    std::swap(eigenvalue_list[max_idx], eigenvalue_list[order_idx]);
    for (std::size_t basis_idx = 0; basis_idx < order; basis_idx++) {
      std::swap(eigenvector_list[max_idx][basis_idx], eigenvector_list[order_idx][basis_idx]);
    }
  }
  return true;
}

void DelayCalculator::calcParasiticArnoldiThreshold(TimingArc& timing_arc, TransType trans_type, double& slew_derate, double& lower_threshold,
                                                     double& upper_threshold, double& voltage_log, double& min_slew_factor, double& x1, double& y1)
{
  slew_derate = timing_arc.get_slew_derate();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_arc.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_arc.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = getNormalizedThreshold(lower_threshold);
  upper_threshold = getNormalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  calcParasiticArnoldiThresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void DelayCalculator::calcParasiticArnoldiThreshold(TransType trans_type, double& slew_derate, double& lower_threshold,
                                                     double& upper_threshold, double& voltage_log, double& min_slew_factor, double& x1, double& y1)
{
  Database& database = STADM.getDatabase();
  TimingLibrary& timing_library = database.get_timing_library();
  slew_derate = timing_library.get_slew_derate_from_library();
  if (trans_type == TransType::kFall) {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_fall();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_fall();
  } else {
    lower_threshold = timing_library.get_slew_lower_threshold_pct_rise();
    upper_threshold = timing_library.get_slew_upper_threshold_pct_rise();
  }
  lower_threshold = getNormalizedThreshold(lower_threshold);
  upper_threshold = getNormalizedThreshold(upper_threshold);
  if (!(lower_threshold > 0.01 && upper_threshold < 0.99 && upper_threshold > lower_threshold && slew_derate > 0.0)) {
    lower_threshold = 0.1;
    upper_threshold = 0.9;
    slew_derate = 0.8;
  }
  voltage_log = std::log(upper_threshold / lower_threshold);
  calcParasiticArnoldiThresholdFactor(lower_threshold, upper_threshold, min_slew_factor, x1, y1);
}

void DelayCalculator::calcParasiticArnoldiThresholdFactor(double lower_threshold, double upper_threshold, double& min_slew_factor, double& x1,
                                                           double& y1)
{
  double upper_log = std::log(1.0 / upper_threshold);
  min_slew_factor = upper_log + calcParasiticArnoldiHInverse((1.0 - upper_threshold) / upper_threshold - upper_log);
  double lower_log = std::log(1.0 / lower_threshold);
  double lower_factor = lower_log + calcParasiticArnoldiHInverse((1.0 - lower_threshold) / lower_threshold - lower_log);
  double lower_ratio = (std::exp(lower_factor) - 1.0) / lower_factor;
  double denominator = std::log(lower_ratio / lower_threshold) - calcParasiticArnoldiHInverse((1.0 - upper_threshold) * lower_factor);
  x1 = (upper_threshold - lower_threshold) / denominator;
  y1 = lower_factor * x1;
}

double DelayCalculator::calcParasiticArnoldiHInverse(double value)
{
  double x = 0.0;
  if (value < 1.0) {
    x = std::sqrt(2.0 * value) + 0.4 * value;
    if (value < 1E-4) {
      return x;
    }
  } else {
    x = value + 1.0;
  }
  for (int32_t iter = 0; iter < 4; iter++) {
    double exp_x = std::exp(-x);
    double function_value = x + exp_x - 1.0 - value;
    x += function_value / (exp_x - 1.0);
  }
  return x;
}

double DelayCalculator::calcParasiticArnoldiTableResistance(TimingArc& timing_arc, TransType output_trans_type, double input_slew,
                                                             double total_capacitance, double voltage_log, double slew_derate,
                                                             double delay_resistance)
{
  if (!(total_capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = calcTimingArcSlewByRawLoad(timing_arc, output_trans_type, input_slew, total_capacitance);
  double transition_time = slew_derate * table_slew;
  double drive_resistance = transition_time / (voltage_log * total_capacitance);
  if (delay_resistance > 0.0 && drive_resistance > delay_resistance) {
    drive_resistance = delay_resistance;
  }
  return drive_resistance;
}

double DelayCalculator::calcParasiticArnoldiTableRamp(TimingArc& timing_arc, TransType output_trans_type, double input_slew,
                                                       double drive_resistance, double capacitance, double lower_threshold, double upper_threshold,
                                                       double voltage_log, double slew_derate, double min_slew_factor)
{
  if (!(drive_resistance > 0.0 && capacitance > 0.0 && voltage_log > 0.0)) {
    return 0.0;
  }
  double table_slew = calcTimingArcSlewByRawLoad(timing_arc, output_trans_type, input_slew, capacitance);
  double transition_time = slew_derate * table_slew;
  double min_slew = drive_resistance * capacitance * min_slew_factor;
  if (voltage_log * drive_resistance * capacitance >= transition_time) {
    return min_slew;
  }
  double ramp = min_slew + 0.3 * transition_time;
  solveParasiticArnoldiRamp(1.0 / (drive_resistance * capacitance), transition_time, lower_threshold, upper_threshold, ramp);
  return ramp;
}

void DelayCalculator::solveParasiticArnoldiRamp(double pole, double transition_time, double lower_threshold, double upper_threshold, double& ramp)
{
  for (int32_t iter = 0; iter < 5; iter++) {
    double lower_pole_time = 0.0;
    double lower_derivative = 0.0;
    double upper_pole_time = 0.0;
    double upper_derivative = 0.0;
    solveParasiticArnoldiRampPoint(pole * ramp, lower_threshold, lower_pole_time, lower_derivative);
    solveParasiticArnoldiRampPoint(pole * ramp, upper_threshold, upper_pole_time, upper_derivative);
    double function_value = (lower_pole_time - upper_pole_time) / pole - transition_time;
    double derivative = lower_derivative - upper_derivative;
    if (std::abs(derivative) < STA_ERROR) {
      return;
    }
    ramp -= function_value / derivative;
    if (std::abs(function_value) < 1E-6) {
      return;
    }
  }
}

double DelayCalculator::solveParasiticArnoldiRampTime(double pole, double ramp, double voltage)
{
  double pole_ramp = pole * ramp;
  if (pole_ramp > 30.0) {
    return (1.0 + pole_ramp * (1.0 - voltage)) / pole;
  }
  double exp_pole_ramp = std::exp(pole_ramp);
  if ((1.0 - pole_ramp * voltage) * exp_pole_ramp >= 1.0) {
    return std::log((exp_pole_ramp - 1.0) / (pole_ramp * voltage)) / pole;
  }
  return calcParasiticArnoldiHInverse((1.0 - voltage) * pole_ramp) / pole;
}

void DelayCalculator::solveParasiticArnoldiRampPoint(double pole_ramp, double voltage, double& pole_time, double& derivative)
{
  if (pole_ramp > 30.0) {
    pole_time = 1.0 + pole_ramp * (1.0 - voltage);
    derivative = 1.0 - voltage;
    return;
  }
  double exp_pole_ramp = std::exp(pole_ramp);
  if ((1.0 - pole_ramp * voltage) * exp_pole_ramp >= 1.0) {
    pole_time = std::log((exp_pole_ramp - 1.0) / (pole_ramp * voltage));
    derivative = exp_pole_ramp / (exp_pole_ramp - 1.0) - 1.0 / pole_ramp;
    return;
  }
  pole_time = calcParasiticArnoldiHInverse((1.0 - voltage) * pole_ramp);
  derivative = (1.0 - voltage) / (pole_time - (1.0 - voltage) * pole_ramp);
}

double DelayCalculator::calcParasiticArnoldiEffectiveCapacitance(double driver_ramp, double drive_resistance,
                                                                  ParasiticArnoldiPoleResidue& pole_residue,
                                                                  double effective_capacitance_time)
{
  if (!(drive_resistance > 0.0) || pole_residue.get_residue_list().empty()) {
    return 0.0;
  }
  std::vector<double>& pole_list = pole_residue.get_pole_list();
  std::vector<double>& residue_list = pole_residue.get_residue_list().front();
  double integrated_current = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    double pole_ramp = pole * driver_ramp;
    double pole_time = pole * effective_capacitance_time;
    double charge = 0.0;
    if (effective_capacitance_time <= driver_ramp) {
      double exp_time = pole_time > 40.0 ? 0.0 : std::exp(-pole_time);
      charge = exp_time - 1.0 + pole_time;
    } else {
      double exp_time = (pole_time - pole_ramp) > 40.0 ? 0.0 : std::exp(-(pole_time - pole_ramp));
      double exp_ramp = pole_ramp > 40.0 ? 0.0 : std::exp(-pole_ramp);
      charge = pole_ramp - (1.0 - exp_ramp) * exp_time;
    }
    charge /= pole_ramp * pole;
    integrated_current += residue_list[pole_idx] * charge;
  }
  integrated_current /= drive_resistance;

  double voltage = calcParasiticArnoldiWaveformVoltage(effective_capacitance_time, driver_ramp, pole_list, residue_list);
  if (std::abs(1.0 - voltage) < STA_ERROR) {
    return 0.0;
  }
  return integrated_current / (1.0 - voltage);
}

void DelayCalculator::solveParasiticArnoldiWaveformTime(double driver_ramp, ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx,
                                                         double voltage, double& waveform_time)
{
  std::vector<double>& pole_list = pole_residue.get_pole_list();
  std::vector<double>& residue_list = pole_residue.get_residue_list()[term_idx];
  int32_t order = pole_residue.get_order();
  while (order > 1 && residue_list[order - 1] < 1E-8 && residue_list[order - 1] > -1E-8) {
    order--;
  }

  int32_t pole_idx = 0;
  if (residue_list[0] < 0.5) {
    for (int32_t idx = 1; idx < order; idx++) {
      if (residue_list[idx] > 0.3 && residue_list[idx] > residue_list[0]) {
        pole_idx = idx;
        break;
      }
    }
  }

  double waveform_at_ramp = 0.0;
  for (int32_t idx = 0; idx < order; idx++) {
    double pole_ramp = pole_list[idx] * driver_ramp;
    waveform_at_ramp += residue_list[idx] * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
  }

  double lower_time = 0.0;
  double upper_time = 0.0;
  double lower_voltage = 0.0;
  double upper_voltage = 0.0;
  if (waveform_at_ramp < voltage) {
    double candidate_time = 0.5 * (1.0 + voltage) * driver_ramp;
    double candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    if (candidate_voltage < voltage) {
      upper_time = candidate_time;
      upper_voltage = candidate_voltage;
      candidate_time = voltage * driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      lower_time = candidate_time;
      lower_voltage = candidate_voltage;
    } else {
      lower_time = candidate_time;
      lower_voltage = candidate_voltage;
      candidate_time = driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      while (candidate_voltage > voltage) {
        lower_time = candidate_time;
        lower_voltage = candidate_voltage;
        candidate_time *= 2.0;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      }
      upper_time = candidate_time;
      upper_voltage = candidate_voltage;
    }
  } else {
    double candidate_time = driver_ramp;
    double candidate_voltage = waveform_at_ramp;
    double search_pole = std::min(pole_list[pole_idx], 10.0);
    while (candidate_voltage >= voltage) {
      lower_time = candidate_time;
      lower_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    upper_time = candidate_time;
    upper_voltage = candidate_voltage;
  }
  waveform_time = solveParasiticArnoldiBracketedTime(driver_ramp, pole_list, residue_list, voltage, lower_time, upper_time, lower_voltage, upper_voltage);
}

void DelayCalculator::solveParasiticArnoldiWaveformTime(double driver_ramp, ParasiticArnoldiPoleResidue& pole_residue, std::size_t term_idx,
                                                         double upper_threshold, double& upper_time, double mid_threshold, double& mid_time,
                                                         double lower_threshold, double& lower_time)
{
  std::vector<double>& source_pole_list = pole_residue.get_pole_list();
  std::vector<double>& source_residue_list = pole_residue.get_residue_list()[term_idx];
  int32_t order = pole_residue.get_order();
  while (order > 1 && source_residue_list[order - 1] < 1E-8 && source_residue_list[order - 1] > -1E-8) {
    order--;
  }
  if (order <= 0) {
    upper_time = 0.0;
    mid_time = 0.0;
    lower_time = 0.0;
    return;
  }

  std::vector<double> pole_list;
  std::vector<double> residue_list;
  for (int32_t idx = 0; idx < order; idx++) {
    pole_list.push_back(source_pole_list[idx]);
    residue_list.push_back(source_residue_list[idx]);
  }

  int32_t pole_idx = 0;
  if (residue_list[0] < 0.5) {
    for (int32_t idx = 1; idx < order; idx++) {
      if (residue_list[idx] > 0.3 && residue_list[idx] > residue_list[0]) {
        pole_idx = idx;
        break;
      }
    }
  }
  double search_pole = std::min(pole_list[pole_idx], 10.0);
  double waveform_at_ramp = 0.0;
  for (int32_t idx = 0; idx < order; idx++) {
    double pole_ramp = pole_list[idx] * driver_ramp;
    waveform_at_ramp += residue_list[idx] * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
  }

  double hi_min_time = 0.0;
  double hi_max_time = 0.0;
  double hi_min_voltage = 0.0;
  double hi_max_voltage = 0.0;
  double mid_min_time = 0.0;
  double mid_max_time = 0.0;
  double mid_min_voltage = 0.0;
  double mid_max_voltage = 0.0;
  double lo_min_time = 0.0;
  double lo_max_time = 0.0;
  double lo_min_voltage = 0.0;
  double lo_max_voltage = 0.0;

  if (waveform_at_ramp < lower_threshold) {
    lo_max_time = driver_ramp;
    lo_max_voltage = waveform_at_ramp;
    double candidate_time = upper_threshold * driver_ramp;
    double candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    if (candidate_voltage < mid_threshold) {
      hi_max_time = candidate_time;
      mid_max_time = candidate_time;
      lo_min_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      mid_max_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time = mid_threshold * driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      if (candidate_voltage > upper_threshold) {
        hi_min_time = candidate_time;
        mid_min_time = candidate_time;
        lo_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
        mid_min_voltage = candidate_voltage;
        lo_min_voltage = candidate_voltage;
        if (candidate_voltage < mid_threshold) {
          mid_max_time = candidate_time;
          mid_max_voltage = candidate_voltage;
        } else {
          mid_min_time = candidate_time;
          mid_min_voltage = candidate_voltage;
        }
      } else {
        hi_max_time = candidate_time;
        mid_min_time = candidate_time;
        hi_max_voltage = candidate_voltage;
        mid_min_voltage = candidate_voltage;
        candidate_time = lower_threshold * driver_ramp;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
        hi_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
      }
    } else {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      mid_max_time = lo_max_time;
      mid_max_voltage = lo_max_voltage;
      if (candidate_voltage > upper_threshold) {
        hi_min_time = mid_min_time;
        hi_min_voltage = mid_min_voltage;
        hi_max_time = mid_max_time;
        hi_max_voltage = mid_max_voltage;
      } else {
        hi_max_time = mid_min_time;
        hi_max_voltage = mid_min_voltage;
        candidate_time = lower_threshold * driver_ramp;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
        hi_min_time = candidate_time;
        hi_min_voltage = candidate_voltage;
      }
    }
  } else if (waveform_at_ramp < mid_threshold) {
    hi_max_time = driver_ramp;
    mid_max_time = driver_ramp;
    lo_min_time = driver_ramp;
    hi_max_voltage = waveform_at_ramp;
    mid_max_voltage = waveform_at_ramp;
    lo_min_voltage = waveform_at_ramp;
    double candidate_time = driver_ramp + 1.6 / search_pole;
    double candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
    candidate_time = mid_threshold * driver_ramp;
    candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    mid_min_time = candidate_time;
    mid_min_voltage = candidate_voltage;
    if (candidate_voltage > upper_threshold) {
      hi_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
    } else {
      hi_max_time = candidate_time;
      hi_max_voltage = candidate_voltage;
      candidate_time = lower_threshold * driver_ramp;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      hi_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
    }
  } else if (waveform_at_ramp < upper_threshold) {
    hi_max_time = driver_ramp;
    mid_min_time = driver_ramp;
    lo_min_time = driver_ramp;
    hi_max_voltage = waveform_at_ramp;
    mid_min_voltage = waveform_at_ramp;
    lo_min_voltage = waveform_at_ramp;
    double candidate_time = lower_threshold * driver_ramp;
    double candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    hi_min_time = candidate_time;
    hi_min_voltage = candidate_voltage;
    candidate_time = driver_ramp + 0.7 / search_pole;
    candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 0.7 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    mid_max_time = candidate_time;
    mid_max_voltage = candidate_voltage;
    if (candidate_voltage < lower_threshold) {
      lo_max_time = candidate_time;
      lo_max_voltage = candidate_voltage;
    } else {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      while (candidate_voltage > lower_threshold) {
        lo_min_time = candidate_time;
        lo_min_voltage = candidate_voltage;
        candidate_time += 1.0 / search_pole;
        candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
      }
      lo_max_time = candidate_time;
      lo_max_voltage = candidate_voltage;
    }
  } else {
    double candidate_time = driver_ramp;
    double candidate_voltage = waveform_at_ramp;
    hi_min_time = candidate_time;
    mid_min_time = candidate_time;
    lo_min_time = candidate_time;
    hi_min_voltage = candidate_voltage;
    mid_min_voltage = candidate_voltage;
    lo_min_voltage = candidate_voltage;
    while (candidate_voltage > upper_threshold) {
      hi_min_time = candidate_time;
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      hi_min_voltage = candidate_voltage;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    hi_max_time = candidate_time;
    hi_max_voltage = candidate_voltage;
    while (candidate_voltage > mid_threshold) {
      mid_min_time = candidate_time;
      lo_min_time = candidate_time;
      mid_min_voltage = candidate_voltage;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    mid_max_time = candidate_time;
    mid_max_voltage = candidate_voltage;
    while (candidate_voltage > lower_threshold) {
      lo_min_time = candidate_time;
      lo_min_voltage = candidate_voltage;
      candidate_time += 1.0 / search_pole;
      candidate_voltage = calcParasiticArnoldiWaveformVoltage(candidate_time, driver_ramp, pole_list, residue_list);
    }
    lo_max_time = candidate_time;
    lo_max_voltage = candidate_voltage;
  }

  upper_time = solveParasiticArnoldiBracketedTime(driver_ramp, pole_list, residue_list, upper_threshold, hi_min_time, hi_max_time, hi_min_voltage,
                                                  hi_max_voltage);
  mid_time = solveParasiticArnoldiBracketedTime(driver_ramp, pole_list, residue_list, mid_threshold, mid_min_time, mid_max_time, mid_min_voltage,
                                                mid_max_voltage);
  lower_time = solveParasiticArnoldiBracketedTime(driver_ramp, pole_list, residue_list, lower_threshold, lo_min_time, lo_max_time, lo_min_voltage,
                                                  lo_max_voltage);
}

double DelayCalculator::calcParasiticArnoldiWaveformVoltage(double time, double driver_ramp, std::vector<double>& pole_list,
                                                            std::vector<double>& residue_list)
{
  double voltage = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole_time = pole_list[pole_idx] * time;
    double pole_ramp = pole_list[pole_idx] * driver_ramp;
    double response = 0.0;
    if (time < driver_ramp) {
      response = 1.0 - time / driver_ramp + (1.0 - std::exp(-pole_time)) / pole_ramp;
    } else {
      response = std::exp(pole_ramp - pole_time) * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
    }
    voltage += residue_list[pole_idx] * response;
  }
  return voltage;
}

void DelayCalculator::calcParasiticArnoldiWaveformVoltageAndDerivative(double time, double driver_ramp, std::vector<double>& pole_list,
                                                                        std::vector<double>& residue_list, double& voltage, double& derivative)
{
  voltage = 0.0;
  derivative = 0.0;
  for (std::size_t pole_idx = 0; pole_idx < pole_list.size(); pole_idx++) {
    double pole = pole_list[pole_idx];
    double pole_time = pole * time;
    double pole_ramp = pole * driver_ramp;
    double response = 0.0;
    double response_derivative = 0.0;
    if (time < driver_ramp) {
      double ramp_response = (1.0 - std::exp(-pole_time)) / pole_ramp;
      response = 1.0 - time / driver_ramp + ramp_response;
      response_derivative = -pole * ramp_response;
    } else {
      response = std::exp(pole_ramp - pole_time) * (1.0 - std::exp(-pole_ramp)) / pole_ramp;
      response_derivative = -pole * response;
    }
    voltage += residue_list[pole_idx] * response;
    derivative += residue_list[pole_idx] * response_derivative;
  }
}

double DelayCalculator::solveParasiticArnoldiBracketedTime(double driver_ramp, std::vector<double>& pole_list,
                                                            std::vector<double>& residue_list, double voltage, double lower_time,
                                                            double upper_time, double lower_voltage, double upper_voltage)
{
  double lower_function = lower_voltage - voltage;
  double upper_function = upper_voltage - voltage;
  if (lower_function == 0.0) {
    return lower_time;
  }
  if (upper_function == 0.0) {
    return upper_time;
  }

  double result_time = (lower_function * upper_time - upper_function * lower_time) / (lower_function - upper_function);
  double low_time = lower_time;
  double high_time = upper_time;
  if (lower_function < upper_function) {
    low_time = lower_time;
    high_time = upper_time;
    if (0.0 < lower_function) {
      return lower_time;
    }
    if (upper_function < 0.0) {
      return upper_time;
    }
  } else {
    low_time = upper_time;
    high_time = lower_time;
    if (0.0 < upper_function) {
      return upper_time;
    }
    if (lower_function < 0.0) {
      return lower_time;
    }
  }

  double old_delta = std::abs(upper_time - lower_time);
  double delta = old_delta;
  double function_value = 0.0;
  double derivative = 0.0;
  calcParasiticArnoldiWaveformVoltageAndDerivative(result_time, driver_ramp, pole_list, residue_list, function_value, derivative);
  function_value -= voltage;
  double last_function_value = 0.0;
  for (int32_t iter = 1; iter < 10; iter++) {
    if ((((result_time - high_time) * derivative - function_value) * ((result_time - low_time) * derivative - function_value) >= 0.0)
        || (std::abs(2.0 * function_value) > std::abs(old_delta * derivative))) {
      old_delta = delta;
      delta = 0.5 * (high_time - low_time);
      if (last_function_value * function_value > 0.0) {
        if (function_value < 0.0) {
          delta = 0.9348 * (high_time - low_time);
        } else {
          delta = 0.0625 * (high_time - low_time);
        }
      }
      last_function_value = function_value;
      result_time = low_time + delta;
      if (low_time == result_time) {
        return result_time;
      }
    } else {
      old_delta = delta;
      delta = function_value / derivative;
      last_function_value = 0.0;
      double temp_time = result_time;
      result_time -= delta;
      if (temp_time == result_time) {
        return result_time;
      }
    }
    if (std::abs(delta) < 1E-6) {
      return result_time;
    }
    calcParasiticArnoldiWaveformVoltageAndDerivative(result_time, driver_ramp, pole_list, residue_list, function_value, derivative);
    function_value -= voltage;
    if (function_value < 0.0) {
      low_time = result_time;
    } else {
      high_time = result_time;
    }
  }
  if (std::abs(function_value) < 1E-6) {
    return result_time;
  }
  return 0.5 * (low_time + high_time);
}

double DelayCalculator::getParasiticTotalResistance(ParasiticNet& parasitic_net)
{
  double resistance = 0.0;
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    resistance += parasitic_resistor.get_resistance();
  }
  return resistance;
}
double DelayCalculator::calcTimingCellArcDelay(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                                TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    if (analysis_type == AnalysisType::kMin) {
      return timing_cell_arc.get_delay_min();
    }
    return timing_cell_arc.get_delay_max();
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return timing_cell_arc.get_delay();
  }
  double output_load = getOutputPinLoad(output_pin, analysis_type, output_trans_type);
  std::vector<double> delay_list;
  for (TimingArc* timing_arc : getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type)) {
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

double DelayCalculator::calcTimingCellArcSlew(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type,
                                               TransType input_trans_type, TransType output_trans_type, double input_slew)
{
  if (timing_cell_arc.get_timing_arc_list().empty()) {
    return input_slew;
  }
  if (!isMatchTimingType(timing_cell_arc, output_trans_type)) {
    return input_slew;
  }
  double output_load = getOutputPinLoad(output_pin, analysis_type, output_trans_type);
  std::vector<double> slew_list;
  for (TimingArc* timing_arc : getCandidateTimingArcList(timing_cell_arc, input_trans_type, output_trans_type)) {
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
double DelayCalculator::calcArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type,
                                      double input_slew)
{
  if (arc.get_type() == ArcType::kNet) {
    return calcNetArcDelay(arc, analysis_type, output_trans_type, input_slew);
  }
  TimingCellArc* timing_cell_arc = getTimingCellArc(arc);
  if (timing_cell_arc != nullptr) {
    return calcTimingCellArcDelay(arc, *timing_cell_arc, analysis_type, input_trans_type, output_trans_type, input_slew);
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

double DelayCalculator::calcArcSlew(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type,
                                     double input_slew)
{
  if (arc.get_type() == ArcType::kNet) {
    return calcNetArcSlew(arc, analysis_type, output_trans_type, input_slew);
  }
  TimingCellArc* timing_cell_arc = getTimingCellArc(arc);
  if (timing_cell_arc != nullptr) {
    return calcTimingCellArcSlew(arc, *timing_cell_arc, analysis_type, input_trans_type, output_trans_type, input_slew);
  }
  return input_slew;
}

double DelayCalculator::calcNetArcSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = STADM.getDatabase();
  if (database.get_parasitic_library().get_net_map().count(arc.get_owner_name()) > 0) {
    return calcParasiticSlew(arc, analysis_type, trans_type, input_slew);
  }
  return input_slew;
}

double DelayCalculator::calcParasiticSlew(Arc& arc, AnalysisType analysis_type, TransType trans_type, double input_slew)
{
  Database& database = STADM.getDatabase();
  ParasiticNet& parasitic_net = database.get_parasitic_library().get_net_map()[arc.get_owner_name()];
  std::string source_node_name = getParasiticNodeName(parasitic_net, arc.get_source_pin());
  std::string sink_node_name = getParasiticNodeName(parasitic_net, arc.get_sink_pin());
  if (source_node_name.empty() || sink_node_name.empty()) {
    return input_slew;
  }

  std::optional<double> cached_load_slew = getParasiticArnoldiCachedLoadSlew(arc, analysis_type, trans_type, input_slew);
  if (cached_load_slew) {
    return *cached_load_slew;
  }
  cached_load_slew = getParasiticDmpCachedLoadSlew(arc, analysis_type, trans_type, input_slew);
  if (cached_load_slew) {
    return *cached_load_slew;
  }
  std::optional<double> input_port_slew
      = calcParasiticArnoldiInputPortSlew(parasitic_net, source_node_name, sink_node_name, analysis_type, trans_type, input_slew);
  if (input_port_slew) {
    return *input_port_slew;
  }

  buildParasiticDelayMap(parasitic_net, source_node_name, analysis_type, trans_type);
  if (_parasitic_impulse_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type].count(sink_node_name) == 0) {
    return input_slew;
  }
  double impulse = _parasitic_impulse_map_cache[parasitic_net.get_net_name()][analysis_type][trans_type][sink_node_name];
  double output_slew = std::sqrt(input_slew * input_slew + impulse);
  if (input_slew < 0.0) {
    return -output_slew;
  }
  return output_slew;
}

}  // namespace ista
