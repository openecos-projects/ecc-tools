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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Database.hpp"
#include "PinDirectionResolver.hpp"

namespace ista {
class GraphConnectivityBuilder
{
 public:
  explicit GraphConnectivityBuilder(Database& database) : _database(database), _pin_direction_resolver(database) {}
  // Build connectivity in a fresh graph, including pin roles and vertex bindings.
  // Logic evaluation, endpoint classification and ordering are separate phases.
  void build();

 private:
  void buildTimingPointList();
  void buildCellArcs();
  void rebuildCellArcListByPinDirection();
  void buildNetDriverLoadList();
  void buildNetArcs();
  void buildGraphVertexList();

  bool buildLibraryCellArcs(Instance& instance);
  std::string getInstancePinName(Instance& instance, std::string& port_name);
  void addCellArc(Instance& instance, TimingCellArc& timing_cell_arc, std::size_t liberty_arc_index);
  void addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name, const std::string& library_source_port,
              const std::string& library_sink_port, bool is_clock_arc, bool is_disable_arc, LibertyArcId liberty_arc_id);
  std::vector<std::string> collectInputPins(Instance& instance);
  bool isInputLike(PinDirection direction);
  std::vector<std::string> collectOutputPins(Instance& instance);
  bool isOutputLike(PinDirection direction);
  void makeNetDriverLoad(Net& net);
  bool isDriverPin(Pin& pin);
  int32_t addGraphVertex(const std::string& pin_name, bool is_driver, TimingPoint& point);
  void addArc(const std::string& source_pin, const std::string& sink_pin, ArcType type, const std::string& owner_name);
  bool shouldDisableNetArc(const std::string& source_pin, const std::string& sink_pin);
  bool isDisableArc(Arc& arc);
  Database& _database;
  PinDirectionResolver _pin_direction_resolver;
};
}  // namespace ista
