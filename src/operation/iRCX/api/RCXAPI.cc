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

#include "CompareParasiticsFlow.hh"
#include "Extraction.hh"
#include "RCXConfig.hh"
#include "RCXData.hh"
#include "Report.hh"
#include "Setup.hh"
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
  RCX_DATA_INST.reset();
  return Setup::initialize(config_file);
}

auto RCXAPI::run() -> bool
{
  LOG_INFO << "RCX run begin...";

  if (!RCX_CONFIG_INST.get_initialized()) {
    LOG_ERROR << "RCX flow failed: RCX config is not initialized.";
    LOG_INFO << "RCX flow end.";
    return false;
  }

  if (!Setup::adaptDB()) {
    LOG_INFO << "RCX run end.";
    return false;
  }

  omp_set_num_threads(RCX_CONFIG_INST.get_thread_num());

  if (!Extraction::run()) {
    LOG_INFO << "RCX flow end.";
    return false;
  }

  LOG_INFO << "RCX flow end.";
  return true;
}

auto RCXAPI::report() -> bool
{
  return Report::dumpSpef();
}

auto RCXAPI::compareParasitics(CompareParasiticsConfig config) -> bool
{
  return CompareParasiticsFlow::run(std::move(config));
}

}  // namespace ircx
