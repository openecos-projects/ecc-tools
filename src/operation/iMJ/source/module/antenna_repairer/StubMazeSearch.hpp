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

#include <cstdint>
#include <utility>
#include <vector>

#include "ARInternalTypes.hpp"
#include "IdbGeometry.h"

namespace idb {
class IdbNet;
}  // namespace idb

namespace imj {

class LocalStubRouter;

struct CutObstacle
{
  idb::IdbRect rect;
  int32_t spacing = 0;
};

struct StubMazeProblem
{
  const LocalStubRouter* router = nullptr;
  idb::IdbNet* target_net = nullptr;
  int max_layer_idx = -1;
  int32_t sx0 = 0;
  int32_t sy0 = 0;
  idb::IdbRect win_rect;
  const std::vector<LayerShapeBox>* filtered_starts = nullptr;
  const std::vector<std::pair<int64_t, LayerShapeBox>>* ranked_goals = nullptr;
  const std::vector<std::vector<RoutingObstacleBox>>* obs_by_layer = nullptr;
  const std::vector<std::vector<CutObstacle>>* cut_obs_by_low_idx = nullptr;
  const std::vector<CutShapeBox>* base_cuts = nullptr;
  const std::vector<std::vector<idb::IdbRect>>* force_max_spacing_obs = nullptr;
  const std::vector<std::vector<idb::IdbRect>>* force_avoid_obs = nullptr;
};

RoutePlan runStubMazeSearch(const StubMazeProblem& problem);

}  // namespace imj
