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

#include "PWHeader.hpp"

namespace ipw {

class SdcCommand
{
 public:
  static void initInst();
  static SdcCommand& getInst();
  static void destroyInst();

  Tcl_Interp* getInterp() const { return _interp; }

  Tcl_Command createCmd(const char* cmd_name, Tcl_ObjCmdProc* proc);

  int evalScriptFile(const std::string& file_name);

  int32_t get_error_line_number() const { return _error_line_number; }
  std::string& get_error_message() { return _error_message; }

 private:
  // self
  static SdcCommand* _sdc_command_instance;

  SdcCommand();
  SdcCommand(const SdcCommand&) = delete;
  SdcCommand& operator=(const SdcCommand&) = delete;
  ~SdcCommand();

  int evalScript(const std::string& script);
  void clearError();
  void setError(int32_t line_number, std::string message);

  Tcl_Interp* _interp = nullptr;
  int32_t _error_line_number = 0;
  std::string _error_message;
};

}  // namespace ipw
