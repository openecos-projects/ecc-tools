// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include "DataManager.hpp"
#include "object/GetName.hpp"
#include "object/ObjectQuery.hpp"
#include "object/ObjectCommands.hpp"

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
    setResult(getFullNames(STADM.getDatabase(), object->getStringVal()));
  } catch (const std::exception& error) {
    setTclError(error.what());
    return 0;
  }
  return 1;
}

TclGetName::TclGetName(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclStringOption("object", 1));
}

unsigned TclGetName::exec()
{
  ecc::TclOption* object = getOptionOrArg("object");
  if (!object->is_set_val()) {
    setTclError("get_name requires one object");
    return 0;
  }
  Database& database = STADM.getDatabase();
  std::string object_name = object->getStringVal();
  // Query collections contain canonical names, including flattened hierarchy.
  // Check those first, so a name containing Tcl list metacharacters is intact.
  if (database.get_instance_map().contains(object_name) || database.get_net_map().contains(object_name)
      || database.get_timing_constraint().get_clock_map().contains(object_name)) {
    setResult(object_name);
    return 1;
  }
  auto pin = database.get_pin_map().find(object_name);
  if (pin != database.get_pin_map().end()) {
    setResult(pin->second.get_pin_name());
    return 1;
  }
  std::vector<std::string> objects = parseObjectPatterns(object_name, false);
  if (objects.size() != 1) {
    setTclError("get_name requires one object");
    return 0;
  }
  ObjectQueryOptions options;
  options.exact = true;
  std::vector<std::string> matches = findObjects(database, objects, QueryObjectType::kAny, options);
  if (matches.size() != 1) {
    setTclError("get_name requires one existing object: " + object_name);
    return 0;
  }
  object_name = matches.front();
  pin = database.get_pin_map().find(object_name);
  if (pin != database.get_pin_map().end()) {
    setResult(pin->second.get_pin_name());
  } else if (database.get_instance_map().contains(object_name) || database.get_net_map().contains(object_name)
             || database.get_timing_constraint().get_clock_map().contains(object_name)) {
    setResult(object_name);
  } else {
    setResult(object_name.substr(object_name.find_last_of('/') + 1));
  }
  return 1;
}

}  // namespace ista::sdc
