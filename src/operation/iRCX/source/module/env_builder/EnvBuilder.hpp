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

#include "DataManager.hpp"
#include "EBModel.hpp"
#include "EnvAxis.hpp"
#include "EnvLayerPixelOverlapList.hpp"
#include "EnvPixelOverlapMerger.hpp"
#include "EnvTrackIdx.hpp"
#include "LineSegment.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "RCXHeader.hpp"
#include "TopoEdge.hpp"
#include "TrackInfo.hpp"

namespace ircx {

#define RCXEB (ircx::EnvBuilder::getInst())

class EnvBuilder
{
 public:
  static void initInst();
  static EnvBuilder& getInst();
  static void destroyInst();
  // function
  void build();

 private:
  // self
  static EnvBuilder* _eb_instance;

  EnvBuilder() = default;
  EnvBuilder(const EnvBuilder& other) = delete;
  EnvBuilder(EnvBuilder&& other) = delete;
  ~EnvBuilder() = default;
  EnvBuilder& operator=(const EnvBuilder& other) = delete;
  EnvBuilder& operator=(EnvBuilder&& other) = delete;
  // function
  void buildEBModel(EBModel& eb_model);
  bool buildNetEnvList(EBModel& eb_model);
  std::vector<CrossLayerOverlap> getClippedCrossLayerOverlapList(const std::vector<CrossLayerOverlap>& cross_layer_overlap_list, int32_t start_coord,
                                                                 int32_t end_coord);
  std::vector<EnvLayerPixelOverlapList> getCrossLayerPixelOverlapList(EBModel& eb_model, const LineSegment& line_segment, int32_t base_layer_idx,
                                                                      bool is_upper_layer);
  bool buildTrackIdxMap(EBModel& eb_model);
  void addTopoEdgeToTrackIdx(EBModel& eb_model, TopoEdge& edge);
  bool initTrackIdx(EnvTrackIdx& track_idx, TrackInfo& track_info, GTLRectInt& die_shape, int32_t bucket_step, bool is_horizontal);
  EnvAxis getCoveredAxis(int32_t origin, int32_t count, int32_t step, int32_t lower_coord, int32_t upper_coord);
  bool buildPixelGridMap(EBModel& eb_model);
  void addTopoEdgeToPixelGrid(EBModel& eb_model, TopoEdge& edge);
  void buildSearchTrackNumMap(EBModel& eb_model);
};

}  // namespace ircx
