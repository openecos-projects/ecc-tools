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

#include "Logger.hpp"

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
      _option_object_map[current_option->get_option_name()] = objv[index];
      _option_object_value_list.emplace_back(current_option->get_option_name(), objv[index]);
      setOptionValue(current_option, value);
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
    _option_object_map[argument->get_option_name()] = objv[index];
    _option_object_value_list.emplace_back(argument->get_option_name(), objv[index]);
    setOptionValue(argument, value);
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
      Tcl_ListObjAppendElement(interp, list, SdcObject::create(value, _list_result_type));
    }
    Tcl_SetObjResult(interp, list);
  } else if (_has_result) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(_result.c_str(), static_cast<int>(_result.size())));
  }
  return TCL_OK;
}

void SdcTclCmd::setOptionValue(ecc::TclOption* option, const char* value)
{
  _option_value_list.emplace_back(option->get_option_name(), value);
  if (option->isDoubleOption()) {
    double number = 0.0;
    if (Tcl_GetDouble(nullptr, value, &number) != TCL_OK || !std::isfinite(number)) {
      throw std::invalid_argument(std::string(option->get_option_name()) + " requires a finite number: " + value);
    }
    std::ostringstream number_stream;
    number_stream << std::setprecision(std::numeric_limits<double>::max_digits10) << number;
    option->setVal(number_stream.str().c_str());
    return;
  }
  if (option->isIntOption()) {
    int number = 0;
    if (Tcl_GetInt(nullptr, value, &number) != TCL_OK) {
      throw std::invalid_argument(std::string(option->get_option_name()) + " requires an integer: " + value);
    }
    const std::string canonical = std::to_string(number);
    option->setVal(canonical.c_str());
    return;
  }
  option->setVal(value);
}

void SdcTclCmd::warn(const std::string& message) const
{
  STALOG.warn(Loc::current(), message);
}

void SdcTclCmd::setResult(std::string result)
{
  _result = std::move(result);
  _has_result = true;
  _has_list_result = false;
}

void SdcTclCmd::setResult(std::vector<std::string> result, SdcObjectType type)
{
  _list_result = std::move(result);
  _list_result_type = type;
  _has_list_result = true;
  _has_result = false;
}

std::vector<Tcl_Obj*> SdcTclCmd::getOptionObjects(const std::string& name)
{
  auto option = _option_object_map.find(name);
  if (option == _option_object_map.end()) {
    return {};
  }
  Tcl_Obj* object = option->second;
  if (SdcObject::getType(object) != SdcObjectType::kNone) {
    return {object};
  }
  int count = 0;
  Tcl_Obj** elements = nullptr;
  if (Tcl_ListObjGetElements(nullptr, object, &count, &elements) != TCL_OK) {
    throw std::invalid_argument(name + " requires an object list");
  }
  return count == 0 ? std::vector<Tcl_Obj*>() : std::vector<Tcl_Obj*>(elements, elements + count);
}

std::vector<std::string> SdcTclCmd::getObjectCollectionNames(Tcl_Obj* object)
{
  std::vector<std::string> names;
  const auto* list_type = Tcl_GetObjType("list");
  std::function<void(Tcl_Obj*, bool, int)> append = [&](Tcl_Obj* value, bool root, int depth) {
    if (depth > 64)
      throw std::invalid_argument("object collection nesting exceeds 64 levels");
    if (SdcObject::getType(value) != SdcObjectType::kNone || (!root && value->typePtr != list_type)) {
      names.emplace_back(Tcl_GetString(value));
      return;
    }
    int count = 0;
    Tcl_Obj** elements = nullptr;
    if (Tcl_ListObjGetElements(nullptr, value, &count, &elements) != TCL_OK)
      throw std::invalid_argument("invalid object collection");
    for (int i = 0; i < count; ++i)
      append(elements[i], false, depth + 1);
  };
  append(object, true, 0);
  return names;
}

std::vector<std::string> SdcTclCmd::getOptionObjectNames(const std::string& name)
{
  const auto option = _option_object_map.find(name);
  return option == _option_object_map.end() ? std::vector<std::string>{} : getObjectCollectionNames(option->second);
}

void SdcTclCmd::resetExecutionState()
{
  resetOptionArgValue();
  _error_message.clear();
  _result.clear();
  _list_result.clear();
  _list_result_type = SdcObjectType::kNone;
  _option_object_map.clear();
  _option_object_value_list.clear();
  _option_value_list.clear();
  _has_result = false;
  _has_list_result = false;
}

void SdcTclCmd::setInterpreterError(Tcl_Interp* interp) const
{
  Tcl_SetObjResult(interp, Tcl_NewStringObj(_error_message.c_str(), static_cast<int>(_error_message.size())));
}

}  // namespace ista::sdc
