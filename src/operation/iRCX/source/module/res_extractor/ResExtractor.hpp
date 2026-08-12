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

#include "CornerData.hpp"
#include "DataManager.hpp"
#include "EdgeEtchInterval.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "ProcessConductor.hpp"
#include "ProcessVia.hpp"
#include "RCXHeader.hpp"
#include "REModel.hpp"
#include "TopoEdge.hpp"

namespace ircx {

#define RCXRE (ircx::ResExtractor::getInst())

class ResExtractor
{
 public:
  static void initInst();
  static ResExtractor& getInst();
  static void destroyInst();
  // function
  void extract();

 private:
  // self
  static ResExtractor* _re_instance;

  ResExtractor() = default;
  ResExtractor(const ResExtractor& other) = delete;
  ResExtractor(ResExtractor&& other) = delete;
  ~ResExtractor() = default;
  ResExtractor& operator=(const ResExtractor& other) = delete;
  ResExtractor& operator=(ResExtractor&& other) = delete;
  // function
  void extractResistance();
  void extractCornerResistance(int32_t corner_idx);
  void extractNetResistance(int32_t corner_idx, int32_t net_idx);
  double extractWireResistance(CornerData& corner_data, ProcessConductor& conductor, TopoEdge& edge,
                                std::span<EdgeEtchInterval> edge_interval_list);
  double extractViaResistance(CornerData& corner_data, ProcessVia& via, TopoEdge& edge);
  double getTmprFactor(double tmpr, double nominal_tmpr, double tmpr_coefficient1, double tmpr_coefficient2);
  ProcessVia* getProcessVia(CornerData& corner_data, int32_t design_layer_idx);
  ProcessConductor* getProcessConductor(CornerData& corner_data, int32_t design_layer_idx);
};

}  // namespace ircx
