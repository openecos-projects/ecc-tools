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

#include "RCXBackendType.hpp"
#include "RCXInterface.hpp"
#include "ircx_ics55.h"
#include "idm.h"

namespace python_interface {

namespace {

RCXBackendType active_backend = RCXBackendType::kNone;

bool is_native_pdk(const std::optional<std::string>& pdk)
{
  return !pdk.has_value() || pdk->empty();
}

bool is_ics55_pdk(const std::optional<std::string>& pdk)
{
  return pdk.has_value() && *pdk == "ics55";
}

bool validate_pdk(const std::optional<std::string>& pdk)
{
  return is_native_pdk(pdk) || is_ics55_pdk(pdk);
}

}  // namespace

bool destroy_rcx()
{
  if (active_backend == RCXBackendType::kNative) {
    RCXI.destroyRCX();
  } else if (active_backend == RCXBackendType::kIcs55) {
    ircx_ics55_destroy();
  }
  active_backend = RCXBackendType::kNone;
  return true;
}

bool init_rcx(const std::string& config, const std::optional<std::string>& pdk)
{
  active_backend = RCXBackendType::kNone;

  if (!validate_pdk(pdk)) {
    return false;
  }

  if (is_ics55_pdk(pdk)) {
    ircx_ics55_init(config.c_str(), dmInst->get_idb_design());
    active_backend = RCXBackendType::kIcs55;
    return true;
  }

  std::map<std::string, std::any> config_map;
  config_map["-config"] = config;
  RCXI.initRCX(config_map);
  active_backend = RCXBackendType::kNative;
  return true;
}

bool run_rcx()
{
  if (active_backend == RCXBackendType::kIcs55) {
    ircx_ics55_run();
    return true;
  }

  if (active_backend != RCXBackendType::kNative) {
    return false;
  }

  RCXI.runRCX();
  return true;
}

}  // namespace python_interface
