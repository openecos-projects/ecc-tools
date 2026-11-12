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

#include "ArcTimingLookup.hpp"
#include "DelayCache.hpp"
#include "OutputLoadEstimator.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"

namespace ipw {

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
    PWLOG.error(Loc::current(), "The instance not initialized!");
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
  PWLOG.info(Loc::current(), "Starting...");

  clearParasiticCache();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DelayCalculator::calculate(DCTask& dc_task)
{
  dc_task.set_is_valid(false);
  if (dc_task.get_arc() != nullptr) {
    _arc_timing_lookup.calculateArc(dc_task);
    return;
  }
  if (dc_task.get_timing_cell_arc() != nullptr) {
    _arc_timing_lookup.calculateTimingCellArc(dc_task);
  }
}

void DelayCalculator::destroy()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  clearParasiticCache();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

double DelayCalculator::getPowerOutputLoad(
    std::string& output_pin,
    AnalysisType analysis_type,
    TransType output_trans_type
)
{
  return getOutputPinLoad(output_pin, analysis_type, output_trans_type);
}

std::vector<TransType> DelayCalculator::getOutputTransTypeList(Arc& arc, TransType input_trans_type)
{
  return _arc_timing_lookup.getOutputTransTypeList(arc, input_trans_type);
}

bool DelayCalculator::calculateDrivingCell(
    std::string& output_pin,
    TimingCellArc& timing_cell_arc,
    AnalysisType analysis_type,
    TransType output_trans_type,
    double input_transition_rise,
    double input_transition_fall,
    double& output_slew
)
{
  return _arc_timing_lookup.calcDrivingCellSlew(
      output_pin,
      timing_cell_arc,
      analysis_type,
      output_trans_type,
      input_transition_rise,
      input_transition_fall,
      output_slew
  );
}

// private

DelayCalculator* DelayCalculator::_dc_instance = nullptr;

void DelayCalculator::clearParasiticCache()
{
  PWDCACHE.clear();
}

}  // namespace ipw