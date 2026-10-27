// SPDX-License-Identifier: MulanPSL-2.0
#include "DataManager.hpp"
#include "object/ObjectCommands.hpp"
#include "object/ObjectQuery.hpp"

namespace ista::sdc {

TclAllClocks::TclAllClocks(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
}

unsigned TclAllClocks::exec()
{
  // all_clocks uses the same query path as get_clocks so ordering and object
  // matching stay consistent.
  setResult(findObjects(STADM.getDatabase(), {"*"}, QueryObjectType::kClock));
  return 1;
}

}  // namespace ista::sdc
