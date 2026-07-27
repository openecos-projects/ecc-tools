// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Database.hpp"
#include "EAModel.hpp"
#include "PowerEdge.hpp"
#include "PowerGraph.hpp"

namespace iemir {

#define EMIREA (iemir::EMAnalyzer::getInst())

class EMAnalyzer
{
 public:
  static void initInst();
  static EMAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();

 private:
  // self
  static EMAnalyzer* _ea_instance;

  EMAnalyzer() = default;
  EMAnalyzer(const EMAnalyzer& other) = delete;
  EMAnalyzer(EMAnalyzer&& other) = delete;
  ~EMAnalyzer() = default;
  EMAnalyzer& operator=(const EMAnalyzer& other) = delete;
  EMAnalyzer& operator=(EMAnalyzer&& other) = delete;
  // function
  void analyzePowerGraphList();
  void analyzePowerGraph(PowerGraph& power_graph);
  EAModel initEAModel();
  void analyzePowerEdgeList(PowerGraph& power_graph, EAModel& ea_model);
  void analyzePowerEdge(PowerGraph& power_graph, PowerEdge& power_edge, EAModel& ea_model);
};

}  // namespace iemir
