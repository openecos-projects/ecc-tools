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

#include <exception>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ScriptEngine.hh"
#include "query/SdcObject.hpp"

namespace ista::sdc {

struct ObjectQueryOptions;

class SdcTclCmd : public ecc::TclCmd
{
 public:
  SdcTclCmd(const char* cmd_name, ClientData client_data);
  ~SdcTclCmd() override = default;

  int execute(Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);
  Tcl_Interp* interpreter() const { return _interp; }
  Tcl_Obj* getOptionObject(const std::string& name) const
  {
    const auto found = _option_object_map.find(name);
    return found == _option_object_map.end() ? nullptr : found->second;
  }
  std::vector<Tcl_Obj*> getOptionObjects(const std::string& name);

 protected:
  double timeValue(const std::string& option);
  double capValue(const std::string& option);
  std::vector<double> timeValues(const std::string& option);
  void setOptionValue(ecc::TclOption* option, const char* value);
  void setTclError(std::string error_message) { _error_message = std::move(error_message); }
  void warn(const std::string& message) const;
  void setResult(std::string result);
  void setResult(std::vector<std::string> result, SdcObjectType type = SdcObjectType::kNone);
  static std::vector<std::string> getObjectCollectionNames(Tcl_Obj* object);
  std::vector<std::string> getOptionObjectNames(const std::string& name);
  const std::vector<std::pair<std::string, Tcl_Obj*>>& getOptionObjectValueList() const { return _option_object_value_list; }
  const std::vector<std::pair<std::string, std::string>>& getOptionValueList() const { return _option_value_list; }

  ClientData getClientData() const { return _client_data; }

 private:
  friend ObjectQueryOptions getObjectQueryOptions(SdcTclCmd& command);
  void resetExecutionState();
  void setInterpreterError(Tcl_Interp* interp) const;

  Tcl_Interp* _interp = nullptr;
  ClientData _client_data = nullptr;
  std::string _error_message;
  std::string _result;
  std::vector<std::string> _list_result;
  SdcObjectType _list_result_type = SdcObjectType::kNone;
  std::map<std::string, Tcl_Obj*> _option_object_map;
  std::vector<std::pair<std::string, Tcl_Obj*>> _option_object_value_list;
  std::vector<std::pair<std::string, std::string>> _option_value_list;
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

  try {
    Command command(Tcl_GetString(objv[0]), client_data);
    return command.execute(interp, objc, objv);
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
  }
}

}  // namespace ista::sdc
