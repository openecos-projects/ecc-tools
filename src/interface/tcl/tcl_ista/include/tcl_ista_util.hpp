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
#pragma once

#include "tcl_util.h"

namespace {

void setResult(const std::string& result)
{
  Tcl_Interp* _interp{tcl::ScriptEngine::getOrCreateInstance()->get_interp()};
  auto* buffer = new char[result.size() + 1];
  std::strcpy(buffer, result.c_str());
  Tcl_SetResult(_interp, buffer, [](auto* buffer) {
    delete[] buffer;
  });
}

void setResult(const std::vector<std::string>& result)
{
  Tcl_Obj* list_obj{Tcl_NewListObj(0, nullptr)};
  Tcl_Interp* _interp{tcl::ScriptEngine::getOrCreateInstance()->get_interp()};
  for (const std::string& value : result) {
    Tcl_ListObjAppendElement(_interp, list_obj, Tcl_NewStringObj(value.c_str(), static_cast<int>(value.size())));
  }
  Tcl_SetObjResult(_interp, list_obj);
}

void setTclError(const std::string& message)
{
  setResult(message);
}

}  // namespace
