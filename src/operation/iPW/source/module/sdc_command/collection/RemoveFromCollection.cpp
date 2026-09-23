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
#include "object/ObjectQuery.hpp"
#include "object/ObjectCommands.hpp"

namespace ipw::sdc {

TclRemoveFromCollection::TclRemoveFromCollection(const char* cmd_name, ClientData client_data) : SdcTclCmd(cmd_name, client_data)
{
  addOption(new ecc::TclSwitchOption("-intersect"));
  addOption(new ecc::TclStringOption("collection", 1));
  addOption(new ecc::TclStringOption("objects", 1));
}

unsigned TclRemoveFromCollection::exec()
{
  ecc::TclOption* collection_option = getOptionOrArg("collection");
  ecc::TclOption* object_option = getOptionOrArg("objects");
  if (!collection_option->is_set_val() || !object_option->is_set_val()) {
    setTclError("remove_from_collection requires a base collection and an object specification");
    return 0;
  }
  Database& database = PWDM.getDatabase();
  const std::vector<std::string> collection = parseObjectPatterns(collection_option->getStringVal(), false);
  const std::vector<std::string> objects = parseObjectPatterns(object_option->getStringVal(), false);
  std::optional<QueryObjectType> type;
  bool homogeneous = true;
  for (const std::string& name : collection) {
    QueryObjectType object_type = QueryObjectType::kAny;
    const auto pin = database.get_pin_map().find(name);
    if (pin != database.get_pin_map().end()) {
      object_type = pin->second.get_is_port() ? QueryObjectType::kPort : QueryObjectType::kPin;
    } else if (database.get_timing_constraint().get_clock_map().contains(name)) {
      object_type = QueryObjectType::kClock;
    } else if (name != database.get_design_name() && !database.get_instance_map().contains(name)) {
      setTclError("invalid collection object '" + name + "'");
      return 0;
    }
    homogeneous = homogeneous && (!type || *type == object_type);
    type = object_type;
  }
  std::set<std::string> selected(objects.begin(), objects.end());
  if (homogeneous && type && *type != QueryObjectType::kAny) {
    const std::vector<std::string> matches = findObjects(database, objects, *type);
    selected.insert(matches.begin(), matches.end());
  }
  const bool intersect = getOptionOrArg("-intersect")->is_set_val();
  std::vector<std::string> result;
  for (const std::string& name : collection) {
    if (selected.contains(name) == intersect) {
      result.push_back(name);
    }
  }
  setResult(std::move(result));
  return 1;
}

}  // namespace ipw::sdc
