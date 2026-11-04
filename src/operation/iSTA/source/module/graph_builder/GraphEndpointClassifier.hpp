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
class GraphEndpointClassifier
{
 public:
  explicit GraphEndpointClassifier(Database& database) : _database(database) {}
  void classify();

 private:
  bool isStartPoint(const std::string& pin_name, Pin& pin);
  bool isRegisterClockStartPoint(const std::string& pin_name, Pin& pin);
  bool isClockPin(const std::string& pin_name, Pin& pin);
  bool isClockSource(const std::string& pin_name);
  bool hasIncomingArc(const std::string& pin_name);
  bool isStartPort(Pin& pin);
  bool isEndPoint(const std::string& pin_name, Pin& pin);
  bool isTimingCheckEndPoint(const std::string& pin_name, Pin& pin);
  bool hasOutgoingArc(const std::string& pin_name);
  bool isEndPort(Pin& pin);
  Database& _database;
};
}  // namespace ista
