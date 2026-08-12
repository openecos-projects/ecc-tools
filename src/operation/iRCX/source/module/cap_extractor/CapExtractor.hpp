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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "CapTableConfig.hpp"
#include "CEModel.hpp"
#include "CornerData.hpp"
#include "CrossLayerOverlap.hpp"
#include "DataManager.hpp"
#include "EdgeEnvInterval.hpp"
#include "EdgeEtchInterval.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "ProcessConductor.hpp"
#include "RCXHeader.hpp"
#include "TopoEdge.hpp"

namespace ircx {

#define RCXCE (ircx::CapExtractor::getInst())

class CapExtractor
{
 public:
  static void initInst();
  static CapExtractor& getInst();
  static void destroyInst();
  // function
  void extract();

 private:
  // self
  static CapExtractor* _ce_instance;

  CapExtractor() = default;
  CapExtractor(const CapExtractor& other) = delete;
  CapExtractor(CapExtractor&& other) = delete;
  ~CapExtractor() = default;
  CapExtractor& operator=(const CapExtractor& other) = delete;
  CapExtractor& operator=(CapExtractor&& other) = delete;
  // function
  void extractCapacitance();
  void extractCornerCapacitance(int32_t corner_idx);
  void extractNetCapacitance(int32_t corner_idx, int32_t net_idx);
  void extractEdgeCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx);
  void extractEdgeIntervalCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx);
  void extractCapacitanceSpan(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx, int32_t start_coord,
                              int32_t end_coord);
  void getCrossLayerName(std::vector<CrossLayerOverlap>& cross_layer_overlap_list, int32_t start_coord, int32_t end_coord,
                         std::string& below_layer_name, std::string& above_layer_name);
  void addGroundCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, TopoEdge* adjacent_edge,
                            double ground_capacitance);
  void addCouplingCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, TopoEdge* adjacent_edge,
                              double coupling_capacitance);
  ProcessConductor* getProcessConductor(CornerData& corner_data, int32_t design_layer_idx);
  CapTableConfig* getCapTableConfig(CornerData& corner_data, std::string& process_layer_name, std::string& below_layer_name,
                                    std::string& above_layer_name);
  void getCapacitance(CapTableConfig& cap_table_config, double spacing, double& coupling_capacitance, double& ground_capacitance);
  void getFarthestCapacitance(CapTableConfig& cap_table_config, double& coupling_capacitance, double& ground_capacitance);
};

}  // namespace ircx
