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

#include "DataManager.hpp"
#include "Direction.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Net.hpp"
#include "RCXHeader.hpp"
#include "SPEFNameMap.hpp"
#include "SWNodeCouplingKey.hpp"
#include "SWModel.hpp"
#include "TopoEdge.hpp"
#include "TopoNode.hpp"

namespace ircx {

#define RCXSW (ircx::SPEFWriter::getInst())

class SPEFWriter
{
 public:
  static void initInst();
  static SPEFWriter& getInst();
  static void destroyInst();
  // function
  void write();

 private:
  // self
  static SPEFWriter* _sw_instance;

  SPEFWriter() = default;
  SPEFWriter(const SPEFWriter& other) = delete;
  SPEFWriter(SPEFWriter&& other) = delete;
  ~SPEFWriter() = default;
  SPEFWriter& operator=(const SPEFWriter& other) = delete;
  SPEFWriter& operator=(SPEFWriter&& other) = delete;
  // function
  void writeSWModel(SWModel& sw_model);
  void buildNameMap(SPEFNameMap& spef_name_map);
  void writeSPEFList(SWModel& sw_model, SPEFNameMap& spef_name_map);
  void writeSPEF(SWModel& sw_model, SPEFNameMap& spef_name_map, int32_t corner_idx);
  void buildNetCouplingRefList(SWModel& sw_model, int32_t corner_idx);
  void buildReportLayerList(SWModel& sw_model);
  void writeHeader(std::ofstream& spef_file_stream, int32_t corner_idx);
  void writeNameMap(std::ofstream& spef_file_stream, SPEFNameMap& spef_name_map);
  void writePortList(std::ofstream& spef_file_stream, SPEFNameMap& spef_name_map);
  char getSPEFDirection(Direction direction);
  void writeLayerMap(SWModel& sw_model, std::ofstream& spef_file_stream);
  void writeDNetList(SWModel& sw_model, std::ofstream& spef_file_stream, SPEFNameMap& spef_name_map, int32_t corner_idx);
  void writeDNet(SWModel& sw_model, std::ofstream& spef_file_stream, SPEFNameMap& spef_name_map, int32_t corner_idx,
                 int32_t net_idx);
  void getNearestNodePair(TopoEdge& self_edge, TopoEdge& other_edge, int32_t& self_node_idx, int32_t& other_node_idx);
  std::string getNodeSPEFName(SPEFNameMap& spef_name_map, TopoNode& node);
  Direction getPinDirection(Net& net, const std::string& pin_name);
  void writeNodeGeometry(SWModel& sw_model, std::ofstream& spef_file_stream, TopoNode& node, double micron_per_dbu);
  void writeResGeometry(SWModel& sw_model, std::ofstream& spef_file_stream, int32_t corner_idx, TopoEdge& edge,
                        double micron_per_dbu);
  int32_t getReportLayerLevel(SWModel& sw_model, int32_t design_layer_idx);
};

}  // namespace ircx
