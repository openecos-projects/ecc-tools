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
#include "SdcTclCmd.hpp"

namespace ista::sdc {

SdcTclCmd::SdcTclCmd(const char* cmd_name, ClientData client_data) : ecc::TclCmd(cmd_name), _client_data(client_data)
{
}

int SdcTclCmd::execute(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  resetExecutionState();

  bool next_is_option_value = false;
  ecc::TclOption* current_option = nullptr;
  int argument_index = 0;
  for (int index = 1; index < objc; ++index) {
    const char* value = Tcl_GetString(objv[index]);
    if (next_is_option_value) {
      current_option->setVal(value);
      next_is_option_value = false;
      continue;
    }

    ecc::TclOption* option = getOptionOrArg(value);
    if (option != nullptr) {
      current_option = option;
      if (option->isSwitchOption()) {
        option->setVal(nullptr);
      } else {
        next_is_option_value = true;
      }
      continue;
    }

    ecc::TclOption* argument = getArg(argument_index++);
    if (argument == nullptr) {
      setTclError(std::string("unexpected argument '") + value + "'");
      setInterpreterError(interp);
      return TCL_ERROR;
    }
    argument->setVal(value);
  }

  if (next_is_option_value) {
    setTclError(std::string("option '") + current_option->get_option_name() + "' requires a value");
    setInterpreterError(interp);
    return TCL_ERROR;
  }
  if (!check()) {
    setTclError(std::string("invalid ") + get_cmd_name() + " command");
    setInterpreterError(interp);
    return TCL_ERROR;
  }
  if (!exec()) {
    if (_error_message.empty()) {
      setTclError(std::string(get_cmd_name()) + " failed");
    }
    setInterpreterError(interp);
    return TCL_ERROR;
  }
  if (!_error_message.empty()) {
    setInterpreterError(interp);
    return TCL_ERROR;
  }

  if (_has_list_result) {
    Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
    for (const std::string& value : _list_result) {
      Tcl_ListObjAppendElement(interp, list, Tcl_NewStringObj(value.c_str(), static_cast<int>(value.size())));
    }
    Tcl_SetObjResult(interp, list);
  } else if (_has_result) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(_result.c_str(), static_cast<int>(_result.size())));
  }
  return TCL_OK;
}

void SdcTclCmd::setResult(std::string result)
{
  _result = std::move(result);
  _has_result = true;
  _has_list_result = false;
}

void SdcTclCmd::setResult(std::vector<std::string> result)
{
  _list_result = std::move(result);
  _has_list_result = true;
  _has_result = false;
}

void SdcTclCmd::resetExecutionState()
{
  resetOptionArgValue();
  _error_message.clear();
  _result.clear();
  _list_result.clear();
  _has_result = false;
  _has_list_result = false;
}

void SdcTclCmd::setInterpreterError(Tcl_Interp* interp) const
{
  Tcl_SetObjResult(interp, Tcl_NewStringObj(_error_message.c_str(), static_cast<int>(_error_message.size())));
}

}  // namespace ista::sdc
