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

#include "RCXAPI.hh"
#include "ircx_ics55.h"
#include "idm.h"

namespace python_interface {

namespace {

enum class RcxBackend
{
  kUninitialized,
  kNative,
  kIcs55,
};

RcxBackend active_backend = RcxBackend::kUninitialized;

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

bool init_rcx(const std::string& config, const std::optional<std::string>& pdk)
{
  active_backend = RcxBackend::kUninitialized;

  if (!validate_pdk(pdk)) {
    return false;
  }

  if (is_ics55_pdk(pdk)) {
    if (ircx_ics55_init(config.c_str()) != 0) {
      active_backend = RcxBackend::kIcs55;
      return true;
    }

    return false;
  }

  if (RCX_API_INST.init(config)) {
    active_backend = RcxBackend::kNative;
    return true;
  }

  return false;
}

bool run_rcx()
{
  if (active_backend == RcxBackend::kIcs55) {
    return ircx_ics55_run_with_idb_design(dmInst->get_idb_design()) != 0;
  }

  if (active_backend != RcxBackend::kNative) {
    return false;
  }

  return RCX_API_INST.run();
}

bool report_rcx()
{
  if (active_backend == RcxBackend::kIcs55) {
    return ircx_ics55_report() != 0;
  }

  if (active_backend != RcxBackend::kNative) {
    return false;
  }

  return RCX_API_INST.report();
}

}  // namespace python_interface
