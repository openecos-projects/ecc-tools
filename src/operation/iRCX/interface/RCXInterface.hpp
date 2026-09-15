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

#include "RCXHeader.hpp"

#if 1  // 前向声明

namespace idb {
class IdbLayerRouting;
class IdbNet;
class IdbPin;
class IdbRegularWireSegment;
class IdbVia;
}  // namespace idb

namespace ircx {
class Net;
}  // namespace ircx

#endif

namespace ircx {

#define RCXI (ircx::RCXInterface::getInst())

class RCXInterface
{
 public:
  static RCXInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用RCX的API

#if 1  // iRCX
  void initRCX(std::map<std::string, std::any> config_map);
  void runRCX();
  void destroyRCX();
  void compareSpef(std::map<std::string, std::any> config_map);
  void dumpNetShape();
  void runRCXFromTopo(std::map<std::string, std::any> config_map);
  void plotSpef(std::map<std::string, std::any> config_map);
#endif

#endif

#if 1  // RCX调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);

#if 1  // database
  void wrapDatabase();
  void wrapDBInfo();
  void wrapLayerList();
  void wrapRoutingLayer(idb::IdbLayerRouting* idb_routing_layer);
  void wrapSPEFNameData();
  void wrapNetList();
  void wrapNet(Net& net, idb::IdbNet* idb_net, int32_t net_idx);
  void wrapPinList(Net& net, idb::IdbNet* idb_net);
  void wrapPin(Net& net, idb::IdbPin* idb_pin, bool is_driver);
  std::string getSPEFName(std::string name);
  void wrapSegmentList(Net& net, idb::IdbNet* idb_net);
  void wrapSegment(Net& net, idb::IdbRegularWireSegment* idb_segment);
  void wrapPatch(Net& net, idb::IdbRegularWireSegment* idb_segment);
  void wrapViaList(Net& net, idb::IdbRegularWireSegment* idb_segment);
  void wrapVia(Net& net, idb::IdbVia* idb_via);
  void wrapSpecialNet();
#endif
#endif

#if 1  // output
  void output();
#endif

#endif

#endif

 private:
  static RCXInterface* _rcx_interface_instance;

  RCXInterface() = default;
  RCXInterface(const RCXInterface& other) = delete;
  RCXInterface(RCXInterface&& other) = delete;
  ~RCXInterface() = default;
  RCXInterface& operator=(const RCXInterface& other) = delete;
  RCXInterface& operator=(RCXInterface&& other) = delete;
  // function
  // 归一化 thread_num，避免过量起线程。
  static int32_t normalizeThreadNumber(const nlohmann::json& config_json);
};

}  // namespace ircx
