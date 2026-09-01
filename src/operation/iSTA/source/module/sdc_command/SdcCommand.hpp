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

#if __has_include(<tcl8.6/tcl.h>)
  #include <tcl8.6/tcl.h>
#else
  #include <tcl.h>
#endif

#include <string>
#include <initializer_list>
#include <vector>

#include "Singleton.hpp"

namespace ista {

struct SdcError
{
  unsigned line_number = 0;
  std::string message;
};

class SdcCommand
{
 public:
  struct Command
  {
    const char* name;
    Tcl_ObjCmdProc* proc;
    ClientData client_data = nullptr;
    Tcl_CmdDeleteProc* delete_proc = nullptr;
  };

  static void initInst();
  static void initInst(std::initializer_list<Command> commands);
  static SdcCommand& getInst();
  static void destroyInst();
  static bool isInitialized();

  SdcCommand();
  SdcCommand(std::initializer_list<Command> commands);
  ~SdcCommand();

  Tcl_Interp* getInterp() const { return _interp; }

  Tcl_Command createCmd(const char* cmd_name, Tcl_ObjCmdProc* proc, ClientData client_data = nullptr,
                        Tcl_CmdDeleteProc* delete_proc = nullptr);
  void registerCommands(std::initializer_list<Command> commands);

  int evalScriptFile(const std::string& file_name);
  int evalString(const std::string& command);

  const std::vector<SdcError>& getErrors() const { return _errors; }
  void clearErrors() { _errors.clear(); }

 private:
  SdcCommand(const SdcCommand&) = delete;
  SdcCommand& operator=(const SdcCommand&) = delete;

  int evalScript(const std::string& script);
  void addError(unsigned line_number, std::string message);

  Tcl_Interp* _interp = nullptr;
  std::vector<SdcError> _errors;
};

}  // namespace ista
