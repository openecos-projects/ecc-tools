// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "SdcCommand.hpp"

namespace ista::sdc {

// Shared command implementation for the SDC interpreter and the public shell.
// The caller must have initialized the STA database.
int writeSdcCommand(ClientData client_data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

}  // namespace ista::sdc
