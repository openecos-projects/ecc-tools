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

#include <cstddef>
#include <string>
#include <vector>

#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "graph/TimingStateQuery.hpp"

namespace ista {
class DataSlewPropagator
{
 public:
  explicit DataSlewPropagator(Database& database, DelayCalculator& delay) : _database(database), _delay(delay), _path_query(database) {}
  void propagate();

 private:
  bool shouldStopDataSlewPropagation(Arc& arc);
  void propagateDataSlewDelayArc(std::size_t arc_idx);
  void propagateDataSlewDelayArc(std::size_t arc_idx, AnalysisType analysis_type, TransType input_trans_type);
  void updateDataSlewDelay(Arc& arc, TimingPoint& source_point, TimingPoint& sink_point, AnalysisType analysis_type, TransType input_trans_type,
                           TransType output_trans_type);
  void updateGraphArcDelay(Arc& arc, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double arc_delay);
  void updateDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type, double data_slew);
  bool hasDataSlew(TimingPoint& timing_point, AnalysisType analysis_type, TransType trans_type);
  bool isBetterDelay(double candidate_delay, double current_delay, AnalysisType analysis_type);
  bool isBetterSlew(double candidate_slew, double current_slew, AnalysisType analysis_type);
  Database& _database;
  DelayCalculator& _delay;
  TimingStateQuery _path_query;
};
}  // namespace ista
