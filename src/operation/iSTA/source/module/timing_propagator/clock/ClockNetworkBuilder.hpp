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

#include "ClockSource.hpp"
#include "Database.hpp"
namespace ista {
// Prepares sources in master-before-generated order and marks their graph coverage.
class ClockNetworkBuilder
{
 public:
  explicit ClockNetworkBuilder(Database& database) : _database(database) {}
  std::vector<ClockSource> build();
  bool stopsAt(const std::string& pin_name);

 private:
  Database& _database;
  void orderSources(std::vector<ClockSource>& sources);
  void markClockPoints(std::vector<ClockSource>& sources);
  void markClock(ClockSource& clock);
};
}  // namespace ista
