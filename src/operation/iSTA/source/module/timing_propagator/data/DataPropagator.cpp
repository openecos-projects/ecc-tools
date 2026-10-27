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
#include "DataPropagator.hpp"

#include "Logger.hpp"
#include "Monitor.hpp"
#include "TransitionFilter.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
void DataPropagator::propagate()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  Database& database = _database;
  auto startpoints = database.graph().startpoints();
  _startpoint_timing.seed(startpoints);
  _data_slew_propagator.propagate();
  _data_arrival_propagator.propagate();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}
}  // namespace ista
