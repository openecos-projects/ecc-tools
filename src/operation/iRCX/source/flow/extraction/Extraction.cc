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
/**
 * @file Extraction.cc
 * @brief iRCX extraction flow implementation.
 */
#include "Extraction.hh"

#include "CapacitanceCalc.hh"
#include "Environment.hh"
#include "ProcessCorner.hpp"
#include "ProcessVariation.hh"
#include "RCXData.hh"
#include "ResistanceCalc.hh"
#include "StageLog.hh"
#include "TopologyBuilder.hh"
#include "log/Log.hh"

namespace ircx {

auto Extraction::run() -> bool
{
  return buildTopology() && runFromTopology();
}

auto Extraction::runFromTopology() -> bool
{
  return buildEnvironment()
      && buildProcessVariation()
      && calculateParasitics();
}

auto Extraction::buildTopology() -> bool
{
  return runStage("build topology", []() -> bool {
    RCXData& data = RCX_DATA_INST;
    TopoPool& topo_pool = data.get_topo_pool();
    const LayoutData& layout = data.get_layout();
    if (layout.get_regular_net_count() == 0) {
      LOG_ERROR << "build topology failed: layout data is empty, call adaptDB first.";
      return false;
    }

    topo_pool.clear();
    TopologyBuilder tb(topo_pool);
    tb.buildAll(layout);
    tb.buildSpecial(layout);

    return true;
  }, {.profile = true});
}

auto Extraction::buildEnvironment() -> bool
{
  return runStage("build environment", []() -> bool {
    RCXData& data = RCX_DATA_INST;
    if (data.get_topo_pool().get_edge_pool().empty()) {
      LOG_ERROR << "build environment failed: topology is empty, call buildTopology first.";
      return false;
    }

    Environment env;
    env.set_layout_data(&data.get_layout());
    env.set_topo_pool(&data.get_topo_pool());

    if (!env.buildNetEnvironments(data.get_net_env_pools())) {
      return false;
    }

    return true;
  }, {.profile = true});
}

auto Extraction::buildProcessVariation() -> bool
{
  return runStage("build process variation", []() -> bool {
    RCXData& data = RCX_DATA_INST;
    const auto& corner_data = data.get_corner_data();
    if (corner_data.empty()) {
      LOG_ERROR << "build process variation failed: process corners not loaded.";
      return false;
    }
    if (data.get_net_env_pools().empty()) {
      LOG_ERROR << "build process variation failed: environment is empty, "
                   "call buildEnvironment first.";
      return false;
    }

    ProcessVariation pv;
    pv.set_layout_data(&data.get_layout());
    pv.set_net_environments(&data.get_net_env_pools());
    pv.set_corner_net_etch_pools(&data.get_corner_net_etch_pools());
    pv.set_topo_pool(&data.get_topo_pool());
    pv.set_layer_table(&data.get_layer_table());
    pv.set_corner_data(&corner_data);

    if (!pv.buildEtchProfiles()) {
      return false;
    }

    return true;
  }, {.profile = true});
}

auto Extraction::calculateParasitics() -> bool
{
  return runStage("calculate parasitics", []() -> bool {
    RCXData& data = RCX_DATA_INST;
    const auto& corner_data = data.get_corner_data();
    if (corner_data.empty()) {
      LOG_ERROR << "calculate parasitics failed: process corners not loaded.";
      return false;
    }
    if (data.get_corner_net_etch_pools().empty()) {
      LOG_ERROR << "calculate parasitics failed: process variation data is empty.";
      return false;
    }
    for (const auto& corner : corner_data) {
      if (!corner.process_corner) {
        LOG_ERROR << "calculate parasitics failed: null process corner "
                  << corner.name << ".";
        return false;
      }
    }

    RCTable& rc_table = data.get_rc_table();
    rc_table.init(
        corner_data.size(),
        data.get_layout().get_regular_net_count(),
        data.get_topo_pool());

    ResistanceCalc res_calc;
    res_calc.set_layout_data(&data.get_layout());
    res_calc.set_topo_pool(&data.get_topo_pool());
    res_calc.set_layer_table(&data.get_layer_table());
    res_calc.set_corner_net_etch_pools(&data.get_corner_net_etch_pools());
    res_calc.set_rc_table(&rc_table);
    res_calc.set_corner_data(&corner_data);
    if (!res_calc.calc()) {
      return false;
    }

    CapacitanceCalc cap_calc;
    cap_calc.set_layout_data(&data.get_layout());
    cap_calc.set_net_environments(&data.get_net_env_pools());
    cap_calc.set_corner_net_etch_pools(&data.get_corner_net_etch_pools());
    cap_calc.set_topo_pool(&data.get_topo_pool());
    cap_calc.set_layer_table(&data.get_layer_table());
    cap_calc.set_rc_table(&rc_table);
    cap_calc.set_corner_data(&corner_data);
    if (!cap_calc.calc()) {
      return false;
    }

    return true;
  }, {.profile = true});
}

}  // namespace ircx
