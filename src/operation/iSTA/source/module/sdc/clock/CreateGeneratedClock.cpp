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
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "clock/ClockCommands.hpp"
#include "network/Network.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

TclCreateGeneratedClock::TclCreateGeneratedClock(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  for (const char* option : {"-name", "-master_clock", "-comment"}) {
    addOption(new ecc::TclStringOption(option, 0));
  }
  addOption(new ecc::TclStringListOption("-source", 0));
  addOption(new ecc::TclStringListOption("objects", 1));
  for (const char* option : {"-divide_by", "-multiply_by", "-duty_cycle"}) {
    addOption(new ecc::TclDoubleOption(option, 0));
  }
  for (const char* option : {"-edges", "-edge_shift"}) {
    addOption(new ecc::TclDoubleListOption(option, 0));
  }
  for (const char* option : {"-add", "-invert", "-preinvert", "-combinational"}) {
    addOption(new ecc::TclSwitchOption(option));
  }
}

unsigned TclCreateGeneratedClock::exec()
{
  Database& database = STADM.getDatabase();
  if (!getOptionOrArg("-source")->is_set_val() || !getOptionOrArg("objects")->is_set_val()) {
    setTclError("create_generated_clock requires -source and target pins/ports");
    return 0;
  }
  std::vector<std::string> sources;
  std::vector<std::string> targets;
  try {
    sources = findClockSources(interpreter(), database, getOptionObjects("-source"));
    targets = findClockSources(interpreter(), database, getOptionObjects("objects"));
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  if (sources.size() != 1 || targets.empty()) {
    setTclError("invalid generated clock source or targets");
    return 0;
  }
  if (getOptionOrArg("-add")->is_set_val()) {
    if (!getOptionOrArg("-name")->is_set_val()) {
      setTclError("create_generated_clock -add requires -name");
      return 0;
    }
    if (!getOptionOrArg("-master_clock")->is_set_val()) {
      setTclError("create_generated_clock -add requires -master_clock");
      return 0;
    }
  }
  const std::string name = getOptionOrArg("-name")->is_set_val() ? getOptionOrArg("-name")->getStringVal() : targets.front();
  GeneratedClockDefinition definition;
  if (getOptionOrArg("-master_clock")->is_set_val()) {
    definition.master_clock = findClock(interpreter(), database, getOptionObject("-master_clock"));
  }
  definition.master_source = sources.front();
  definition.targets = targets;
  definition.master_source_kind = findClockSourceKinds(interpreter(), database, getOptionObjects("-source"), sources).front();
  definition.target_kinds = findClockSourceKinds(interpreter(), database, getOptionObjects("objects"), targets);
  if (getOptionOrArg("-divide_by")->is_set_val())
    definition.divide_by = getOptionOrArg("-divide_by")->getDoubleVal();
  if (getOptionOrArg("-multiply_by")->is_set_val())
    definition.multiply_by = getOptionOrArg("-multiply_by")->getDoubleVal();
  if (getOptionOrArg("-duty_cycle")->is_set_val())
    definition.duty_cycle = getOptionOrArg("-duty_cycle")->getDoubleVal();
  if (getOptionOrArg("-edges")->is_set_val())
    definition.edges = getOptionOrArg("-edges")->getDoubleList();
  if (getOptionOrArg("-edge_shift")->is_set_val())
    definition.edge_shifts = timeValues("-edge_shift");
  definition.invert = getOptionOrArg("-invert")->is_set_val();
  definition.preinvert = getOptionOrArg("-preinvert")->is_set_val();
  definition.combinational = getOptionOrArg("-combinational")->is_set_val();
  definition.add = getOptionOrArg("-add")->is_set_val();
  try {
    const std::string comment = getOptionOrArg("-comment")->is_set_val() ? getOptionOrArg("-comment")->getStringVal() : "";
    TimingClock generated = database.sdc().makeGeneratedClock(name, definition, comment);
    database.sdc().defineClock(database.network(), std::move(generated), targets, definition.add);
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

}  // namespace ista::sdc
