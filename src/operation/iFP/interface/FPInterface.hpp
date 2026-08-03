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

#include <any>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#if 1  // 前向声明

namespace idb {
class IdbInstance;
class IdbLayerCut;
class IdbSpecialWire;
class IdbVia;
enum class IdbConnectDirection : uint8_t;
enum class IdbOrient : uint8_t;
}  // namespace idb

#endif

namespace ifp {

class Instance;
class PGSegment;
enum class IOPinDirection;
enum class PlacementOrientation;

#define FPI (ifp::FPInterface::getInst())

class FPInterface
{
 public:
  static FPInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用FP的API

#if 1  // iFP
  void initFP(std::map<std::string, std::any> config_map);
  void runFP();
  void destroyFP();
#endif

#endif

#if 1  // FP调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  void wrapDBInfo();
  void wrapMicronDBU();
  void wrapManufactureGrid();
  void wrapCellArea();
  void wrapSiteMap();
  void wrapCellMasterMap();
  void wrapRoutingLayerList();
  void wrapInstanceList();
  void wrapMacroPinShapeList(idb::IdbInstance* idb_instance, Instance& instance);
  void wrapPlacedMacroPinShapeList(idb::IdbInstance* idb_instance, Instance& instance);
  void wrapUnplacedMacroPinShapeList(idb::IdbInstance* idb_instance, Instance& instance);
  PlacementOrientation wrapPlacementOrientation(idb::IdbOrient idb_orient);
  void wrapNetList();
  void wrapMacroPlacement();
  void wrapMacroPlacement(Instance& instance, double x_micron, double y_micron, PlacementOrientation orient);
  void wrapMacroNetPinList(Instance& instance);
  void wrapIOPinList();
#endif

#if 1  // output
  void output();
  void outputFloorplan();
  void outputDie();
  void outputCore();
  void outputRowList();
  idb::IdbOrient unwrapPlacementOrientation(PlacementOrientation orient);
  void outputTrackList();
  void outputPGNetList();
  idb::IdbConnectDirection unwrapIOPinDirection(IOPinDirection io_pin_direction);
  void outputIOPinList();
  void outputIOInstancePlacement();
  void outputMacroPlacement();
  void outputNewInstanceList();
  void outputPGSegmentList();
  void adjustPGLineSegmentListByViaEnclosure();
  idb::IdbVia* getIDBVia(idb::IdbLayerCut* idb_cut_layer, PGSegment& pg_segment);
  void adjustLineSegmentListByViaEnclosure(
      std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>>& pg_net_layer_coord_to_stripe_segment_list_map,
      std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>>& pg_net_layer_coord_to_rail_segment_list_map,
      PGSegment& pg_segment, std::string layer_name, int32_t enclosure_ll_x, int32_t enclosure_ll_y, int32_t enclosure_ur_x,
      int32_t enclosure_ur_y);
  bool adjustLineSegmentListByViaEnclosure(
      std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>>& pg_net_layer_coord_to_line_segment_list_map,
      PGSegment& pg_segment, std::string layer_name, int32_t enclosure_ll_x, int32_t enclosure_ll_y, int32_t enclosure_ur_x,
      int32_t enclosure_ur_y);
  bool adjustLineSegmentByViaEnclosure(std::vector<PGSegment*>& line_segment_list, int32_t enclosure_ll_x, int32_t enclosure_ll_y,
                                        int32_t enclosure_ur_x, int32_t enclosure_ur_y);
  void outputPGVia(idb::IdbSpecialWire* idb_special_wire, PGSegment& pg_segment);
#endif

#endif

#endif

 private:
  static FPInterface* _fp_interface_instance;

  FPInterface() = default;
  FPInterface(const FPInterface& other) = delete;
  FPInterface(FPInterface&& other) = delete;
  ~FPInterface() = default;
  FPInterface& operator=(const FPInterface& other) = delete;
  FPInterface& operator=(FPInterface&& other) = delete;
  // function
};

}  // namespace ifp
