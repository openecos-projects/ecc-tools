// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include <exception>
#include <set>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "clock/ClockCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

TclSetPropagatedClock::TclSetPropagatedClock(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringListOption("clocks", 1));
}

unsigned TclSetPropagatedClock::exec()
{
  ecc::TclOption* clock_option = getOptionOrArg("clocks");
  if (!clock_option->is_set_val()) {
    setTclError("set_propagated_clock requires a clock collection");
    return 0;
  }
  Database& database = STADM.getDatabase();
  try {
    const auto targets = resolveClockTargets(interpreter(), database, getOptionObjects("clocks"));
    for (const auto& [kind, name] : targets) {
      if (kind == QueryObjectType::kClock) {
        auto& clock = database.sdc().get_clock_map().at(name);
        if (clock.get_source_list().empty()) {
          warn("set_propagated_clock cannot be applied to virtual clock '" + name + "'");
          continue;
        }
        clock.set_is_propagated(true);
        clock.clear_network_latency();
      } else {
        auto& selectors = database.sdc().clockPinConstraints()[name];
        selectors[""].propagated = true;
        for (auto& [selector, value] : selectors) {
          value.propagated = true;
          value.network_latency.clear();
        }
      }
    }
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

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
    const auto targets = resolveClockTargets(interpreter(), database, getOptionObjects("objects"));
    for (const auto& [kind, name] : targets) {
      if (kind == QueryObjectType::kClock)
        database.sdc().get_clock_map().at(name).set_is_propagated(false);
      else {
        auto found = database.sdc().clockPinConstraints().find(name);
        if (found == database.sdc().clockPinConstraints().end())
          continue;
        for (auto& [selector, value] : found->second)
          if (value.propagated == true)
            value.propagated.reset();
      }
    }
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}  // namespace ista::sdc
