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

#include <memory>

#include "CornerData.hpp"
#include "CrossLayerOverlap.hpp"
#include "DataManager.hpp"
#include "EdgeEnvInterval.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "GridNeighborhood.hpp"
#include "grid/PatternGrid.hpp"
#include "ProcessConductor.hpp"
#include "RCXHeader.hpp"
#include "ReciprocalCap.hpp"
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
  struct DirectedCoupling
  {
    int32_t first, second, corner, lo, hi;
    double cap;
  };
  std::unique_ptr<pattern::GridNeighborhood> _neighborhood;
  std::shared_ptr<const pattern::PatternGrid> _grid;
  std::shared_ptr<const pattern::PatternGrid> _grid_override;
  std::vector<std::vector<DirectedCoupling>> _directed;
  void finishPatternCoupling();
  void addDirectedCoupling(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, TopoEdge* neighbor, int32_t lo, int32_t hi, double cap);
  void extractCapacitance();
  void extractCornerCapacitance(int32_t corner_idx);
  std::string extractNetCapacitance(int32_t corner_idx, int32_t net_idx);
  std::string extractEdgeCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx);
  std::string extractEdgeIntervalCapacitance(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx);
  std::string extractCapacitanceSpan(int32_t corner_idx, int32_t net_idx, int32_t edge_idx, int32_t interval_idx, int32_t start_coord, int32_t end_coord);
  void getCrossLayerName(std::vector<CrossLayerOverlap>& cross_layer_overlap_list, int32_t start_coord, int32_t end_coord, std::string& below_layer_name,
                         std::string& above_layer_name);
  const ProcessConductor* getProcessConductor(CornerData& corner_data, int32_t design_layer_idx);
};

}  // namespace ircx
