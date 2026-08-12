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

#if 1  // 前向声明

namespace idb {
class IdbLayerShape;
class IdbPin;
class IdbSpecialNet;
class IdbSpecialWireSegment;
class IdbVia;
class IdbViaMaster;
enum class IdbConnectType : uint8_t;
}  // namespace idb

namespace iemir {
class PowerNet;
enum class PowerNetType;
}  // namespace iemir

#endif

namespace iemir {

#define EMIRI (iemir::EMIRInterface::getInst())

class EMIRInterface
{
 public:
  static EMIRInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用EMIR的API

#if 1  // iEMIR
  void initEMIR(std::map<std::string, std::any> config_map);
  void runEMIR();
  void destroyEMIR();
#endif

#endif

#if 1  // EMIR调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  void wrapDBInfo();
  void wrapInstanceIdSet();
  void wrapPowerNetList();
  void wrapPowerNet(idb::IdbSpecialNet* idb_power_net);
  PowerNetType wrapPowerNetType(idb::IdbConnectType connect_type);
  void wrapPowerWireSegmentList(PowerNet& power_net, idb::IdbSpecialNet* idb_power_net);
  void wrapPowerWireSegment(PowerNet& power_net, idb::IdbSpecialWireSegment* idb_segment);
  void wrapPowerVia(PowerNet& power_net, idb::IdbVia* idb_via);
  double getGeneratedViaResistance(idb::IdbViaMaster* idb_via_master, int32_t cut_num);
  void wrapPowerPinList(PowerNet& power_net, idb::IdbSpecialNet* idb_power_net);
  void wrapPowerPin(PowerNet& power_net, idb::IdbPin* idb_pin, bool is_source);
  void wrapPowerPinShape(PowerNet& power_net, idb::IdbPin* idb_pin, idb::IdbLayerShape* idb_layer_shape, bool is_source);
#endif

#if 1  // output
  void output();
#endif

#endif

#endif

 private:
  static EMIRInterface* _emir_interface_instance;

  EMIRInterface() = default;
  EMIRInterface(const EMIRInterface& other) = delete;
  EMIRInterface(EMIRInterface&& other) = delete;
  ~EMIRInterface() = default;
  EMIRInterface& operator=(const EMIRInterface& other) = delete;
  EMIRInterface& operator=(EMIRInterface&& other) = delete;
  // function
};

}  // namespace iemir
