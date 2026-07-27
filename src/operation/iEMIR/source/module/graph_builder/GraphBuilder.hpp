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

#include "Database.hpp"
#include "GBModel.hpp"
#include "PowerNet.hpp"
#include "PowerNodeType.hpp"

namespace iemir {

#define EMIRGB (iemir::GraphBuilder::getInst())

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
  GBModel initGBModel();
  void buildPowerGraphList();
  void buildPowerGraph(PowerNet& power_net);
  void initPowerGraph(PowerGraph& power_graph, PowerNet& power_net);
  void buildWireNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model);
  void buildWireEndpointNode(PowerGraph& power_graph, PowerWireSegment& power_wire_segment, std::size_t segment_idx, GBModel& gb_model);
  std::size_t getPowerNode(PowerGraph& power_graph, int32_t layer_idx, int32_t x, int32_t y, PowerNodeType power_node_type);
  void buildWireIntersectionNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model);
  void buildWireIntersectionNode(PowerGraph& power_graph, PowerWireSegment& first_power_wire_segment,
                                 PowerWireSegment& second_power_wire_segment, std::size_t first_segment_idx,
                                 std::size_t second_segment_idx, GBModel& gb_model);
  void appendWireCoordinateNode(PowerGraph& power_graph, PowerWireSegment& first_power_wire_segment,
                                PowerWireSegment& second_power_wire_segment, std::size_t first_segment_idx,
                                std::size_t second_segment_idx, int32_t x, int32_t y, GBModel& gb_model);
  bool getWireIntersectionCoordinate(PowerWireSegment& first_power_wire_segment, PowerWireSegment& second_power_wire_segment,
                                     int32_t& x, int32_t& y);
  void appendWireNodeId(GBModel& gb_model, std::size_t segment_idx, std::size_t node_id);
  void buildViaNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model);
  void buildPinNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model);
  void buildGeneratedSourceNodeList(PowerGraph& power_graph);
  void buildFullSourceNodeList(PowerGraph& power_graph);
  bool isOnWireSegment(PowerWireSegment& power_wire_segment, int32_t x, int32_t y);
  bool isOnWireSegment(PowerWireSegment& power_wire_segment, PowerNode& power_node);
  void buildWireEdgeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model);
  void buildViaEdgeList(PowerGraph& power_graph, PowerNet& power_net);
  void addPowerEdge(PowerGraph& power_graph, PowerEdgeType power_edge_type, std::size_t first_node_id, std::size_t second_node_id,
                    int32_t layer_idx, int32_t width, int32_t length, double resistance);
  void checkPowerGraphConnectivity(PowerGraph& power_graph);
};

}  // namespace iemir
