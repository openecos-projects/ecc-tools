// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <iosfwd>
#include "SdcCommand.hpp"
#include <ostream>
#include <string>

namespace ipw {
class Database;
namespace sdc {
int writeSdcCommand(ClientData, Tcl_Interp*, int, Tcl_Obj* const[]);
// Serialize effective constraints, without executing or retaining the source Tcl.
// Throws when a database object cannot be represented by the supported SDC format.
void writeSdc(Database& database, std::ostream& output);
// Atomically replace path only after serialization and file close succeed.
void writeSdcFile(Database& database, const std::string& path);
}  // namespace sdc
}  // namespace ipw
