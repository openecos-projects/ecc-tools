// SPDX-License-Identifier: MulanPSL-2.0
#include "STAInterface.hpp"
#include "WriteSdc.hpp"
#include "tcl_sta.h"

namespace tcl {
int writeSdcCommand(ClientData client_data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  if (!STAI.isInitialized()) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("STA must be initialized before write_sdc", -1));
    return TCL_ERROR;
  }
  return ista::sdc::writeSdcCommand(client_data, interp, objc, objv);
}
}  // namespace tcl
