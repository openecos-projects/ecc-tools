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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "GeometryLayerMetadata.h"
#include "GeometryStore.h"
#include "IdbLayer.h"
#include "Logger.hpp"
#include "MIModel.hpp"
#include "Monitor.hpp"

namespace izh {

#define ZHMI (izh::MetalInserter::getInst())

class MetalInserter
{
 public:
  static void initInst();
  static MetalInserter& getInst();
  static void destroyInst();
  // function
  void insert(std::map<std::string, std::any> config_map);

 private:
  // self
  static MetalInserter* _mi_instance;

  MetalInserter() = default;
  MetalInserter(const MetalInserter& other) = delete;
  MetalInserter(MetalInserter&& other) = delete;
  ~MetalInserter() = default;
  MetalInserter& operator=(const MetalInserter& other) = delete;
  MetalInserter& operator=(MetalInserter&& other) = delete;
  // function

#if 1  // 初始化

  MIModel initMIModel(std::map<std::string, std::any>& config_map);
  void setMIComParam(MIModel& mi_model, std::map<std::string, std::any>& config_map);
  void initDatabaseInfo(MIModel& mi_model);
  void initMILayerList(MIModel& mi_model);
  int32_t getRoutingLayerIdx(const std::vector<idb::IdbLayer*>& idb_routing_layer_list, const std::string& layer_name);
  MILayer initMILayer(idb::IdbLayerRouting* idb_routing_layer,
                      const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list);
  int32_t getGeometryLayerIdx(const std::string& layer_name,
                              const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list);
  int32_t getMinArea(idb::IdbLayerRouting* idb_routing_layer);
  int32_t getCeilDiv(int32_t dividend, int32_t divisor);
  int32_t getMaxSpacing(idb::IdbLayerRouting* idb_routing_layer);

#endif

#if 1  // 构建

  void buildMetalFill(MIModel& mi_model);
  void buildGeometryStore(ecc::geometry::GeometryStore& geometry_store);
  void buildLayerMetalFill(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer);
  void buildDensityWindowList(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer);
  double getMetalArea(ecc::geometry::GeometryStore& geometry_store, int32_t geometry_layer_idx, const MIRect& rect);
  bool isMetalShape(ecc::geometry::OwnerType owner_type);
  double getUnionArea(const std::vector<MIRect>& rect_list);
  void buildLayerFill(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer);
  std::vector<int32_t> getTrackCoordList(MILayer& mi_layer, const MIRect& rect);
  int32_t getTrackStep(MILayer& mi_layer);
  int32_t getFirstTrackCoord(int32_t coordinate, int32_t track_start, int32_t track_pitch);
  std::vector<MIRect> getFillRectList(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store, MILayer& mi_layer,
                                      const MIRect& fill_region_rect, int32_t track_coord);
  std::vector<std::pair<int32_t, int32_t>> getBlockedCoordIntervalList(ecc::geometry::GeometryStore& geometry_store,
                                                                        MILayer& mi_layer, const MIRect& fill_rect);
  MIRect getFillRect(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_region_rect, int32_t track_coord);
  std::vector<MIRect> getUniformFillRectList(MIModel& mi_model, MILayer& mi_layer, const MIRect& free_fill_rect);
  int32_t getFillSpacing(MILayer& mi_layer, int32_t parallel_length);
  int32_t getAlignUp(int32_t coordinate, int32_t grid);
  int32_t getAlignDown(int32_t coordinate, int32_t grid);
  bool isIgnoredShape(ecc::geometry::OwnerType owner_type);
  int32_t getRequiredSpacing(MILayer& mi_layer, const MIRect& first_rect, const MIRect& second_rect);
  idb::IdbLayerRouting* getRoutingLayer(const std::string& layer_name);
  int32_t getDefaultSpacing(idb::IdbLayerRouting* idb_routing_layer, int32_t wire_width);
  int32_t getPRLSpacing(idb::IdbLayerRouting* idb_routing_layer, int32_t wire_width, int32_t parallel_length);
  int32_t getParallelLength(const MIRect& first_rect, const MIRect& second_rect);
  std::vector<int32_t> getAffectedDensityWindowIdxList(MIModel& mi_model, MILayer& mi_layer, const MIRect& rect);
  void addFillRect(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect);
  void updateDensityWindowList(MIModel& mi_model, MILayer& mi_layer, const MIRect& fill_rect);

#endif

#if 1  // 输出

  void writeMetalFill(MIModel& mi_model);
  void printResult(MIModel& mi_model);
  void printLayerResult(MIModel& mi_model, MILayer& mi_layer);

#endif
};

}  // namespace izh
