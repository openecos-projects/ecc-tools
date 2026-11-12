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
#include "DataArrivalPropagator.hpp"
#include "DataSlewPropagator.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "StartpointInitializer.hpp"

namespace ista {
class DataPropagator
{
 public:
  explicit DataPropagator(Database& database, DelayCalculator& delay)
      : _database(database), _data_arrival_propagator(database), _data_slew_propagator(database, delay), _startpoint_timing(database, delay)
  {
  }
  void propagate();

 private:
  Database& _database;
  DataArrivalPropagator _data_arrival_propagator;
  DataSlewPropagator _data_slew_propagator;
  StartpointInitializer _startpoint_timing;
};
}  // namespace ista
