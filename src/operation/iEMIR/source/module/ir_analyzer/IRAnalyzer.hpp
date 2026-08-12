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
#include "IAModel.hpp"
#include "InstancePower.hpp"
#include "PowerGraph.hpp"
#include "PowerNetType.hpp"

namespace iemir {

#define EMIRIA (iemir::IRAnalyzer::getInst())

class IRAnalyzer
{
 public:
  static void initInst();
  static IRAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();

 private:
  // self
  static IRAnalyzer* _ia_instance;

  IRAnalyzer() = default;
  IRAnalyzer(const IRAnalyzer& other) = delete;
  IRAnalyzer(IRAnalyzer&& other) = delete;
  ~IRAnalyzer() = default;
  IRAnalyzer& operator=(const IRAnalyzer& other) = delete;
  IRAnalyzer& operator=(IRAnalyzer&& other) = delete;
  // function
  void analyzePowerGraphList();
  void analyzePowerGraph(PowerGraph& power_graph);
  IAModel initIAModel(PowerGraph& power_graph);
  void buildSourceVoltage(PowerGraph& power_graph, IAModel& ia_model);
  void buildNodeMatrixIndex(PowerGraph& power_graph, IAModel& ia_model);
  void buildNodeCurrentMap(PowerGraph& power_graph, IAModel& ia_model);
  void buildInstanceNodeCurrent(PowerGraph& power_graph, uint64_t instance_id, InstancePower& instance_power, IAModel& ia_model);
  int32_t getInstancePowerGraphNum(uint64_t instance_id, PowerNetType power_net_type);
  void solveNodeVoltage(PowerGraph& power_graph, IAModel& ia_model);
  void updatePowerNodeVoltage(PowerGraph& power_graph, IAModel& ia_model, std::vector<double>& node_voltage_list);
};

}  // namespace iemir
