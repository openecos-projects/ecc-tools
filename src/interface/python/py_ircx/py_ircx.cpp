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
#include "py_ircx.h"

#include <filesystem>

#include "RCXInterface.hpp"

namespace python_interface {

namespace {

bool rcx_initialized = false;

bool is_native_pdk(const std::optional<std::string>& pdk)
{
  return !pdk.has_value() || pdk->empty();
}

}  // namespace

bool destroy_rcx()
{
  if (rcx_initialized) {
    RCXI.destroyRCX();
  }
  rcx_initialized = false;
  return true;
}

bool init_rcx(const std::filesystem::path& config, const std::optional<std::string>& pdk)
{
  const std::string config_ = config.string();
  rcx_initialized = false;

  if (!is_native_pdk(pdk)) {
    return false;
  }

  std::map<std::string, std::any> config_map;
  config_map["-config"] = config_;
  RCXI.initRCX(config_map);
  rcx_initialized = true;
  return true;
}

bool run_rcx()
{
  if (!rcx_initialized) {
    return false;
  }

  RCXI.runRCX();
  return true;
}

}  // namespace python_interface
