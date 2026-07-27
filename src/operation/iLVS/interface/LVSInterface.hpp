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
#include <map>
#include <string>
#include <unordered_set>

#if 1  // 前置声明

namespace idb {
class IdbDesign;
class IdbInstance;
class IdbLayer;
class IdbPin;
class IdbPins;
class IdbRect;
class IdbRegularWireSegment;
class IdbVia;
}  // namespace idb

namespace ilvs {
enum class ConnectType;
class DefData;
class DesignData;
class NetRoutingData;
class NetlistData;
class Net;
class RoutingShape;
class Shape;
}  // namespace ilvs

#endif

namespace ilvs {

#define LVSI (ilvs::LVSInterface::getInst())

class LVSInterface
{
 public:
  static LVSInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用LVS的API

#if 1  // iLVS
  void initLVS(std::map<std::string, std::any> config_map);
  void runLVS();
  void destroyLVS();
#endif

#endif

#if 1  // LVS调用外部的API

#if 1  // 顶层数据

#if 1  // 输入
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  NetlistData wrapNetlistData(idb::IdbDesign* idb_design);
  DefData wrapDefData(idb::IdbDesign* idb_design);
  void wrapDesignData(idb::IdbDesign* idb_design, DesignData& design_data);
  void wrapInstanceList(idb::IdbDesign* idb_design, DesignData& design_data);
  void wrapInstance(idb::IdbInstance* idb_instance, DesignData& design_data);
  void wrapIOPinList(idb::IdbDesign* idb_design, DesignData& design_data);
  std::string wrapDesignTerminal(idb::IdbPin* idb_pin, DesignData& design_data);
  void wrapNetList(idb::IdbDesign* idb_design, DesignData& design_data);
  void wrapNetPinList(idb::IdbPins* idb_pin_list, Net& net, DesignData& design_data);
  void wrapPowerGroundTerminal(idb::IdbDesign* idb_design, DesignData& design_data);
  void wrapPowerGroundPin(idb::IdbPin* idb_pin, DesignData& design_data, ConnectType connect_type,
                          std::unordered_set<idb::IdbPin*>& idb_pin_set);
  void wrapDefRoutingData(idb::IdbDesign* idb_design, DefData& def_data);
  void wrapNetRoutingData(idb::IdbDesign* idb_design, DefData& def_data);
  void wrapRoutingDataPin(const std::string& net_name, idb::IdbPin* idb_pin, bool is_power_net, bool is_ground_net,
                          DefData& def_data);
  RoutingShape wrapRoutingDataShape(idb::IdbLayer* idb_layer, const idb::IdbRect& idb_rect, bool is_supply_route_shape);
  void wrapRoutingDataVia(idb::IdbVia* idb_via, NetRoutingData& net_routing_data);
  void wrapSpecialNetRoutingData(idb::IdbDesign* idb_design, DefData& def_data);
  Shape wrapShape(int32_t layer_idx, idb::IdbRect idb_rect);
  idb::IdbRect getPhysicalSegmentRect(idb::IdbRegularWireSegment* idb_segment);
  std::string getTerminalName(idb::IdbPin* idb_pin);
#endif

#if 1  // 输出
  void output();
#endif

#endif

#endif

 private:
  static LVSInterface* _lvs_interface_instance;

  LVSInterface() = default;
  LVSInterface(const LVSInterface& other) = delete;
  LVSInterface(LVSInterface&& other) = delete;
  ~LVSInterface() = default;
  LVSInterface& operator=(const LVSInterface& other) = delete;
  LVSInterface& operator=(LVSInterface&& other) = delete;
};

}  // namespace ilvs
