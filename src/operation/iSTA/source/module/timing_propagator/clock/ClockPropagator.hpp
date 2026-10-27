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

#include <vector>

#include "ClockArcPropagator.hpp"
#include "ClockSource.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "GeneratedClockSeeder.hpp"

namespace ista {
class ClockPropagator
{
 public:
  explicit ClockPropagator(Database& database, DelayCalculator& delay)
      : _database(database), _clock_arc_propagator(database, delay), _generated_clock_seeder(database, delay)
  {
  }
  void propagate();

 private:
  void publishClock(ClockSource& clock);
  void propagateSources(std::vector<ClockSource>& sources);
  void propagateClockSlewDelay(ClockSource& clock);
  Database& _database;
  ClockArcPropagator _clock_arc_propagator;
  GeneratedClockSeeder _generated_clock_seeder;
};
}  // namespace ista
