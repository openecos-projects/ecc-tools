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
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Net.hpp"
#include "RCXHeader.hpp"
#include "Segment.hpp"
#include "TBModel.hpp"
#include "TBNodeKey.hpp"
#include "TBTopo.hpp"
#include "TopoNode.hpp"

namespace ircx {

#define RCXTB (ircx::TopoBuilder::getInst())

class TopoBuilder
{
 public:
  static void initInst();
  static TopoBuilder& getInst();
  static void destroyInst();
  // function
  void build();

 private:
  // self
  static TopoBuilder* _tb_instance;

  TopoBuilder() = default;
  TopoBuilder(const TopoBuilder& other) = delete;
  TopoBuilder(TopoBuilder&& other) = delete;
  ~TopoBuilder() = default;
  TopoBuilder& operator=(const TopoBuilder& other) = delete;
  TopoBuilder& operator=(TopoBuilder&& other) = delete;
  // function
  void buildRegularNetTopoList();
  TBTopo buildNetTopo(Net& net);
  void appendNodeIfAbsent(Net& net, std::vector<TopoNode>& node_list, std::map<TBNodeKey, int32_t>& node_key_to_idx_map,
                          std::set<std::string>& consumed_pin_name_set, int32_t layer_idx, const GTLPointInt& point);
  int32_t appendNode(std::vector<TopoNode>& node_list, TopoNode node);
  void mergeNodeShape(std::vector<TopoNode>& node_list, int32_t node_idx, const GTLRectInt& shape);
  GTLRectInt getSegmentEndpointShape(Segment& segment, const GTLPointInt& point);
  void buildSpecialEdgeList();
};

}  // namespace ircx
