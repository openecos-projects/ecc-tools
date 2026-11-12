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

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Database.hpp"

namespace ista {
class PinDirectionResolver
{
 public:
  explicit PinDirectionResolver(Database& database) : _database(database) {}
  void normalize();
  std::string getPinNameListString(std::vector<std::string>& pin_name_list);
  void resolve();
  LibertyPort* libertyPort(Pin& pin);
  bool isDriverDirection(Pin& pin, PinDirection direction);

 private:
  std::map<std::string, PinDirection> makeInoutPinDirectionMap();
  bool isFloatingInoutPin(Pin& pin);
  PinDirection inferInoutPinDirection(const std::string& pin_name, Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map);
  PinDirection inferInoutPinDirectionByTimingCell(Pin& pin);
  PinDirection inferInoutPinDirectionByTimingGraph(const std::string& pin_name);
  bool hasOutgoingCellArc(const std::string& pin_name);
  bool hasIncomingCellArc(const std::string& pin_name);
  PinDirection inferInoutPinDirectionByNet(Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map);
  Net* getPinNet(Pin& pin);
  std::vector<std::string> getDriverPinList(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map);
  int32_t getDriverPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map);
  int32_t getUnresolvedInoutPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map);
  bool isResolvedDriverPin(const std::string& pin_name, std::map<std::string, PinDirection>& inout_pin_direction_map);
  PinDirection getDriverPinDirection(Pin& pin);
  PinDirection getLoadPinDirection(Pin& pin);
  Database& _database;
};
}  // namespace ista
