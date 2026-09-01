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
#include "SdcCommand.hpp"

#include "STAHeader.hpp"

namespace ista {

void SdcCommand::initInst()
{
  Singleton<SdcCommand>::initInst();
}

void SdcCommand::initInst(std::initializer_list<Command> commands)
{
  Singleton<SdcCommand>::initInst(commands);
}

SdcCommand& SdcCommand::getInst()
{
  return Singleton<SdcCommand>::getInst();
}

void SdcCommand::destroyInst()
{
  Singleton<SdcCommand>::destroyInst();
}

SdcCommand::SdcCommand()
{
  _interp = Tcl_CreateInterp();
}

SdcCommand::~SdcCommand()
{
  if (_interp != nullptr) {
    Tcl_DeleteInterp(_interp);
  }
}

SdcCommand::SdcCommand(std::initializer_list<Command> commands) : SdcCommand()
{
  registerCommands(commands);
}

bool SdcCommand::isInitialized()
{
  return Singleton<SdcCommand>::isInitialized();
}

Tcl_Command SdcCommand::createCmd(const char* cmd_name, Tcl_ObjCmdProc* proc, ClientData client_data, Tcl_CmdDeleteProc* delete_proc)
{
  return Tcl_CreateObjCommand(_interp, cmd_name, proc, client_data, delete_proc);
}

void SdcCommand::registerCommands(std::initializer_list<Command> commands)
{
  for (const Command& command : commands) {
    createCmd(command.name, command.proc, command.client_data, command.delete_proc);
  }
}

int SdcCommand::evalScriptFile(const std::string& file_name)
{
  clearErrors();
  std::ifstream script_file(file_name);
  if (!script_file.is_open()) {
    addError(0, "failed to open SDC file '" + file_name + "'");
    return TCL_ERROR;
  }

  const std::string script((std::istreambuf_iterator<char>(script_file)), std::istreambuf_iterator<char>());
  return evalScript(script);
}

int SdcCommand::evalString(const std::string& command)
{
  clearErrors();
  const int result = Tcl_EvalEx(_interp, command.c_str(), static_cast<int>(command.size()), TCL_EVAL_GLOBAL);
  if (result != TCL_OK) {
    addError(Tcl_GetErrorLine(_interp), Tcl_GetStringResult(_interp));
  }
  return result;
}

int SdcCommand::evalScript(const std::string& script)
{
  clearErrors();
  int result = TCL_OK;
  std::size_t offset = 0;
  unsigned line_number = 1;

  while (offset < script.size()) {
    Tcl_Parse parse{};
    const char* command_start = script.data() + offset;
    const int parse_result = Tcl_ParseCommand(_interp, command_start, static_cast<int>(script.size() - offset), 0, &parse);
    const char* command_end = parse.commandStart + parse.commandSize;
    std::size_t consumed = static_cast<std::size_t>(command_end - command_start);
    if (consumed == 0) {
      consumed = script.size() - offset;
    }

    const unsigned command_line = line_number + static_cast<unsigned>(std::count(command_start, parse.commandStart, '\n'));
    if (parse_result != TCL_OK) {
      addError(command_line, Tcl_GetStringResult(_interp));
      Tcl_ResetResult(_interp);
      Tcl_FreeParse(&parse);
      return TCL_ERROR;
    }

    if (parse.numWords > 0) {
      Tcl_SetErrorLine(_interp, static_cast<int>(command_line));
      const int command_result = Tcl_EvalEx(_interp, parse.commandStart, parse.commandSize, TCL_EVAL_GLOBAL);
      if (command_result != TCL_OK) {
        addError(command_line, Tcl_GetStringResult(_interp));
        Tcl_ResetResult(_interp);
        result = TCL_ERROR;
      }
    }

    line_number += static_cast<unsigned>(std::count(command_start, command_start + consumed, '\n'));
    offset += consumed;
    Tcl_FreeParse(&parse);
  }

  return result;
}

void SdcCommand::addError(unsigned line_number, std::string message)
{
  _errors.emplace_back(SdcError{line_number, std::move(message)});
}

}  // namespace ista
