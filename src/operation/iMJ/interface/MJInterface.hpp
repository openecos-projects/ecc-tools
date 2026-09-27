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
#include <set>
#include <string>
#include <vector>

#if 1  // 前向声明

namespace idb {
class IdbLayerRouting;
class IdbLayerCut;
class IdbNet;
class IdbPin;
enum class IdbLayerDirection : uint8_t;
enum class IdbConnectType : uint8_t;
class IdbRegularWireSegment;
}  // namespace idb

namespace imj {
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
  void initMJ(std::map<std::string, std::any> config_map);
  void insertFiller(std::map<std::string, std::any> config_map);
  void insertMetal(std::map<std::string, std::any> config_map);
  void checkAntenna(std::map<std::string, std::any> config_map);
  void flattenDef(std::map<std::string, std::any> config_map);
  void destroyMJ();
#endif

#endif

#if 1  // MJ调用外部的API

#if 1  // TopData

#if 1  // input
  void input(std::map<std::string, std::any>& config_map);
  void wrapConfig(std::map<std::string, std::any>& config_map);
#endif

#if 1  // output
  void output();
#endif

#endif

#endif

 private:
  static MJInterface* _mj_interface_instance;

  MJInterface() = default;
  MJInterface(const MJInterface& other) = delete;
  MJInterface(MJInterface&& other) = delete;
  ~MJInterface() = default;
  MJInterface& operator=(const MJInterface& other) = delete;
  MJInterface& operator=(MJInterface&& other) = delete;
  // function
};

}  // namespace imj
