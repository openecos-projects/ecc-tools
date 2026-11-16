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
#include "compat/CompatibilityCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {
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
  if (database.network().instances().contains(object_name) || database.network().hasNet(object_name) || database.sdc().get_clock_map().contains(object_name)) {
    setResult(object_name);
    return 1;
  }
  auto pin = database.network().pins().find(object_name);
  if (pin != database.network().pins().end()) {
    setResult(pin->second.get_pin_name());
    return 1;
  }
  std::vector<std::string> objects = parseObjectPatterns(interpreter(), object_name, false);
  if (objects.size() != 1) {
    setTclError("get_name requires one object");
    return 0;
  }
  ObjectQueryOptions options;
  options.exact = true;
  std::vector<std::string> matches = findObjects(interpreter(), database, objects, QueryObjectType::kAny, options);
  if (matches.size() != 1) {
    setTclError("get_name requires one existing object: " + object_name);
    return 0;
  }
  object_name = matches.front();
  pin = database.network().pins().find(object_name);
  if (pin != database.network().pins().end()) {
    setResult(pin->second.get_pin_name());
  } else if (database.network().instances().contains(object_name) || database.network().hasNet(object_name)
             || database.sdc().get_clock_map().contains(object_name)) {
    setResult(object_name);
  } else {
    setResult(object_name.substr(object_name.find_last_of('/') + 1));
  }
  return 1;
}

}
