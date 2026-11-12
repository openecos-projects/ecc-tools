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

#include <string_view>

#include "ClockArcPropagator.hpp"
#include "ClockSource.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"

namespace ista {
class GeneratedClockSeeder
{
 public:
  explicit GeneratedClockSeeder(Database& database, DelayCalculator& delay) : _database(database), _delay(delay), _clock_arc_propagator(database, delay) {}
  void seed(ClockSource& clock);

 private:
  bool seedFromMaster(ClockSource& clock, const TimingClock& definition);
  bool hasPhysicalClockState(const TimingPoint& timing_point, std::string_view clock_name);
  Database& _database;
  DelayCalculator& _delay;
  ClockArcPropagator _clock_arc_propagator;
};
}  // namespace ista
