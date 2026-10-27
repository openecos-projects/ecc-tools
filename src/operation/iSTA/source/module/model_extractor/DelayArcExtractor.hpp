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
#include "ModelPathFinder.hpp"
#include "liberty/TimingModel.hpp"

namespace ista {
class DelayArcExtractor
{
 public:
  explicit DelayArcExtractor(Database& database) : _database(database), _model_path_query(database) {}
  void extract(CharacterizedLibrary& tc_lib, AnalysisType analysis_type);

 private:
  double getWorseDelay(double current_delay, double delay, bool has_delay, AnalysisType analysis_type);
  void buildDelayArc(CharacterizedLibrary& tc_lib, std::string& source_port, std::string& sink_port, std::string& timing_type, std::string& timing_sense,
                     double rise_delay, double fall_delay, bool has_rise_delay, bool has_fall_delay);
  std::string getDelayArcRelatedPin(TimingPath& timing_path);
  std::string getDelayArcTimingType(TimingPath& timing_path);
  TransType getRegisterClockTransType(TimingPath& timing_path);
  std::string getDelayArcTimingSense(TimingPath& timing_path);
  double getDelayArcValue(TimingPath& timing_path);
  Database& _database;
  ModelPathFinder _model_path_query;
};
}  // namespace ista
