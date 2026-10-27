// SPDX-License-Identifier: MulanPSL-2.0
// This consumer deliberately links only ista_sdc_frontend.
#include <string>

#include "SdcCommand.hpp"
#include "SdcTclCmd.hpp"
#include "query/ObjectQuery.hpp"

int main()
{
  ista::SdcCommand interpreter;
  if (interpreter.evalString("expr {2 + 3}") != TCL_OK)
    return 1;
  if (std::string(Tcl_GetStringResult(interpreter.getInterp())) != "5")
    return 2;
  return interpreter.getErrors().empty() ? 0 : 3;
}
