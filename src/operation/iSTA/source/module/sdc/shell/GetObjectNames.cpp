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
#include "shell/ObjectCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {
TclGetFullName::TclGetFullName(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("object", 1));
}

unsigned TclGetFullName::exec()
{
  ecc::TclOption* object = getOptionOrArg("object");
  if (!object->is_set_val()) {
    setTclError(std::string(get_cmd_name()) + " requires an object");
    return 0;
  }

  try {
    setResult(getFullNames(interpreter(), STADM.getDatabase(), getOptionObjects("object")));
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}
