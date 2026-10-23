// SPDX-License-Identifier: MulanPSL-2.0
#include "STAInterface.hpp"
#include "tcl_sta.h"

namespace tcl {
TclOutputSDC::TclOutputSDC(const char* name) : TclCmd(name)
{
  addOption(new ecc::TclStringOption("-file_path", 0));
}

unsigned TclOutputSDC::exec()
{
  auto* interp = ecc::ScriptEngine::getOrCreateInstance()->get_interp();
  auto* path = getOptionOrArg("-file_path");
  if (!path->is_set_val() || std::string(path->getStringVal()).empty()) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("output_sdc requires a non-empty -file_path", -1));
    return 0;
  }
  try {
    STAI.outputSDC(path->getStringVal());
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return 0;
  }
  return 1;
}
}  // namespace tcl
