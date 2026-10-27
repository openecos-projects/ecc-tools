// SPDX-License-Identifier: MulanPSL-2.0
#include "ObjectQueryOptions.hpp"

#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "SdcTclCmd.hpp"
namespace ista::sdc {
void addObjectQueryOptions(SdcTclCmd& command, bool hierarchical, bool exact, bool of_objects)
{
  command.addOption(new ecc::TclSwitchOption("-quiet"));
  command.addOption(new ecc::TclSwitchOption("-regexp"));
  command.addOption(new ecc::TclSwitchOption("-nocase"));
  command.addOption(new ecc::TclStringOption("-filter", 0));
  if (hierarchical)
    command.addOption(new ecc::TclSwitchOption("-hierarchical"));
  if (exact)
    command.addOption(new ecc::TclSwitchOption("-exact"));
  if (of_objects)
    command.addOption(new ecc::TclStringListOption("-of_objects", 0));
}

ObjectQueryOptions getObjectQueryOptions(SdcTclCmd& command)
{
  ObjectQueryOptions options;
  for (const auto& [name, target] : std::initializer_list<std::pair<const char*, bool*>>{
           {"-regexp", &options.regexp}, {"-nocase", &options.nocase}, {"-exact", &options.exact}, {"-hierarchical", &options.hierarchical}}) {
    if (ecc::TclOption* option = command.getOptionOrArg(name); option != nullptr)
      *target = option->is_set_val();
  }
  if (ecc::TclOption* filter = command.getOptionOrArg("-filter"); filter != nullptr && filter->is_set_val())
    options.filter = filter->getStringVal();
  if (ecc::TclOption* objects = command.getOptionOrArg("-of_objects"); objects != nullptr && objects->is_set_val()) {
    options.has_of_objects = true;
    for (Tcl_Obj* object : command.getOptionObjects("-of_objects")) {
      const auto type = queryObjectType(object);
      if (type == QueryObjectType::kAny && SdcObject::getType(object) != SdcObjectType::kNone)
        throw std::invalid_argument("unsupported object type for -of_objects");
      options.of_objects.emplace_back(type, Tcl_GetString(object));
    }
  }
  return options;
}

std::optional<std::string> getObjectQueryError(const ObjectQueryOptions& options, bool has_patterns)
{
  if (options.regexp && options.exact)
    return "-regexp and -exact are mutually exclusive";
  if (options.nocase && !options.regexp)
    return "-nocase requires -regexp";
  if (has_patterns && options.has_of_objects)
    return "patterns and -of_objects are mutually exclusive";
  if (options.hierarchical && options.has_of_objects)
    return "-hierarchical and -of_objects are mutually exclusive";
  return std::nullopt;
}
}  // namespace ista::sdc
