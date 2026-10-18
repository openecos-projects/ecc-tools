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

#include "DCModel.hpp"
#include "DCIOModel.hpp"
#include "IdbDesign.h"
#include "IdbLayout.h"

namespace imj {

#define MJDC (imj::DEFCutter::getInst())

class DEFCutter
{
 public:
  static void initInst();
  static DEFCutter& getInst();
  static void destroyInst();
  // function
  void cut(std::map<std::string, std::any>& config_map);

 private:
  static DEFCutter* _dc_instance;

  DEFCutter() = default;
  DEFCutter(const DEFCutter& other) = delete;
  DEFCutter(DEFCutter&& other) = delete;
  ~DEFCutter() = default;
  DEFCutter& operator=(const DEFCutter& other) = delete;
  DEFCutter& operator=(DEFCutter&& other) = delete;
  // function
  DCModel initDCModel(std::map<std::string, std::any>& config_map);
  void cutGridList(DCModel& dc_model);
  void cutGrid(DCGrid& grid, DCRect& boundary, bool is_row);
  void selectObjectList(std::vector<DCObject>& object_list, DCRect& boundary);
  void cutNetList(DCModel& dc_model);
  void buildBoundaryShapeList(DCNet& net, DCRoute& route, DCRect& input_rect, DCRect& die);
  void buildIOPinList(DCModel& dc_model);
  std::string getIOPinLayerName(DCModel& dc_model, std::vector<int32_t>& net_idx_list);
  bool isBoundaryRect(const DCRect& rect, const DCRect& die);
  DCPinType getIOPinType(DCModel& dc_model, std::vector<int32_t>& net_idx_list);
  std::array<int32_t, 2> getIOPinTargetCoord(DCModel& dc_model, std::vector<int32_t>& net_idx_list);
  void cutObjectList(std::vector<DCObject>& object_list, DCRect& boundary);
  void placeIOPinList(DCModel& dc_model);
  std::vector<DCIOSlot> buildIOSlotList(DCModel& dc_model);
  void buildIOEdgeSlotList(DCModel& dc_model, idb::IdbLayerRouting* layer, int32_t origin, int32_t pitch, int32_t count,
                           std::vector<DCIOSlot>& slot_list);
  int32_t getIOPinDepth(DCModel& dc_model, idb::IdbLayerRouting* layer, int32_t width, bool low_side);
  void buildIOSlotRTree(DCIOModel& io_model);
  int32_t getIOMaxSpacing(idb::IdbLayerRouting* layer);
  std::map<std::string, std::vector<DCRect>> buildIOObstacleMap(DCModel& dc_model);
  void blockIOSlotList(DCIOModel& io_model, DCLayerRect& obstacle);
  bool hasIOPinSpacing(DCLayerRect& first, DCLayerRect& second);
  double getIOPinDistance(DCIOPin& pin, const DCRect& rect);
  void updateDesign(DCModel& dc_model);
  void buildFloorplan(DCModel& dc_model, idb::IdbLayout* layout);
  void buildInstanceList(DCModel& dc_model, idb::IdbDesign* design);
  void buildPinList(DCModel& dc_model, idb::IdbDesign* design);
  void copyPin(idb::IdbPin* source, idb::IdbPin* pin);
  void buildNetList(DCModel& dc_model, idb::IdbDesign* design);
  void connectPin(idb::IdbDesign* design, idb::IdbPin* pin, idb::IdbNet* net, idb::IdbSpecialNet* special_net);
  void buildShapeList(DCModel& dc_model, idb::IdbDesign* design);
  void updateFloorplan(idb::IdbLayout& layout);
#if 1  // exhibit
  void writeReport(DCModel& dc_model);
#endif
};

}  // namespace imj
