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

#include "Segment.hpp"
#include "TBTask.hpp"

namespace irt {

#define RTTB (irt::TOPOBuilder::getInst())

struct TBRefineStat
{
  int32_t shifted_edge_num = 0;
  int32_t refined_steiner_num = 0;
  bool attempted_congestion_flute = false;
  bool used_congestion_flute = false;
  bool attempted_steiner_refine = false;
  bool used_steiner_refine = false;
  bool used_terminal_mst = false;
};

class TOPOBuilder
{
 public:
  static void initInst();
  static TOPOBuilder& getInst();
  static void destroyInst();
  // function
  void init();
  std::vector<Segment<PlanarCoord>> getPlanarTopoList(const TBTask& tb_task);
  std::vector<Segment<PlanarCoord>> getPlanarTopoList(const TBTask& tb_task, TBRefineStat& refine_stat);
  void destroy();

 private:
  // self
  static TOPOBuilder* _tb_instance;

  TOPOBuilder() = default;
  TOPOBuilder(const TOPOBuilder& other) = delete;
  TOPOBuilder(TOPOBuilder&& other) = delete;
  ~TOPOBuilder() = default;
  TOPOBuilder& operator=(const TOPOBuilder& other) = delete;
  TOPOBuilder& operator=(TOPOBuilder&& other) = delete;
};

}  // namespace irt
