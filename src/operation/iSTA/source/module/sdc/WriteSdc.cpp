// SPDX-License-Identifier: MulanPSL-2.0
#include "WriteSdc.hpp"

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"
#include "SdcWriter.hpp"

namespace ista::sdc {

int writeSdcCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
  try {
    std::optional<std::string> filename;
    for (int index = 1; index < objc; ++index) {
      int length = 0;
      const char* text = Tcl_GetStringFromObj(objv[index], &length);
      const std::string argument(text, length);
      if (argument == "-nosplit") {
        // The serializer already writes one complete command per line.
        continue;
      }
      if (!argument.empty() && argument.front() == '-')
        throw std::invalid_argument("write_sdc does not support option '" + argument + "'; usage: write_sdc file_name [-nosplit]");
      if (filename)
        throw std::invalid_argument("write_sdc requires exactly one file_name");
      if (argument.empty() || argument.find('\0') != std::string::npos)
        throw std::invalid_argument("write_sdc requires a non-empty file_name without NUL characters");
      filename = argument;
    }
    if (!filename)
      throw std::invalid_argument("usage: write_sdc file_name [-nosplit]");
    writeSdcFile(STADM.getDatabase(), *filename);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
    return TCL_OK;
  } catch (const std::exception& error) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
    return TCL_ERROR;
  }
}

}  // namespace ista::sdc
