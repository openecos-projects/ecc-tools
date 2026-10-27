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
#include "TimingAnalyzer.hpp"

#include "ClockChecker.hpp"
#include "ClockPropagator.hpp"
#include "DataPropagator.hpp"
#include "DelayCalculator.hpp"
#include "ElectricalChecker.hpp"
#include "FanoutChecker.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PathAnalyzer.hpp"
#include "PathChecker.hpp"
#include "RequiredPropagator.hpp"

namespace ista {
void TimingAnalyzer::analyze()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  Database& database = _database;
  _delay.clear();
  database.graph() = Graph{};
  database.pathGroups().clear();
  database.pathStats() = PathStats{};
  database.fanoutChecks().clear();
  database.electricalChecks().clear();
  database.clockChecks().clear();
  GraphBuilder(database).build();
  _delay.initialize(database.graph());

  database.graph().clearTiming();
  ClockPropagator(database, _delay).propagate();
  DataPropagator(database, _delay).propagate();

  {
    // Clock topology/waveforms stay fixed throughout required/path analysis.
    PathChecker check(database, _delay, ClockReconvergenceCalculator::CacheMode::kSnapshot);
    RequiredPropagator(database, check).propagate();
    PathAnalyzer(database, check).analyze();
  }
  database.fanoutChecks() = FanoutChecker(database).check();
  database.electricalChecks() = ElectricalChecker(database, _delay).check();
  database.clockChecks() = ClockChecker(database).check();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}
}  // namespace ista
