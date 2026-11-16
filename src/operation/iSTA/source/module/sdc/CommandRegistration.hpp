// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include "DataManager.hpp"
#include "SdcCommand.hpp"
#include "SdcTclCmd.hpp"
namespace ista::sdc {
template <typename Command>
void registerCommand(SdcCommand& interpreter, const char* name)
{
  interpreter.createCmd(name, &executeTclCommand<Command>);
}

template <typename Command>
int executeConstraintCommand(ClientData data, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  if (DataManager::isInitialized())
    STADM.getDatabase().sdc().markChanged();
  return executeTclCommand<Command>(data, interp, objc, objv);
}

template <typename Command>
void registerConstraintCommand(SdcCommand& interpreter, const char* name)
{
  interpreter.createCmd(name, &executeConstraintCommand<Command>);
}

}
