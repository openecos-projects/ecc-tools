// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include "DataManager.hpp"
#include "clock/ClockCommands.hpp"
#include "object/ObjectQuery.hpp"

namespace ista::sdc {

TclRemovePropagatedClock::TclRemovePropagatedClock(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("objects", 1));
}

unsigned TclRemovePropagatedClock::exec()
{
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!object_option->is_set_val() || object_option->getStringList().empty()) {
    setTclError(std::string(get_cmd_name()) + " requires an object collection");
    return 0;
  }
  Database& database = STADM.getDatabase();
  try {
    for (const std::string& clock_name : findClocksFromObjects(database, object_option->getStringList())) {
      database.get_timing_constraint().get_clock_map().at(clock_name).set_is_propagated(false);
    }
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}  // namespace ista::sdc
