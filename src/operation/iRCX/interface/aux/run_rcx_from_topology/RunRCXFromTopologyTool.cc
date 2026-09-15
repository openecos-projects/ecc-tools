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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "RunRCXFromTopologyTool.hh"

#include "DataManager.hpp"
#include "LayoutData.hpp"
#include "Logger.hpp"
#include "Patch.hpp"
#include "RCXHeader.hpp"
#include "Segment.hpp"
#include "TopoEdge.hpp"
#include "TopoPool.hpp"
#include "config/RunRCXFromTopologyConfig.hh"
#include "topology/SpefTopologyBuilder.hh"

namespace ircx::run_rcx_from_topology {

void buildSpecialEdgeList(LayoutData& layout_data, TopoPool& topo_pool)
{
  Net& special_net = layout_data.get_special_net();
  std::vector<TopoEdge> special_edge_list;
  special_edge_list.reserve(special_net.get_segment_list().size() + special_net.get_patch_list().size());
  for (Segment& segment : special_net.get_segment_list()) {
    TopoEdge edge;
    edge.set_layer_idx(segment.get_layer_idx());
    edge.set_shape(segment.get_shape());
    special_edge_list.push_back(std::move(edge));
  }
  for (Patch& patch : special_net.get_patch_list()) {
    TopoEdge edge;
    edge.set_layer_idx(patch.get_layer_idx());
    edge.set_shape(patch.get_shape());
    special_edge_list.push_back(std::move(edge));
  }
  topo_pool.add_special_edge_list(std::move(special_edge_list));
}

}  // namespace ircx::run_rcx_from_topology

namespace ircx {

bool RunRCXFromTopologyTool::run(run_rcx_from_topology::Config config)
{
  run_rcx_from_topology::ConfigValidator validator;
  if (!validator.validate(config)) {
    return false;
  }

  Database& database = RCXDM.getDatabase();
  LayoutData& layout_data = database.get_layout_data();
  if (layout_data.get_regular_net_num() == 0) {
    RCXLOG.warn(Loc::current(), "run_rcx_from_topology failed: layout data is empty, call init_rcx first.");
    return false;
  }

  TopoPool topo_pool;
  run_rcx_from_topology::SpefTopologyBuilder topology_builder(topo_pool);
  if (!topology_builder.build(layout_data, database.get_layer_table(), config.spef_file, config.strict)) {
    return false;
  }
  run_rcx_from_topology::buildSpecialEdgeList(layout_data, topo_pool);
  database.set_topo_pool(topo_pool);
  return true;
}

}  // namespace ircx
