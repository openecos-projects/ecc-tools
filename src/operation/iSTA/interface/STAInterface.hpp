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
class IdbNet;
class IdbPin;
class IdbPins;
enum class IdbConnectType : uint8_t;
enum class IdbConnectDirection : uint8_t;
}  // namespace idb

namespace ista {
enum class PinDirection;
class Net;
class Pin;
}  // namespace ista

#endif

namespace ista {

#define STAI (ista::STAInterface::getInst())

class STAInterface
{
 public:
  static STAInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用STA的API

#if 1  // iSTA
  void initSTA(std::map<std::string, std::any> config_map);
  void runSTA();
  void extractLib();
  void destroySTA();
#endif

#endif

#if 1  // STA调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
  void wrapDatabase();
  void wrapDBInfo();
  void wrapInstanceList();
  void wrapInstance(idb::IdbInstance* idb_instance);
  void wrapInstancePinList(idb::IdbInstance* idb_instance);
  void wrapInstancePin(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
  bool wrapSignalConnectType(idb::IdbConnectType connect_type);
  std::string wrapInstancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin);
  PinDirection wrapPinDirection(idb::IdbConnectDirection idb_direction);
  void wrapPinCoordinate(Pin& pin, idb::IdbPin* idb_pin);
  void wrapPortList();
  void wrapPortPin(idb::IdbPin* idb_pin);
  std::string wrapPinName(idb::IdbPin* idb_pin);
  void wrapNetList();
  void wrapNet(idb::IdbNet* idb_net);
  void wrapNetPinList(idb::IdbNet* idb_net, Net& net);
  void wrapNetPinList(idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net);
  void wrapNetPin(idb::IdbPin* idb_pin, Net& net);
  std::string wrapNetIOPinName(idb::IdbPin* idb_pin);
  std::string wrapNetInstancePinName(idb::IdbPin* idb_pin);
  void wrapNetPinNameList(Net& net, std::string& pin_name);
  void wrapNetToDatabase(Net& net);
#endif

#if 1  // output
  void output();
#endif

#endif

#endif

 private:
  static STAInterface* _sta_interface_instance;

  STAInterface() = default;
  STAInterface(const STAInterface& other) = delete;
  STAInterface(STAInterface&& other) = delete;
  ~STAInterface() = default;
  STAInterface& operator=(const STAInterface& other) = delete;
  STAInterface& operator=(STAInterface&& other) = delete;
  // function
};

}  // namespace ista
