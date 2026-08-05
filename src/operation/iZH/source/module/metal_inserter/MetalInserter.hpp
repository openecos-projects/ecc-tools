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

#include "Logger.hpp"
#include "GeometryLayerMetadata.h"
#include "GeometryStore.h"
#include "MIModel.hpp"
#include "Monitor.hpp"
#include "json.hpp"

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
  MIModel initMIModel(std::map<std::string, std::any>& config_map);
  void initRuleFilePath(MIModel& mi_model, std::map<std::string, std::any>& config_map);
  void initFillArea(MIModel& mi_model, std::map<std::string, std::any>& config_map);
  void initResetFill(MIModel& mi_model, std::map<std::string, std::any>& config_map);
  void initLayerRuleList(MIModel& mi_model);
  std::vector<std::string> getLayerNameList(nlohmann::json& layer_group);
  std::vector<MIFillShape> getFillShapeList(nlohmann::json& non_opc, int32_t dbu_per_micron);
  std::vector<double> getNumberList(nlohmann::json& number_config);
  int32_t getDbuValue(double micron_value, int32_t dbu_per_micron);
  void buildMetalFill(MIModel& mi_model);
  void resetMetalFill(MIModel& mi_model);
  void initLayerDirection(MILayerRule& layer_rule);
  void buildLayerMetalFill(MIModel& mi_model, ecc::geometry::GeometryStore& geometry_store,
                           const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list, MILayerRule& layer_rule);
  int32_t getGeometryLayerIdx(const std::vector<ecc::geometry::GeometryLayerMetadata>& geometry_layer_list, const MILayerRule& layer_rule);
  std::vector<MIRect> buildFillRectList(ecc::geometry::GeometryStore& geometry_store, int32_t geometry_layer_idx,
                                        const MILayerRule& layer_rule, const MIRect& fill_area);
  MIFillShape getOrientFillShape(MIFillShape fill_shape, bool is_horizontal);
  bool isBlocked(ecc::geometry::GeometryStore& geometry_store, int32_t geometry_layer_idx, const MIRect& metal_rect, int32_t spacing);
  ecc::geometry::Rect32 getGeometryRect(const MIRect& metal_rect);
  void writeMetalFill(MIModel& mi_model, const MILayerRule& layer_rule, const std::vector<MIRect>& metal_rect_list);
};

}  // namespace izh
