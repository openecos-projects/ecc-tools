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

#include "Logger.hpp"
#include "Monitor.hpp"
#include "PlanarCoord.hpp"
#include "Segment.hpp"
#include "TBTask.hpp"

namespace irt {

#define RTTB (irt::TOPOBuilder::getInst())

struct TBSteinerRepairStat
{
  int32_t raw_steiner_in_macro = 0;
  int32_t fixed_steiner_in_macro = 0;
  int32_t failed_steiner_legalize_num = 0;
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
  std::vector<Segment<PlanarCoord>> getPlanarTopoList(const TBTask& tb_task, TBSteinerRepairStat& steiner_repair_stat);
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
  // function
  std::vector<Segment<PlanarCoord>> getFlutePlanarTopoList(const std::vector<PlanarCoord>& planar_coord_list);
  std::vector<Segment<PlanarCoord>> legalizePlanarTopo(const TBTask& tb_task, std::vector<Segment<PlanarCoord>> raw_topo_list,
                                                      TBSteinerRepairStat& steiner_repair_stat);
  PlanarCoord getNearestLegalCoord(const std::vector<PlanarRect>& planar_obs_list, const PlanarRect& planar_search_region,
                                   const PlanarCoord& coord);
  bool isSteinerForbiddenCoord(const std::vector<PlanarRect>& planar_obs_list, const PlanarCoord& coord);
};

}  // namespace irt
