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
#include <algorithm>
#include <filesystem>
#include <omp.h>
#include <system_error>

#include "Flow.hh"
#include "CapacitanceCalc.hh"
#include "Environment.hh"
#include "ProcessCorner.hpp"
#include "ProcessVariation.hh"
#include "RCXData.hh"
#include "ResistanceCalc.hh"
#include "setup/Setup.hh"
#include "SpefDumper.hh"
#include "TopologyBuilder.hh"
#include "log/Log.hh"
namespace ircx {

void Flow::reset()
{
  RCX_DATA_INST.reset();
  environment_.reset();
  process_variation_.reset();
  num_threads_ = 0;
  operating_temperature_ = 0.0;
  output_dir_ = ".";
  setup_ready_ = false;
  run_success_ = false;
  report_success_ = false;
}

auto Flow::runRCX() -> void
{
  run_success_ = false;
  report_success_ = false;

  LOG_INFO << "RCX flow begin...";

  if (!setup_ready_) {
    LOG_ERROR << "RCX flow failed: setup is not ready.";
    LOG_INFO << "RCX flow end.";
    return;
  }

  if (!readData()) {
    LOG_INFO << "RCX flow end.";
    return;
  }

  run();
}

auto Flow::readData() -> bool
{
  return Setup::readData();
}

auto Flow::checkShortOpen() -> bool
{
  // if (!db_) {
  //   if (!adaptDB()) {
  //     LOG_WARNING << "check short/open: database is empty, call adaptDB() first.";
  //     return 0;
  //   }
  // }

  // LOG_INFO << "check short/open start";


  // LOG_INFO << "check short/open end";
  return true;
}

auto Flow::buildTopology() -> bool
{
  LOG_INFO << "build topology start";

  RCXData& data = RCX_DATA_INST;
  TopoPool& topo_pool = data.topo_pool();
  const LayoutData& layout = data.layout();
  if (layout.regular_net_count() == 0) {
    LOG_ERROR << "build topology failed: layout data is empty, call adaptDB first.";
    return false;
  }

  topo_pool.clear();
  TopologyBuilder tb(topo_pool);
  tb.build_all(layout);
  tb.build_special(layout);

  LOG_INFO << "build topology end";
  return true;
}

auto Flow::buildEnvironment() -> bool
{
  LOG_INFO << "build environment start";

  const RCXData& data = RCX_DATA_INST;
  if (data.topo_pool().edge_pool().empty()) {
    LOG_ERROR << "build environment failed: topology is empty, call buildTopology first.";
    return false;
  }

  Environment& env = environment_;
  env.set_layout_data(&data.layout());
  env.set_topo_pool(&data.topo_pool());

  if (!env.buildNetEnvPools()) {
    return false;
  }

  LOG_INFO << "build environment end";
  return true;
}

auto Flow::buildProcessVariation() -> bool
{
  LOG_INFO << "build process variation start";

  RCXData& data = RCX_DATA_INST;
  const auto process_corners = data.corners();
  if (process_corners.empty()) {
    LOG_ERROR << "build process variation failed: process corners not loaded.";
    return false;
  }
  if (environment_.net_env_pools().empty()) {
    LOG_ERROR << "build process variation failed: environment is empty, call buildEnvironment first.";
    return false;
  }

  ProcessVariation& pv = process_variation_;
  pv.set_layout_data(&data.layout());
  pv.set_net_env_pools(&environment_.net_env_pools());
  pv.set_topo_pool(&data.topo_pool());
  pv.set_layer_table(&data.layer_table());
  pv.set_corners(process_corners);
  
  if (!pv.buildEtchPools()) {
    return false;
  }

  LOG_INFO << "build process variation end";
  return true;
}

auto Flow::extractParasitics() -> bool
{
  LOG_INFO << "extract parasitics start";

  RCXData& data = RCX_DATA_INST;
  const auto process_corners = data.corners();
  const auto cap_tables = data.corner_cap_tables();
  if (process_corners.empty()) {
    LOG_ERROR << "extract parasitics failed: process corners not loaded.";
    return false;
  }
  if (process_corners.size() != cap_tables.size()) {
    LOG_ERROR << "extract parasitics failed: corner/captab size mismatch.";
    return false;
  }
  if (process_variation_.corner_net_etch_pools().empty()) {
    LOG_ERROR << "extract parasitics failed: process variation data is empty.";
    return false;
  }
  for (Size corner_idx = 0; corner_idx < process_corners.size(); ++corner_idx) {
    if (process_corners[corner_idx] == nullptr) {
      LOG_ERROR << "extract parasitics failed: null process corner.";
      return false;
    }
    if (cap_tables[corner_idx] == nullptr) {
      LOG_ERROR << "captab not loaded for corner "
                << process_corners[corner_idx]->get_technology();
      return false;
    }
  }

  // Pre-allocate RCTable for parallel access
  RCTable& rc_table = data.rc_table();
  rc_table.init(process_corners.size(), data.layout().regular_net_count(), data.topo_pool());

  // Resistance 
  ResistanceCalc res_calc;
  res_calc.set_layout_data(&data.layout());
  res_calc.set_topo_pool(&data.topo_pool());
  res_calc.set_layer_table(&data.layer_table());
  res_calc.set_corner_net_etch_pools(&process_variation_.corner_net_etch_pools());
  res_calc.set_rc_table(&rc_table);
  res_calc.set_corners(process_corners);
  res_calc.set_operating_temperature(operating_temperature_);
  if (!res_calc.calc()) {
    return false;
  }

  // Capacitance
  CapacitanceCalc cap_calc;
  cap_calc.set_layout_data(&data.layout());
  cap_calc.set_net_env_pools(&environment_.net_env_pools());
  cap_calc.set_corner_net_etch_pools(&process_variation_.corner_net_etch_pools());
  cap_calc.set_topo_pool(&data.topo_pool());
  cap_calc.set_layer_table(&data.layer_table());
  cap_calc.set_rc_table(&rc_table);
  cap_calc.set_cap_tables(cap_tables);
  cap_calc.set_corners(process_corners);
  if (!cap_calc.calc()) {
    return false;
  }

  LOG_INFO << "extract parasitics end";
  return true;
}

auto Flow::run() -> void
{
  run_success_ = false;

  omp_set_num_threads(num_threads_);

  if (!buildTopology() ||
      !buildEnvironment() ||
      !buildProcessVariation() ||
      !extractParasitics()) {
    LOG_INFO << "RCX flow end.";
    return;
  }

  run_success_ = true;
  LOG_INFO << "RCX flow end.";
}

auto Flow::report(const Str& output_dir) -> void
{
  report_success_ = dumpSpef(output_dir);
}

auto Flow::dumpSpef(const Str& output_dir) -> bool
{
  LOG_INFO << "report spef start";
  const Str resolved_output_dir = output_dir.empty() ? "." : output_dir;
  std::error_code ec;
  std::filesystem::create_directories(resolved_output_dir, ec);
  if (ec) {
    LOG_ERROR << "Failed to create RCX output directory "
              << resolved_output_dir << ": " << ec.message();
    return false;
  }

  RCXData& data = RCX_DATA_INST;
  const auto process_corners = data.corners();
  if (process_corners.empty()) {
    LOG_ERROR << "report spef failed: process corners not loaded.";
    return false;
  }

  SpefDumper dumper;

  dumper.set_spef_context(&data.spef_context());
  dumper.set_layout_data(&data.layout());
  dumper.set_layer_table(&data.layer_table());
  dumper.set_topo_pool(&data.topo_pool());
  dumper.set_rc_table(&data.rc_table());
  dumper.set_corners(process_corners);

  for (Size corner_idx = 0; corner_idx < process_corners.size(); ++corner_idx) {
    dumper.dump(resolved_output_dir, corner_idx);
  }

  LOG_INFO << "report spef end";
  return true;
}

Flow::Flow()
{
  char config[] = "iRCX";
  char* argv[] = {config, nullptr};
  ieda::Log::init(argv);
}
Flow::~Flow() = default;

}  // namespace ircx
