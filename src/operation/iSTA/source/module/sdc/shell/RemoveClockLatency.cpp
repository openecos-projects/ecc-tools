#include "shell/EditingCommands.hpp"
// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"
#include "clock/ClockCommands.hpp"
#include "query/ObjectQuery.hpp"

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
  Database& database = STADM.getDatabase();
  try {
    const auto targets = resolveClockTargets(interpreter(), database, getOptionObjects("objects"));
    std::set<std::string> selectors;
    if (getOptionOrArg("-clock")->is_set_val()) {
      for (const auto& [kind, name] : resolveClockTargets(interpreter(), database, getOptionObjects("-clock"), true))
        selectors.insert(name);
      for (const auto& [kind, name] : targets)
        if (kind == QueryObjectType::kClock)
          throw std::invalid_argument("-clock requires pin or port targets");
    }
    const bool source = getOptionOrArg("-source")->is_set_val();
    for (const auto& [kind, name] : targets) {
      if (kind == QueryObjectType::kClock) {
        auto& clock = database.sdc().get_clock_map().at(name);
        if (source)
          clock.clear_source_latency();
        else
          clock.clear_network_latency();
      } else {
        auto found = database.sdc().clockPinConstraints().find(name);
        if (found == database.sdc().clockPinConstraints().end())
          continue;
        for (auto& [selector, value] : found->second) {
          if (!selectors.empty() && !selectors.contains(selector))
            continue;
          if (source)
            value.source_latency.clear();
          else {
            value.network_latency.clear();
            if (value.propagated == false)
              value.propagated.reset();
          }
        }
      }
    }
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}  // namespace ista::sdc
