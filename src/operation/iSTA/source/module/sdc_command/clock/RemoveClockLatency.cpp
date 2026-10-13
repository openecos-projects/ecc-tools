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

TclRemoveClockLatency::TclRemoveClockLatency(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-source"));
  addOption(new ecc::TclStringOption("-clock", 0));
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclRemoveClockLatency::exec()
{
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!object_option->is_set_val()) {
    setTclError(std::string(get_cmd_name()) + " requires an object collection");
    return 0;
  }
  if (getOptionOrArg("-clock")->is_set_val()) {
    warn(std::string(get_cmd_name()) + " -clock is ignored for clock objects");
  }

  Database& database = STADM.getDatabase();
  std::set<std::string> clocks;
  try {
    clocks = findClocks(database, parseObjectPatterns(object_option->getStringVal(), false));
  } catch (const std::exception& error) {
    setTclError(std::string(get_cmd_name()) + " currently supports clock objects: " + error.what());
    return 0;
  }
  const bool source = getOptionOrArg("-source")->is_set_val();
  for (const std::string& name : clocks) {
    TimingClock& clock = database.get_timing_constraint().get_clock_map().at(name);
    if (source) {
      clock.clear_source_latency();
    } else {
      clock.clear_network_latency();
    }
  }
  return 1;
}

}  // namespace ista::sdc
