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

#include <string>
#include <vector>

#include "ScriptEngine.hh"

namespace ista::sdc {

class SdcTclCmd : public ecc::TclCmd
{
 public:
  SdcTclCmd(const char* cmd_name, ClientData client_data);
  ~SdcTclCmd() override = default;

  int execute(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

 protected:
  void setTclError(std::string error_message) { _error_message = std::move(error_message); }
  void setResult(std::string result);
  void setResult(std::vector<std::string> result);

  ClientData getClientData() const { return _client_data; }

 private:
  void resetExecutionState();
  void setInterpreterError(Tcl_Interp* interp) const;

  ClientData _client_data = nullptr;
  std::string _error_message;
  std::string _result;
  std::vector<std::string> _list_result;
  bool _has_result = false;
  bool _has_list_result = false;
};

template <typename Command>
int executeTclCommand(ClientData client_data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  if (objc == 0) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("empty Tcl command", -1));
    return TCL_ERROR;
  }

  Command command(Tcl_GetString(objv[0]), client_data);
  return command.execute(interp, objc, objv);
}

}  // namespace ista::sdc
