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
/**
 * @file ScriptEngine.cpp
 * @author simin tao (taosm@pcl.ac.cn)
 * @brief The file is the implementation of the script engine based on tcl.
 * @version 0.1
 * @date 2020-11-18
 */

#include "ScriptEngine.hh"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string_view>
#include <utility>

namespace ecc {
ScriptEngine* ScriptEngine::_instance = nullptr;

ScriptEngine::ScriptEngine()
{
  _interp = Tcl_CreateInterp();
}
ScriptEngine::~ScriptEngine()
{
  Tcl_DeleteInterp(_interp);
}

/**
 * @brief Get the script engine or create one.
 *
 * @return ScriptEngine* The script engine.
 */
ScriptEngine* ScriptEngine::getOrCreateInstance()
{
  static std::mutex mt;
  if (_instance == nullptr) {
    std::lock_guard<std::mutex> lock(mt);
    if (_instance == nullptr) {
      _instance = new ScriptEngine();
    }
  }
  return _instance;
}

/**
 * @brief Close the script engine.
 *
 */
void ScriptEngine::destroyInstance()
{
  delete _instance;
  _instance = nullptr;
}

/**
 * @brief Create a Cmd object
 *
 * @param cmd_name The user defined cmd name.
 * @param proc The cmd callback function.
 * @param cmd_Data The cmd data that will be copied to proc.
 * @param delete_proc The deleteProc will be invoked before the command is
 * deleted through a call to Tcl_DeleteCommand.
 * @return int
 */
Tcl_Command ScriptEngine::createCmd(const char* cmd_name, Tcl_ObjCmdProc* proc, void* cmd_data, Tcl_CmdDeleteProc* delete_proc)
{
  return Tcl_CreateObjCommand(_interp, cmd_name, proc, cmd_data, delete_proc);
}

/**
 * @brief Call the tcl interpreter to execuate the tcl file.
 *
 * @param file_name The script file.
 * @return int The return code.
 */
int ScriptEngine::evalScriptFile(const char* file_name)
{
  return Tcl_EvalFile(_interp, file_name);
}

/**
 * @brief Call the tcl interpreter to execuate the cmd string.
 *
 * @param cmd_str The cmd string.
 * @return int The return code.
 */
int ScriptEngine::evalString(const char* cmd_str)
{
  return Tcl_Eval(_interp, cmd_str);
}

const char* ScriptEngine::getTclFileName()
{
  evalString("set fileName [dict get [info frame 2] file]");
  const char* file_name = Tcl_GetVar(_interp, "fileName", 0);
  return file_name;
}

/**
 * @brief Get the current tcl line no.
 *
 * @return unsigned The tcl file line no.
 */
unsigned ScriptEngine::getTclLineNo()
{
  evalString("set lineNum [dict get [info frame 2] line]");
  const char* line_no = Tcl_GetVar(_interp, "lineNum", 0);
  return static_cast<unsigned>(std::atoi(line_no));
}

void ScriptEngine::setResult(char* result)
{
  Tcl_SetResult(_interp, result, nullptr);
}

/**
 * @brief Append the cmd execuate result to tcl interpretr.
 *
 * @param result The cmd execuate result.
 */
void ScriptEngine::appendResult(char* result)
{
  Tcl_AppendResult(_interp, result, nullptr);
}

/**
 * @brief Get the result from interpreter.
 *
 * @return const char* The tcl result that is string format.
 */
const char* ScriptEngine::getResult()
{
  return Tcl_GetStringResult(_interp);
}

TclOption::TclOption(const char* option_name, unsigned is_arg) : _option_name(option_name), _is_arg(is_arg)
{
}

TclOption::~TclOption() = default;

void checkTclOption(TclOption* option, const char* option_name, Loc location)
{
  if (option == nullptr) {
    ECCLOG.error(location, "The Tcl option '", option_name, "' is null.");
  }
}

std::vector<std::string> TclOption::splitList(const char* val)
{
  int value_count = 0;
  const char** value_list = nullptr;
  if (Tcl_SplitList(nullptr, val, &value_count, &value_list) != TCL_OK) {
    return {};
  }

  std::vector<std::string> result;
  result.reserve(value_count);
  for (int value_idx = 0; value_idx < value_count; value_idx++) {
    result.emplace_back(value_list[value_idx]);
  }
  Tcl_Free(reinterpret_cast<char*>(value_list));
  return result;
}

TclSwitchOption::TclSwitchOption(const char* option_name) : TclOption(option_name, 0)
{
}

TclSwitchOption::~TclSwitchOption() = default;

TclDoubleOption::TclDoubleOption(const char* option_name, unsigned is_arg, float default_val)
    : TclOption(option_name, is_arg), _default_val(default_val)
{
}

TclDoubleOption::~TclDoubleOption() = default;

TclStringOption::TclStringOption(const char* option_name, unsigned is_arg, const char* default_val)
    : TclOption(option_name, is_arg)
{
  if (default_val != nullptr) {
    _default_val = default_val;
  }
}

TclStringOption::~TclStringOption() = default;

TclStringListListOption::TclStringListListOption(const char* option_name, unsigned is_arg, std::vector<StrList>&& default_val)
    : TclOption(option_name, is_arg), _default_val(std::move(default_val))
{
}

void TclStringListListOption::setVal(const char* val)
{
  const char* first_char = val;
  while (*first_char == ' ') {
    first_char++;
  }
  if (*first_char != '{') {
    _val.push_back(splitList(val));
    _is_set_val = 1;
    return;
  }

  int outer_count = 0;
  const char** outer_value_list = nullptr;
  if (Tcl_SplitList(nullptr, val, &outer_count, &outer_value_list) != TCL_OK) {
    return;
  }

  for (int outer_idx = 0; outer_idx < outer_count; outer_idx++) {
    int inner_count = 0;
    const char** inner_value_list = nullptr;
    if (Tcl_SplitList(nullptr, outer_value_list[outer_idx], &inner_count, &inner_value_list) != TCL_OK) {
      Tcl_Free(reinterpret_cast<char*>(outer_value_list));
      return;
    }

    StrList value_list;
    for (int inner_idx = 0; inner_idx < inner_count; inner_idx++) {
      value_list.emplace_back(inner_value_list[inner_idx]);
    }
    Tcl_Free(reinterpret_cast<char*>(inner_value_list));
    _val.push_back(value_list);
  }
  Tcl_Free(reinterpret_cast<char*>(outer_value_list));

  _is_set_val = 1;
}

TclStringListListListOption::TclStringListListListOption(const char* option_name, unsigned is_arg,
                                                         std::vector<StrListList>&& default_val)
    : TclOption(option_name, is_arg), _default_val(std::move(default_val))
{
}

void TclStringListListListOption::setVal(const char* val)
{
  int outer_count = 0;
  const char** outer_value_list = nullptr;
  if (Tcl_SplitList(nullptr, val, &outer_count, &outer_value_list) != TCL_OK) {
    return;
  }

  for (int outer_idx = 0; outer_idx < outer_count; outer_idx++) {
    int middle_count = 0;
    const char** middle_value_list = nullptr;
    if (Tcl_SplitList(nullptr, outer_value_list[outer_idx], &middle_count, &middle_value_list) != TCL_OK) {
      Tcl_Free(reinterpret_cast<char*>(outer_value_list));
      return;
    }

    StrListList value_list;
    for (int middle_idx = 0; middle_idx < middle_count; middle_idx++) {
      int inner_count = 0;
      const char** inner_value_list = nullptr;
      if (Tcl_SplitList(nullptr, middle_value_list[middle_idx], &inner_count, &inner_value_list) != TCL_OK) {
        Tcl_Free(reinterpret_cast<char*>(middle_value_list));
        Tcl_Free(reinterpret_cast<char*>(outer_value_list));
        return;
      }

      StrList inner_value;
      for (int inner_idx = 0; inner_idx < inner_count; inner_idx++) {
        inner_value.emplace_back(inner_value_list[inner_idx]);
      }
      Tcl_Free(reinterpret_cast<char*>(inner_value_list));
      value_list.push_back(inner_value);
    }
    Tcl_Free(reinterpret_cast<char*>(middle_value_list));
    _val.push_back(value_list);
  }
  Tcl_Free(reinterpret_cast<char*>(outer_value_list));

  _is_set_val = 1;
}

TclStringListListListListOption::TclStringListListListListOption(const char* option_name, unsigned is_arg,
                                                                 std::vector<StrListListList>&& default_val)
    : TclOption(option_name, is_arg), _default_val(std::move(default_val))
{
}

void TclStringListListListListOption::setVal(const char* val)
{
  int outer_count = 0;
  const char** outer_value_list = nullptr;
  if (Tcl_SplitList(nullptr, val, &outer_count, &outer_value_list) != TCL_OK) {
    return;
  }

  for (int outer_idx = 0; outer_idx < outer_count; outer_idx++) {
    int middle_count = 0;
    const char** middle_value_list = nullptr;
    if (Tcl_SplitList(nullptr, outer_value_list[outer_idx], &middle_count, &middle_value_list) != TCL_OK) {
      Tcl_Free(reinterpret_cast<char*>(outer_value_list));
      return;
    }

    StrListListList value_list;
    for (int middle_idx = 0; middle_idx < middle_count; middle_idx++) {
      int inner_count = 0;
      const char** inner_string_list = nullptr;
      if (Tcl_SplitList(nullptr, middle_value_list[middle_idx], &inner_count, &inner_string_list) != TCL_OK) {
        Tcl_Free(reinterpret_cast<char*>(middle_value_list));
        Tcl_Free(reinterpret_cast<char*>(outer_value_list));
        return;
      }

      StrListList inner_value_list;
      for (int inner_idx = 0; inner_idx < inner_count; inner_idx++) {
        int leaf_count = 0;
        const char** leaf_value_list = nullptr;
        if (Tcl_SplitList(nullptr, inner_string_list[inner_idx], &leaf_count, &leaf_value_list) != TCL_OK) {
          Tcl_Free(reinterpret_cast<char*>(inner_string_list));
          Tcl_Free(reinterpret_cast<char*>(middle_value_list));
          Tcl_Free(reinterpret_cast<char*>(outer_value_list));
          return;
        }

        StrList leaf_value;
        for (int leaf_idx = 0; leaf_idx < leaf_count; leaf_idx++) {
          leaf_value.emplace_back(leaf_value_list[leaf_idx]);
        }
        Tcl_Free(reinterpret_cast<char*>(leaf_value_list));
        inner_value_list.push_back(leaf_value);
      }
      Tcl_Free(reinterpret_cast<char*>(inner_string_list));
      value_list.push_back(inner_value_list);
    }
    Tcl_Free(reinterpret_cast<char*>(middle_value_list));
    _val.push_back(value_list);
  }
  Tcl_Free(reinterpret_cast<char*>(outer_value_list));

  _is_set_val = 1;
}

TclCmd::TclCmd(const char* cmd_name) : _cmd_name(cmd_name)
{
}

TclCmd::~TclCmd() = default;

/**
 * @brief Reset the option and arg value.
 *
 */
void TclCmd::resetOptionArgValue()
{
  for (auto& [option_name, option] : _options) {
    option->resetVal();
  }
}

std::map<std::string, std::unique_ptr<TclCmd>> TclCmds::_cmds;

/**
 * @brief The tcl cmd process callback function.
 *
 * @param clientData The callback data, which transparent from regiester
 * function.
 * @param interp The tcl interp.
 * @param objc The tcl cmd option and arg num count.
 * @param objv The tcl cmd option and arg obj.
 * @return int The process result, success return TCL_OK, else return
 * TCL_ERROR.
 */
int CmdProc(ClientData clientData, Tcl_Interp* interp, int objc, struct Tcl_Obj* const* objv)
{
  const char* cmd_name = Tcl_GetString(objv[0]);
  TclCmd* cmd = TclCmds::getTclCmd(cmd_name);
  cmd->resetOptionArgValue();

  bool next_is_option_val = false;
  TclOption* curr_option = nullptr;
  int arg_index = 0;
  for (int cnt = 1; cnt < objc; ++cnt) {
    struct Tcl_Obj* obj = objv[cnt];
    // get option lead string or arg
    const char* obj_str = Tcl_GetString(obj);
    if (!next_is_option_val) {
      TclOption* option = cmd->getOptionOrArg(obj_str);
      curr_option = option;
      if (option) {
        if (!option->isSwitchOption()) {
          // It is option, next should be option value if it is not switch
          // option,
          next_is_option_val = true;
        } else {
          // switch option
          option->setVal(nullptr);
        }
      } else {
        // should be arg, arg is need keep order.
        TclOption* arg = cmd->getArg(arg_index);
        ++arg_index;
        if (!arg) {
          ECCLOG.warn(Loc::current(), "The cmd ", cmd->get_cmd_name(), " syntax has error.");
          return TCL_ERROR;
        }

        arg->setVal(obj_str);
      }
    } else {
      curr_option->setVal(obj_str);
      next_is_option_val = false;
    }
  }

  if (next_is_option_val) {
    ECCLOG.warn(Loc::current(), "The cmd syntax has error ", curr_option->get_option_name(), " need val.");
  }

  unsigned result = cmd->exec();
  return result ? TCL_OK : TCL_ERROR;
}

/**
 * @brief Registe the tcl cmd.
 *
 * @param cmd
 */
void TclCmds::addTclCmd(std::unique_ptr<TclCmd> cmd)
{
  ScriptEngine::getOrCreateInstance()->createCmd(cmd->get_cmd_name(), CmdProc, cmd.get());
  _cmds.emplace(cmd->get_cmd_name(), std::move(cmd));
}

/**
 * @brief Get tcl cmd accord to cmd name.
 *
 */
TclCmd* TclCmds::getTclCmd(const char* cmd_name)
{
  auto it = _cmds.find(cmd_name);
  if (it != _cmds.end()) {
    return it->second.get();
  }
  return nullptr;
}

/**
 * @brief Encode the pointer for transmit.
 *
 * @param pointer
 * @return char*
 */
std::string TclEncodeResult::encode(void* pointer)
{
  std::ostringstream stream;
  stream << _encode_preamble << pointer;
  return stream.str();
}

/**
 * @brief decode the encode string to pointer.
 *
 * @param encode_str
 */
void* TclEncodeResult::decode(const char* encode_str)
{
  std::string_view pointer_str(encode_str);
  if (pointer_str.starts_with(_encode_preamble)) {
    pointer_str.remove_prefix(std::strlen(_encode_preamble));
  }
  const int hex = 16;
  auto pointer_address = static_cast<uintptr_t>(std::stoull(std::string(pointer_str), nullptr, hex));
  return reinterpret_cast<void*>(pointer_address);
}

bool containWildcard(const char* pattern)
{
  return (pattern[0] == '-') && (strpbrk(pattern, "*?") != nullptr);
}

bool matchWildcardWithtarget(const char* const pattern, const char* const target)
{
  const char* p = pattern;
  const char* t = target;

  while (1) {
    while (*p && *t && (*p == *t)) {
      ++p;
      ++t;
    }

    if (*p == '\0') {
      return (*t == '\0') ? true : false;
    }

    if (*t == '\0') {
      if (*p == '\0') {
        return true;
      } else {
        while (*p == '*') {
          ++p;
          if (*p == '\0') {
            return true;
          } else if (*p == '*') {
            continue;
          } else {
            return false;
          }
        }
      }
    }

    if (*p == '?') {
      ++p;
      ++t;
    } else if (*p == '*') {
      if (*(p + 1) == '\0') {
        return true;
      } else {
        if (*(p + 1) == *t) {
          ++p;
        } else {
          ++t;
        }
      }
    } else if (*p != *t) {
      return false;
    }
  }
}

}  // namespace ecc
