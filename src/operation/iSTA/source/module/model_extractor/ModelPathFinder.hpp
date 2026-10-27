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
#include <vector>

#include "Database.hpp"
#include "liberty/TimingModel.hpp"

namespace ista {
class ModelPathFinder
{
 public:
  explicit ModelPathFinder(Database& database) : _database(database) {}
  std::vector<TimingPath*> getTimingPathList(AnalysisType analysis_type);
  bool isInputPort(std::string& pin_name);
  bool isOutputPort(std::string& pin_name);
  bool isClockPort(std::string& pin_name);
  bool isRegisterStartPoint(std::string& pin_name);
  bool isRegisterEndPoint(std::string& pin_name);
  std::string getRelatedClockPort(std::string clock_name);

 private:
  bool isMatchAnalysisType(TimingPath& timing_path, AnalysisType analysis_type);
  bool isPort(std::string& pin_name);
  Database& _database;
};
}  // namespace ista
