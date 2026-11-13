// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include <exception>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "query/ObjectCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {
TclAllClocks::TclAllClocks(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
}

unsigned TclAllClocks::exec()
{
  // all_clocks uses the same query path as get_clocks so ordering and object
  // matching stay consistent.
  setResult(findObjects(interpreter(), STADM.getDatabase(), {"*"}, QueryObjectType::kClock), SdcObjectType::kClock);
  return 1;
}

}
