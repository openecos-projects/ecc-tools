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

#include <optional>
#include <string>

#include "ArnoldiResponseCalculator.hpp"
#include "CellDelayCalculator.hpp"
#include "CheckDelayCalculator.hpp"
#include "Config.hpp"
#include "Database.hpp"
#include "DelayResult.hpp"
#include "DmpResponseCalculator.hpp"
#include "InputPortResponseCalculator.hpp"
#include "LibertyArcSelector.hpp"
#include "LoadCalculator.hpp"
#include "PinCapacitanceCalculator.hpp"
#include "RcModelBuilder.hpp"
#include "RcTreeAnalyzer.hpp"
#include "ReceiverThresholdCalculator.hpp"
#include "WireDelayCalculator.hpp"

namespace ista {
// Evaluates explicit arc/cell/check requests. Owns one set of calculation caches; traversal lives elsewhere.
class DelayCalculator
{
 public:
  DelayCalculator(Database& database, const Config& config)
      : _database(database),
        _pin_capacitance(database),
        _rc(database, _pin_capacitance),
        _load(database, _pin_capacitance, _rc),
        _models(database, config, _rc),
        _threshold(database),
        _dmp(database, _rc, _models, _threshold),
        _arnoldi(database, config, _rc, _models, _threshold),
        _input_port(database, _rc, _models, _threshold),
        _selection(database),
        _check(_selection),
        _cell(_load, _dmp, _arnoldi, _selection),
        _wire(database, _rc, _dmp, _arnoldi, _input_port)
  {
  }
  DelayCalculator(const DelayCalculator&) = delete;
  DelayCalculator& operator=(const DelayCalculator&) = delete;
  static void initInst();
  static DelayCalculator& getInst();
  static void destroyInst();
  void clear()
  {
    _rc.clear();
    _models.clear();
    _dmp.clear();
    _arnoldi.clear();
    _input_port.clear();
  }
  void initialize(Arc& arc) { initializeArcTiming(arc); }
  void initialize(Graph& graph);
  // Complete unvisited conditional variants after propagation, for SDF/export.
  // Does not update the aggregate delays used by timing paths.
  void completeCellArcDelays(Graph& graph);
  // The same SDC electrical input drives both data and clock propagation.
  double inputPortSlew(std::string pin, AnalysisType analysis, TransType edge);
  std::optional<DelayResult> inputPortTiming(std::string& pin, AnalysisType analysis, TransType edge);
  double pinLoad(std::string pin, AnalysisType analysis, TransType edge) { return _load.pinLoad(pin, analysis, edge); }
  std::optional<DelayResult> arcDelay(Arc& arc, AnalysisType analysis, TransType input, TransType output, double slew,
                                      std::optional<double> output_slew_input = std::nullopt);
  std::optional<DelayResult> cellDelay(std::string& pin, TimingCellArc& arc, AnalysisType analysis, TransType input, TransType output, double slew);
  double checkDelay(TimingCheckArc& arc, AnalysisType analysis, TransType clock, TransType data, double clock_slew, double data_slew);
  bool calculateDrivingCell(std::string& pin, TimingCellArc& arc, AnalysisType analysis, TransType output, double rise_slew, double fall_slew,
                            DelayResult& result)
  {
    return _cell.calculateDrivingCell(pin, arc, analysis, output, rise_slew, fall_slew, result);
  }

 private:
  void initializeArcTiming(Arc& arc);
  void initializeAnalysisArcTiming(Arc& arc, AnalysisType analysis_type);
  void initializeTransArcTiming(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double calcArcSlew(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double input_slew);
  double calcArcDelay(Arc& arc);
  double calcCellArcDelay(Arc& arc, AnalysisType analysis_type);
  double calcCellArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type);
  double calcArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double input_slew);
  Database& _database;
  PinCapacitanceCalculator _pin_capacitance;
  RcTreeAnalyzer _rc;
  LoadCalculator _load;
  RcModelBuilder _models;
  ReceiverThresholdCalculator _threshold;
  DmpResponseCalculator _dmp;
  ArnoldiResponseCalculator _arnoldi;
  InputPortResponseCalculator _input_port;
  LibertyArcSelector _selection;
  CheckDelayCalculator _check;
  CellDelayCalculator _cell;
  WireDelayCalculator _wire;
};
#define STADC (ista::DelayCalculator::getInst())
}  // namespace ista
