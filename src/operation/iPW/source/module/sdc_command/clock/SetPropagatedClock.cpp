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
#include "DataManager.hpp"
#include "Logger.hpp"
#include "object/ObjectQuery.hpp"
#include "clock/ClockCommands.hpp"

namespace ipw::sdc {

namespace {

std::set<std::string> findPropagatedClocks(Database& database, const std::vector<std::string>& objects)
{
  std::set<std::string> result;
  for (const std::string& object : objects) {
    const std::vector<std::string> direct_clocks = findObjects(database, {object}, QueryObjectType::kClock);
    if (!direct_clocks.empty()) {
      result.insert(direct_clocks.begin(), direct_clocks.end());
      continue;
    }

    const std::vector<std::string> sources = findClockSources(database, {object});
    for (const std::string& source : sources) {
      for (auto& [clock_name, clock] : database.get_timing_constraint().get_clock_map()) {
        if (std::find(clock.get_source_list().begin(), clock.get_source_list().end(), source) != clock.get_source_list().end()) {
          result.insert(clock_name);
        }
      }
    }
  }
  if (result.empty()) {
    throw std::invalid_argument("set_propagated_clock resolved to empty clock collection");
  }
  return result;
}

}  // namespace

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
  const std::vector<std::string> clock_name_list = clock_option->getStringList();
  if (clock_name_list.empty()) {
    setTclError("set_propagated_clock requires at least one clock");
    return 0;
  }

  Database& database = PWDM.getDatabase();
  auto& clock_map = database.get_timing_constraint().get_clock_map();
  std::set<std::string> resolved_clocks;
  try {
    resolved_clocks = findPropagatedClocks(database, clock_name_list);
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  for (const std::string& clock_name : resolved_clocks) {
    if (clock_map.at(clock_name).get_source_list().empty()) {
      setTclError("set_propagated_clock cannot be applied to virtual clock: " + clock_name);
      return 0;
    }
  }
  for (const std::string& clock_name : resolved_clocks) {
    clock_map.at(clock_name).set_is_propagated(true);
  }
  return 1;
}

}  // namespace ipw::sdc
