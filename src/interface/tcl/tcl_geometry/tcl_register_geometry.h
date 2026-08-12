#pragma once

#include "ScriptEngine.hh"
#include "UserShell.hh"
#include "tcl_geometry.h"

using namespace ieda;

namespace tcl {

int registerCmdGeometry()
{
  registerTclCmd(CmdGeometrySnapshot, "geometry_snapshot");
  return EXIT_SUCCESS;
}

}  // namespace tcl
