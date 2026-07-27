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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file RunRCXFromTopologyTool.cc
 * @brief run_rcx_from_topology implementation detail.
 */
#include "RunRCXFromTopologyTool.hh"

#include <utility>

#include "Extraction.hh"
#include "LayoutData.hh"
#include "RCXData.hh"
#include "TopoPool.hh"
#include "TopologyBuilder.hh"
#include "config/RunRCXFromTopologyConfig.hh"
#include "log/Log.hh"
#include "topology/SpefTopologyBuilder.hh"

namespace ircx {

auto RunRCXFromTopologyTool::run(run_rcx_from_topology::Config config) -> bool
{
  const run_rcx_from_topology::ConfigValidator validator;
  if (!validator.validate(config)) {
    return false;
  }

  RCXData& data = RCX_DATA_INST;
  const LayoutData& layout = data.get_layout();
  if (layout.get_regular_net_count() == 0) {
    LOG_ERROR << "run_rcx_from_topology failed: layout data is empty, call adaptDB first.";
    return false;
  }

  TopoPool& topo_pool = data.get_topo_pool();
  topo_pool.clear();

  const run_rcx_from_topology::SpefTopologyBuilder spef_topology_builder(topo_pool);
  if (!spef_topology_builder.build(
          layout,
          data.get_layer_table(),
          config.spef_file,
          config.strict)) {
    return false;
  }

  TopologyBuilder(topo_pool).buildSpecial(layout);
  return Extraction::runFromTopology();
}

}  // namespace ircx
