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
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "query/ObjectCommands.hpp"
#include "query/ObjectQuery.hpp"

namespace ista::sdc {

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
  Database& database = STADM.getDatabase();
  const auto collection = getOptionObjects("collection");
  const auto objects = getOptionObjects("objects");
  const bool intersect = getOptionOrArg("-intersect")->is_set_val();
  std::vector<Tcl_Obj*> result;
  for (Tcl_Obj* object : collection) {
    const std::string name = Tcl_GetString(object);
    const auto object_type = SdcObject::getType(object);
    auto type = queryObjectType(object);
    if (object_type == SdcObjectType::kNone)
      type = getObjectType(database, name);
    if (type == QueryObjectType::kAny && name != database.network().name()) {
      setTclError("invalid collection object '" + name + "'");
      return 0;
    }
    bool selected = false;
    for (Tcl_Obj* selector : objects) {
      const auto selected_type = queryObjectType(selector);
      if (SdcObject::getType(selector) != SdcObjectType::kNone) {
        const bool same_type = object_type == SdcObjectType::kNone ? selected_type == type : SdcObject::getType(selector) == object_type;
        selected = selected || (same_type && name == Tcl_GetString(selector));
      } else {
        const auto matches = findObjects(interpreter(), database, {Tcl_GetString(selector)}, type);
        selected = selected || name == Tcl_GetString(selector) || std::find(matches.begin(), matches.end(), name) != matches.end();
      }
    }
    if (selected == intersect)
      result.push_back(object);
  }
  // Tcl_NewListObj retains each original element, including its object type.
  // Input arguments remain alive until execute returns; the interpreter owns
  // the new result list afterwards.
  Tcl_SetObjResult(interpreter(), Tcl_NewListObj(static_cast<int>(result.size()), result.data()));
  return 1;
}

}  // namespace ista::sdc
