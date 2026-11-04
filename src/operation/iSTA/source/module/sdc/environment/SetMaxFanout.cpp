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
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "DataManager.hpp"
#include "environment/EnvironmentCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

TclSetMaxFanout::TclSetMaxFanout(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclDoubleOption("fanout", 1));
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclSetMaxFanout::exec()
{
  ecc::TclOption* fanout_option = getOptionOrArg("fanout");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!fanout_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("set_max_fanout requires a limit and an input port or design collection");
    return 0;
  }
  const double fanout = fanout_option->getDoubleVal();
  if (!std::isfinite(fanout) || fanout < 0.0) {
    setTclError("set_max_fanout must be finite and non-negative");
    return 0;
  }
  Database& database = STADM.getDatabase();
  const auto objects = getOptionObjects("objects");
  if (objects.empty()) {
    setTclError("set_max_fanout requires a non-empty collection");
    return 0;
  }
  bool design = false;
  std::set<std::string> ports;
  for (Tcl_Obj* value : objects) {
    const std::string object = Tcl_GetString(value);
    const auto type = SdcObject::getType(value);
    if ((type == SdcObjectType::kDesign || type == SdcObjectType::kNone) && !database.network().name().empty() && object == database.network().name()) {
      design = true;
      continue;
    }
    const std::vector<std::string> matches = resolvePinTargets(interpreter(), database, {value}, true, true);
    if (matches.empty()) {
      setTclError("set_max_fanout object not found: " + object);
      return 0;
    }
    for (const std::string& name : matches) {
      const PinDirection direction = database.network().pins().at(name).get_direction();
      if (direction != PinDirection::kInput && direction != PinDirection::kInout) {
        setTclError("set_max_fanout requires input ports; '" + name + "' is not an input port");
        return 0;
      }
      ports.insert(name);
    }
  }
  Sdc& constraint = database.sdc();
  if (design) {
    constraint.get_max_fanout() = fanout;
  }
  for (const std::string& port : ports) {
    constraint.get_port_max_fanout_map()[port] = fanout;
  }
  return 1;
}

}  // namespace ista::sdc
