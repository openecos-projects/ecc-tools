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
#pragma once

#include <string>

#include "ArnoldiResponseCalculator.hpp"
#include "Database.hpp"
#include "DelayResult.hpp"
#include "DmpResponseCalculator.hpp"
#include "LibertyArcSelector.hpp"
#include "LoadCalculator.hpp"

namespace ista {
// Selects Liberty cell arcs and evaluates delay/transition with load and driver response.
class CellDelayCalculator
{
 public:
  CellDelayCalculator(LoadCalculator& load, DmpResponseCalculator& dmp, ArnoldiResponseCalculator& arnoldi, LibertyArcSelector& selection)
      : _load(load), _dmp(dmp), _arnoldi(arnoldi), _selection(selection)
  {
  }
  CellDelayCalculator(const CellDelayCalculator&) = delete;
  CellDelayCalculator& operator=(const CellDelayCalculator&) = delete;
  bool calculateDrivingCell(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType output_trans_type,
                            double input_transition_rise, double input_transition_fall, DelayResult& timing_result);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type);
  double calcTimingCellArcDelay(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type,
                                double input_slew, bool is_initialization = false);
  double calcTimingCellArcSlew(Arc& arc, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type,
                               double input_slew);
  double calcTimingCellArcDelay(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type,
                                double input_slew, double output_load);
  double calcTimingCellArcSlew(TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type,
                               double input_slew, double output_load);
  double calcTimingCellArcDelay(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                                TransType output_trans_type, double input_slew);
  double calcTimingCellArcSlew(std::string& output_pin, TimingCellArc& timing_cell_arc, AnalysisType analysis_type, TransType input_trans_type,
                               TransType output_trans_type, double input_slew);

  // Evaluate an explicit Liberty variant, including a condition inactive in STA.
  double calcTimingArcDelay(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew,
                            double output_load);

 private:
  void updateTimingArcDelay(Arc& arc, TimingArc& timing_arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double delay,
                            bool is_initialization = false);
  double convertOutputLoad(TimingArc& timing_arc, double output_load);
  double calcTimingArcSlew(std::string& output_pin, TimingArc& timing_arc, AnalysisType analysis_type, TransType output_trans_type, double input_slew,
                           double output_load);
  double calcTimingArcDelayByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  double calcTimingArcSlewByLoad(TimingArc& timing_arc, TransType output_trans_type, double input_slew, double output_load);
  LoadCalculator& _load;
  DmpResponseCalculator& _dmp;
  ArnoldiResponseCalculator& _arnoldi;
  LibertyArcSelector& _selection;
};
}  // namespace ista
