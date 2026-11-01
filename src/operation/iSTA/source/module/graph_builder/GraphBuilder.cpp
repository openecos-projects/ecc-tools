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
#include "GraphBuilder.hpp"

#include "GraphConnectivityBuilder.hpp"
#include "GraphEndpointClassifier.hpp"
#include "GraphOrderBuilder.hpp"
#include "Logger.hpp"
#include "LogicSimulator.hpp"
#include "Monitor.hpp"

namespace ista {
void GraphBuilder::build()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  auto& database = _database;
  GraphConnectivityBuilder(database).build();
  LogicSimulator(database.network(), database.liberty(), database.sdc(), database.graph()).propagate();
  GraphEndpointClassifier(database).classify();
  GraphOrderBuilder(database.graph()).build();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}
}  // namespace ista
