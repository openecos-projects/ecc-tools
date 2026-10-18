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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Database.hpp"
#include "GBColorType.hpp"

namespace ista {

#define STAGB (ista::GraphBuilder::getInst())

class GraphBuilder
{
 public:
  static void initInst();
  static GraphBuilder& getInst();
  static void destroyInst();
  // function
  void build();

 private:
  // self
  static GraphBuilder* _gb_instance;

  GraphBuilder() = default;
  GraphBuilder(const GraphBuilder& other) = delete;
  GraphBuilder(GraphBuilder&& other) = delete;
  ~GraphBuilder() = default;
  GraphBuilder& operator=(const GraphBuilder& other) = delete;
  GraphBuilder& operator=(GraphBuilder&& other) = delete;
  // function
  void buildTimingPointList();
  void normalizePinDirectionByTimingCell();
  void buildCellArcs();
  bool buildLibraryCellArcs(Instance& instance);
  std::string getInstancePinName(Instance& instance, std::string& port_name);
  void addCellArc(Instance& instance, TimingCellArc& timing_cell_arc);
  void addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name, const std::string& library_source_port,
              const std::string& library_sink_port, bool is_clock_arc, bool is_disable_arc, TimingCellArc* timing_cell_arc);
  std::vector<std::string> collectInputPins(Instance& instance);
  bool isInputLike(PinDirection direction);
  std::vector<std::string> collectOutputPins(Instance& instance);
  bool isOutputLike(PinDirection direction);
  void buildInoutPinDirectionByGraph();
  std::map<std::string, PinDirection> makeInoutPinDirectionMap();
  bool isFloatingInoutPin(Pin& pin);
  PinDirection inferInoutPinDirection(const std::string& pin_name, Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map);
  PinDirection inferInoutPinDirectionByTimingCell(Pin& pin);
  TimingCellPort* getTimingCellPort(Pin& pin);
  PinDirection inferInoutPinDirectionByTimingGraph(const std::string& pin_name);
  bool hasOutgoingCellArc(const std::string& pin_name);
  bool hasIncomingCellArc(const std::string& pin_name);
  PinDirection inferInoutPinDirectionByNet(Pin& pin, std::map<std::string, PinDirection>& inout_pin_direction_map);
  Net* getPinNet(Pin& pin);
  std::vector<std::string> getDriverPinList(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map);
  int32_t getDriverPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map);
  int32_t getUnresolvedInoutPinNum(Net& net, std::map<std::string, PinDirection>& inout_pin_direction_map);
  bool isResolvedDriverPin(const std::string& pin_name, std::map<std::string, PinDirection>& inout_pin_direction_map);
  bool isDriverDirection(Pin& pin, PinDirection direction);
  PinDirection getDriverPinDirection(Pin& pin);
  PinDirection getLoadPinDirection(Pin& pin);
  void rebuildCellArcListByPinDirection();
  void buildNetDriverLoadList();
  void makeNetDriverLoad(Net& net);
  bool isDriverPin(Pin& pin);
  std::string getPinNameListString(std::vector<std::string>& pin_name_list);
  void buildNetArcs();
  void buildGraphVertexList();
  int32_t addGraphVertex(const std::string& pin_name, bool is_driver, TimingPoint& point);
  void addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name);
  bool shouldDisableNetArc(const std::string& source_pin, const std::string& sink_pin);
  bool isDisableArc(Arc& arc);
  void buildStartEndPointList();
  bool isStartPoint(const std::string& pin_name, Pin& pin);
  bool isRegisterClockStartPoint(const std::string& pin_name, Pin& pin);
  bool isClockPin(const std::string& pin_name, Pin& pin);
  bool isClockSource(const std::string& pin_name);
  bool hasIncomingArc(const std::string& pin_name);
  bool isStartPort(Pin& pin);
  bool isEndPoint(const std::string& pin_name, Pin& pin);
  bool isTimingCheckEndPoint(const std::string& pin_name, Pin& pin);
  bool hasOutgoingArc(const std::string& pin_name);
  bool isEndPort(Pin& pin);
  void breakLoopArcList();
  void buildTimingOrder();
  void printLoopInfo();
  void initializeArcTiming();
};

}  // namespace ista
