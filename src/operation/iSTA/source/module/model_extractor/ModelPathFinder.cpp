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
#include "ModelPathFinder.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ista {
std::vector<TimingPath*> ModelPathFinder::getTimingPathList(AnalysisType analysis_type)
{
  Database& database = _database;
  std::vector<TimingPath*> timing_path_list;
  for (PathGroup& timing_path_group : database.pathGroups()) {
    for (auto& timing_path_end_pair : timing_path_group.endpointPaths()) {
      for (TimingPath& timing_path : timing_path_end_pair.second) {
        if (isMatchAnalysisType(timing_path, analysis_type)) {
          timing_path_list.push_back(&timing_path);
        }
      }
    }
  }
  return timing_path_list;
}

bool ModelPathFinder::isMatchAnalysisType(TimingPath& timing_path, AnalysisType analysis_type)
{
  return timing_path.get_analysis_type() == analysis_type;
}

bool ModelPathFinder::isPort(std::string& pin_name)
{
  Database& database = _database;
  return database.network().pins().count(pin_name) > 0 && database.network().pins()[pin_name].get_is_port();
}

bool ModelPathFinder::isInputPort(std::string& pin_name)
{
  Database& database = _database;
  return isPort(pin_name)
         && (database.network().pins()[pin_name].get_direction() == PinDirection::kInput
             || database.network().pins()[pin_name].get_direction() == PinDirection::kInout);
}

bool ModelPathFinder::isOutputPort(std::string& pin_name)
{
  Database& database = _database;
  return isPort(pin_name)
         && (database.network().pins()[pin_name].get_direction() == PinDirection::kOutput
             || database.network().pins()[pin_name].get_direction() == PinDirection::kInout);
}

bool ModelPathFinder::isClockPort(std::string& pin_name)
{
  Database& database = _database;
  if (!isPort(pin_name)) {
    return false;
  }
  for (std::pair<const std::string, TimingClock>& clock_pair : database.sdc().get_clock_map()) {
    for (std::string& clock_source : clock_pair.second.get_source_list()) {
      if (clock_source == pin_name) {
        return true;
      }
    }
  }
  return false;
}

bool ModelPathFinder::isRegisterStartPoint(std::string& pin_name)
{
  Database& database = _database;
  if (database.network().pins().count(pin_name) == 0) {
    return false;
  }
  Pin& pin = database.network().pins()[pin_name];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  return instance.get_is_sequential() && pin_name == instance.get_output_pin_name();
}

bool ModelPathFinder::isRegisterEndPoint(std::string& pin_name)
{
  Database& database = _database;
  if (database.network().pins().count(pin_name) == 0) {
    return false;
  }
  Pin& pin = database.network().pins()[pin_name];
  if (pin.get_is_port() || database.network().instances().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_check_arc_list().empty();
}

std::string ModelPathFinder::getRelatedClockPort(std::string clock_name)
{
  Database& database = _database;
  if (database.sdc().get_clock_map().count(clock_name) == 0) {
    return "";
  }
  TimingClock& timing_clock = database.sdc().get_clock_map()[clock_name];
  for (std::string& source_name : timing_clock.get_source_list()) {
    if (isClockPort(source_name)) {
      return source_name;
    }
  }
  return "";
}
}  // namespace ista
