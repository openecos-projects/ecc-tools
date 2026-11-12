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

#include "Database.hpp"

namespace ista {
class ReceiverThresholdCalculator
{
 public:
  ReceiverThresholdCalculator(Database& database) : _database(database) {}
  ReceiverThresholdCalculator(const ReceiverThresholdCalculator&) = delete;
  ReceiverThresholdCalculator& operator=(const ReceiverThresholdCalculator&) = delete;
  bool compatible(TimingArc& timing_arc, std::string& load_pin, TransType trans_type);
  void adjust(TimingArc& timing_arc, std::string& load_pin, TransType trans_type, double& wire_delay, double& load_slew);
  // Scalar reference-point conversion is only for skipped-RC/balanced responses.
  void adjustSignalLevel(std::string& source, std::string& load, AnalysisType analysis, TransType edge, double& delay, double& slew);
  double voltageRatio(const TimingArc& source, std::string& load);
  LibertyCell* getThresholdTimingCell(std::string& pin_name);
  LibertyCell* sourceCell(std::string& pin_name, AnalysisType analysis, TransType edge);
  double getTimingCellSlewLowerThreshold(LibertyCell& timing_cell, TransType trans_type);
  double getTimingCellSlewUpperThreshold(LibertyCell& timing_cell, TransType trans_type);
  double getTimingCellInputThreshold(LibertyCell& timing_cell, TransType trans_type);

 private:
  Database& _database;
};
}  // namespace ista
