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
#include "RCXAPI.hh"

#include <omp.h>
#include <utility>

#include "CompareSpefTool.hh"
#include "DumpNetShapeTool.hh"
#include "Extraction.hh"
#include "RunRCXFromTopologyTool.hh"
#include "PlotSpefTool.hh"
#include "RCXConfig.hh"
#include "RCXData.hh"
#include "Report.hh"
#include "Setup.hh"
#include "StageLog.hh"
#include "config/CompareSpefConfig.hh"
#include "config/RunRCXFromTopologyConfig.hh"
#include "config/PlotSpefConfig.hh"
#include "log/Log.hh"

namespace ircx {

RCXAPI::RCXAPI()
{
  char config[] = "iRCX";
  char* argv[] = {config, nullptr};
  ieda::Log::init(argv);
}

auto RCXAPI::init(const std::string& config_file) -> bool
{
  return runStage("init_rcx", [&]() {
    RCX_DATA_INST.reset();
    return Setup::initialize(config_file);
  });
}

auto RCXAPI::run() -> bool
{
  return runStage("run_rcx", []() {
    if (!RCX_CONFIG_INST.is_initialized()) {
      LOG_ERROR << "run_rcx failed: RCX config is not initialized.";
      return false;
    }

    if (!Setup::adaptDB()) {
      return false;
    }

    omp_set_num_threads(RCX_CONFIG_INST.get_thread_count());

    return Extraction::run();
  });
}

auto RCXAPI::report() -> bool
{
  return runStage("report_spef", []() {
    return Report::dumpSpef();
  });
}

auto RCXAPI::compare_spef(compare_spef::Config config) -> bool
{
  return runStage("compare_spef", [&]() {
    return CompareSpefTool::run(std::move(config));
  }, {.profile = true});
}

auto RCXAPI::dump_net_shape() -> bool
{
  return runStage("dump_net_shape", []() {
    if (!Setup::adaptDB()) {
      return false;
    }

    return DumpNetShapeTool::run();
  }, {.profile = true});
}

auto RCXAPI::run_rcx_from_topology(run_rcx_from_topology::Config config) -> bool
{
  return runStage("run_rcx_from_topology", [&]() {
    if (!RCX_CONFIG_INST.is_initialized()) {
      LOG_ERROR << "run_rcx_from_topology failed: RCX config is not initialized.";
      return false;
    }

    if (!Setup::adaptDB()) {
      return false;
    }

    omp_set_num_threads(RCX_CONFIG_INST.get_thread_count());

    return RunRCXFromTopologyTool::run(std::move(config));
  }, {.profile = true});
}

auto RCXAPI::plot_spef(plot_spef::Config config) -> bool
{
  return runStage("plot_spef", [&]() {
    if (config.spef_file.empty()) {
      return PlotSpefTool::run(RCX_DATA_INST, std::move(config));
    }
    return PlotSpefTool::run(std::move(config));
  }, {.profile = true});
}

}  // namespace ircx
