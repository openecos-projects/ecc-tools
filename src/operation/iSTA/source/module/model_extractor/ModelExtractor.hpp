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

#include "CheckArcExtractor.hpp"
#include "Database.hpp"
#include "DelayArcExtractor.hpp"
#include "ModelPathFinder.hpp"
#include "liberty/TimingModel.hpp"

namespace ista {
class ModelExtractor
{
 public:
  explicit ModelExtractor(Database& database) : _database(database), _check_arc_extractor(database), _delay_arc_extractor(database), _model_path_query(database)
  {
  }
  TimingModel extract();

 private:
  void extractLibraries(TimingModel& tc_model);
  void extractLibrary(TimingModel& tc_model, AnalysisType analysis_type);
  void buildPorts(CharacterizedLibrary& tc_lib);
  void buildCharacterizedPort(CharacterizedLibrary& tc_lib, Pin& pin);
  double getPortCapacitance(Pin& pin);
  void extractClockArcs(CharacterizedLibrary& tc_lib);
  void buildClockPathArc(CharacterizedLibrary& tc_lib, std::string& clock_port_name, std::string timing_type);
  double getDesignArea();
  Database& _database;
  CheckArcExtractor _check_arc_extractor;
  DelayArcExtractor _delay_arc_extractor;
  ModelPathFinder _model_path_query;
};
}  // namespace ista
