// SPDX-License-Identifier: MulanPSL-2.0
// This consumer deliberately links only ista_sdc_frontend.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "SdcCommand.hpp"
#include "SdcTclCmd.hpp"
#include "query/ObjectQuery.hpp"

int main(int, char** argv)
{
  Tcl_FindExecutable(argv[0]);
  ista::SdcCommand interpreter;
  if (interpreter.evalString("expr {2 + 3}") != TCL_OK)
    return 1;
  if (std::string(Tcl_GetStringResult(interpreter.getInterp())) != "5")
    return 2;
  if (!interpreter.getErrors().empty())
    return 3;

  const auto directory = std::filesystem::temp_directory_path() / ("ista-sdc source [literal] $-" + std::to_string(getpid()));
  std::filesystem::create_directory(directory);
  struct Cleanup
  {
    std::filesystem::path directory;
    ~Cleanup()
    {
      std::error_code ignored;
      std::filesystem::remove_all(directory, ignored);
    }
  } cleanup{directory};
  const auto source = directory / "constraints.sdc";
  const auto helper = directory / "io.tcl";
  {
    std::ofstream(helper) << "set helper_script [info script]\nset output_load 0.2\n";
    std::ofstream(source) << "set constraint_script [info script]\n"
                             "source [file join [file dirname [info script]] io.tcl]\n"
                             "set resumed_script [info script]\n"
                             "return\n"
                             "error {commands after top-level return must not execute}\n";
  }
  if (interpreter.evalScriptFile(source.string()) != TCL_OK) {
    std::cerr << "SDC could not source its sibling helper: " << Tcl_GetStringResult(interpreter.getInterp()) << '\n';
    return 4;
  }
  const auto value = [&](const char* name) {
    const char* result = Tcl_GetVar(interpreter.getInterp(), name, TCL_GLOBAL_ONLY);
    return result ? std::string(result) : std::string();
  };
  if (value("constraint_script") != source.string() || value("helper_script") != helper.string() || value("resumed_script") != source.string()
      || value("output_load") != "0.2")
    return 5;
  if (interpreter.evalString("info script") != TCL_OK || std::string(Tcl_GetStringResult(interpreter.getInterp())) != "")
    return 6;
  {
    std::ofstream(source) << "# error line must be relative to this file\nset before_error 1\nerror {constraint failed}\n";
  }
  if (interpreter.evalScriptFile(source.string()) != TCL_ERROR || interpreter.getErrors().size() != 1 || interpreter.getErrors()[0].line_number != 3
      || interpreter.getErrors()[0].message != "constraint failed" || value("errorInfo").find(source.string()) == std::string::npos)
    return 7;
  if (interpreter.evalString("info script") != TCL_OK || std::string(Tcl_GetStringResult(interpreter.getInterp())) != "")
    return 8;
  if (interpreter.evalScriptFile((directory / "missing.sdc").string()) != TCL_ERROR || interpreter.getErrors().empty())
    return 9;
  return 0;
}
