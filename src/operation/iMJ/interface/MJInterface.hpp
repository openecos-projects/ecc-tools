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
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if 1  // 前向声明

namespace idb {
class IdbDesign;
class IdbLayout;
class IdbRect;
class IdbVia;
class IdbSpecialNet;
class IdbLayerRouting;
class IdbLayerCut;
class IdbNet;
class IdbPin;
enum class IdbLayerDirection : uint8_t;
enum class IdbConnectType : uint8_t;
class IdbRegularWireSegment;
}  // namespace idb

namespace imj {
class DAModel;
class DAPin;
class DCModel;
class DCNet;
class DCRect;
class DCRoute;
class DCLayerRect;
enum class DCPinType;
class RoutingLayer;
class CutLayer;
class LayerCoord;
class LayerRect;
template <typename T>
class Segment;
class Net;
class Pin;
enum class Direction;
enum class ConnectType;
class EXTLayerRect;
class TAPanel;
class PlanarCoord;
}  // namespace imj

namespace ecc_feature {
class MJSummary;
class FeatureManager;
}  // namespace ecc_feature

#endif

namespace imj {

struct NativeAntennaSummary
{
  bool checked = false;
  bool coverage_complete = false;
  uint64_t net_count = 0;
  uint64_t connected_net_count = 0;
  uint64_t wire_count = 0;
  uint64_t violation_count = 0;
  uint64_t rule_layer_count = 0;
  uint64_t covered_rule_layer_count = 0;
  uint64_t skipped_rule_layer_count = 0;
  // Diagnostic only: routed nets without an explicit ANTENNAGATEAREA. A
  // diffusion-only terminal can still provide a valid antenna denominator.
  uint64_t nets_without_gate = 0;
  // Final per-net coverage diagnostic: routed nets with neither positive gate
  // nor positive diffusion antenna area on any terminal.
  uint64_t nets_without_terminal_antenna_info = 0;
  uint64_t pins_missing_antenna_info = 0;
  uint64_t pins_with_gate_area = 0;
  // Per-layer-event diagnostic. A component can gain a gate through a later
  // layer/via event, so this is not a final net-coverage failure by itself.
  uint64_t components_without_gate = 0;
  uint64_t skipped_segments = 0;
  uint64_t conductors_out_of_range = 0;
  uint64_t partial_areas_dropped = 0;
  // Number of antenna oxide models omitted because native checking currently
  // selects only the default/first model.
  uint64_t unsupported_oxide_model_count = 0;
  uint64_t unsupported_polygon_count = 0;
  uint64_t unsupported_route_shape_count = 0;
  uint64_t via_geometry_count = 0;
  std::string rule_file;
  std::string report_path;
  std::vector<std::string> violations;
  std::vector<std::string> coverage_diagnostics;
};

using MJConfigMap = std::map<std::string, std::any>;

// An external physical-design backend supplies only the operations that need
// its storage.  The iMJ algorithms and legacy iDB path remain backend-neutral.
struct DesignOperations
{
  std::function<void(MJConfigMap&)> insert_filler;
  std::function<void(MJConfigMap&)> insert_metal;
  std::function<NativeAntennaSummary(MJConfigMap&)> check_antenna;
};

#define MJI (imj::MJInterface::getInst())

class MJInterface
{
 public:
  static MJInterface& getInst()
  {
    if (_mj_interface_instance == nullptr) {
      _mj_interface_instance = new MJInterface();
    }
    return *_mj_interface_instance;
  }
  static void destroyInst();

#if 1  // 外部调用MJ的API

#if 1  // iMJ
  void initMJ(MJConfigMap config_map);
  void insertFiller(MJConfigMap config_map);
  void insertMetal(MJConfigMap config_map);
  void checkAntenna(MJConfigMap config_map);
  void flattenDef(MJConfigMap config_map);
  void analyzeDriver();
  void cutDEF(MJConfigMap config_map);
  void destroyMJ();
  void abortMJ() noexcept;
#endif

#endif

#if 1  // MJ调用外部的API

#if 1  // TopData

#if 1  // input
  void input(MJConfigMap& config_map);
  void wrapConfig(MJConfigMap& config_map);
  void wrapDriverDesign(DAModel& da_model);
  std::set<std::string> getDriverPGNetNameSet(idb::IdbDesign* design);
  std::vector<idb::IdbPin*> getSpecialNetPinList(idb::IdbDesign* design, idb::IdbSpecialNet* net, bool is_io);
  DAPin wrapDriverPin(idb::IdbPin* pin, bool is_io);
  void wrapCutDesign(DCModel& dc_model);
  void wrapCutFloorplan(DCModel& dc_model);
  DCRect wrapCutRect(idb::IdbRect* rect);
  void wrapCutObjectList(DCModel& dc_model);
  std::vector<DCLayerRect> wrapCutPinShapeList(idb::IdbPin* pin);
  std::vector<DCLayerRect> wrapCutViaShapeList(idb::IdbVia* via);
  DCRect wrapCutViaRect(idb::IdbVia* via);
  void wrapCutNetList(DCModel& dc_model);
  DCPinType wrapCutPinType(idb::IdbPin* pin);
  std::set<std::string> wrapCutPinLayerNameSet(idb::IdbPin* pin);
  void wrapCutRouteList(DCNet& net);
  DCRect wrapCutWireRect(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, int32_t ext1, int32_t ext2);
#endif

#if 1  // output
  void output();
#endif

#endif

#endif

  // Supplying any operation selects an external design source. Clearing the
  // group restores the legacy iDB implementation for all iMJ stages.
  void setDesignOperations(DesignOperations operations);
  void clearDesignOperations() noexcept { _design_operations = {}; }
  [[nodiscard]] bool hasDesignOperations() const noexcept;

  // Runtime-only result from an external design operation. The legacy checker
  // keeps its upstream reporting API; its adaptor returns a summary separately.
  // A zero violation count must be read with checked/coverage_complete.
  [[nodiscard]] const NativeAntennaSummary& lastAntennaSummary() const noexcept { return _last_antenna_summary; }

 private:
  static MJInterface* _mj_interface_instance;
  DesignOperations _design_operations;
  NativeAntennaSummary _last_antenna_summary;

  MJInterface() = default;
  MJInterface(const MJInterface& other) = delete;
  MJInterface(MJInterface&& other) = delete;
  ~MJInterface() = default;
  MJInterface& operator=(const MJInterface& other) = delete;
  MJInterface& operator=(MJInterface&& other) = delete;
  // function
};

}  // namespace imj
