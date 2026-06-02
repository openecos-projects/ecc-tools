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

namespace izh {
class RoutingLayer;
class CutLayer;
class Violation;
enum class ViolationType;
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
}  // namespace izh

namespace ieda_feature {
class ZHSummary;
class FeatureManager;
}  // namespace ieda_feature

#endif

namespace izh {

#define ZHI (izh::ZHInterface::getInst())

class ZHInterface
{
 public:
  static ZHInterface& getInst();
  static void destroyInst();

#if 1  // 外部调用ZH的API

#if 1  // izh
  void fixFanout(std::map<std::string, std::any> config_map);
  void insertFiller(std::map<std::string, std::any> config_map);
#endif

#endif

 private:
  static ZHInterface* _zh_interface_instance;

  ZHInterface() = default;
  ZHInterface(const ZHInterface& other) = delete;
  ZHInterface(ZHInterface&& other) = delete;
  ~ZHInterface() = default;
  ZHInterface& operator=(const ZHInterface& other) = delete;
  ZHInterface& operator=(ZHInterface&& other) = delete;
  // function
};

}  // namespace izh
