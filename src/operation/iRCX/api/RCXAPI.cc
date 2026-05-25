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

#include "Flow.hh"
#include "Setup.hh"

namespace ircx {

auto RCXAPI::init(const RCXInitOptions& options) -> void
{
  resetAPI();
  RCX_FLOW_INST.set_num_threads(options.thread_number);
  RCX_FLOW_INST.set_operating_temperature(options.operating_temperature);
  RCX_FLOW_INST.setSetupReady(true);
}

auto RCXAPI::init(const std::string& config_file) -> void
{
  resetAPI();
  const bool setup_ready = Setup::initialize(config_file);
  RCX_FLOW_INST.setSetupReady(setup_ready);
}

auto RCXAPI::resetAPI() -> void
{
  RCX_FLOW_INST.reset();
}

auto RCXAPI::readCorner(const std::string& corner_name,
                        const char* itf_file,
                        const char* captab_file) -> bool
{
  return Setup::readCorner(corner_name, itf_file, captab_file);
}

auto RCXAPI::readMapping(const char* mapping_file) -> bool
{
  return Setup::readMapping(mapping_file);
}

auto RCXAPI::adaptDB() -> bool
{
  return RCX_FLOW_INST.readData();
}

auto RCXAPI::checkShortOpen() -> bool
{
  return RCX_FLOW_INST.checkShortOpen();
}

auto RCXAPI::buildTopology() -> bool
{
  return RCX_FLOW_INST.buildTopology();
}

auto RCXAPI::buildEnvironment() -> bool
{
  return RCX_FLOW_INST.buildEnvironment();
}

auto RCXAPI::buildProcessVariation() -> bool
{
  return RCX_FLOW_INST.buildProcessVariation();
}

auto RCXAPI::extractParasitics() -> bool
{
  return RCX_FLOW_INST.extractParasitics();
}

auto RCXAPI::run() -> void
{
  RCX_FLOW_INST.run();
}

auto RCXAPI::runRCX() -> void
{
  RCX_FLOW_INST.runRCX();
}

auto RCXAPI::report(const std::string& output_dir) -> void
{
  RCX_FLOW_INST.report(output_dir.empty() ? "." : output_dir);
}

auto RCXAPI::runSuccess() -> bool
{
  return RCX_FLOW_INST.run_success();
}

auto RCXAPI::reportSuccess() -> bool
{
  return RCX_FLOW_INST.report_success();
}

}  // namespace ircx
