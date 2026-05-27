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

auto RCXAPI::init(const std::string& config_file) -> bool
{
  RCX_FLOW_INST.reset();
  return Setup::initialize(config_file);
}

auto RCXAPI::run() -> bool
{
  return RCX_FLOW_INST.run();
}

auto RCXAPI::report() -> bool
{
  return RCX_FLOW_INST.report();
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
  return RCX_FLOW_INST.adaptDB();
}

auto RCXAPI::extract() -> bool
{
  return RCX_FLOW_INST.extract();
}

}  // namespace ircx
