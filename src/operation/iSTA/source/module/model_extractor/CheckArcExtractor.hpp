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
class CheckArcExtractor
{
 public:
  explicit CheckArcExtractor(Database& database) : _database(database), _model_path_query(database) {}
  void extract(CharacterizedLibrary& tc_lib, AnalysisType analysis_type);

 private:
  void buildCheckArc(CharacterizedLibrary& tc_lib, std::string& source_port, std::string& sink_port, std::string& timing_type, double rise_constraint,
                     double fall_constraint, bool has_rise_constraint, bool has_fall_constraint);
  std::string getTimingCheckType(TimingPath& timing_path, AnalysisType analysis_type);
  std::string getTimingCheckTypeSuffix(TransType clock_trans_type);
  double getCheckConstraint(TimingPath& timing_path, AnalysisType analysis_type);
  Database& _database;
  ModelPathFinder _model_path_query;
};
}  // namespace ista
