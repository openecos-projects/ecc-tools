// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Database.hpp"
namespace ista::sdc {
enum class QueryObjectType
{
  kPort,
  kPin,
  kCell,
  kNet,
  kClock,
  kLibrary,
  kLibCell,
  kLibPin,
  kAny
};
struct ObjectMatch
{
  std::string object_name;
  std::vector<std::string> names;
  std::vector<std::string> path;  // Elaborated components; literal slashes remain inside a component.
  std::map<std::string, std::string> attributes;
};
std::vector<ObjectMatch> collectObjectMatches(Database& database, QueryObjectType type);
QueryObjectType getObjectType(Database& database, const std::string& name);
// Input names are already resolved; this layer does not interpret Tcl patterns.
std::set<std::string> collectRelatedObjects(Database& database, const std::vector<std::string>& object_names, QueryObjectType target,
                                            QueryObjectType source_type = QueryObjectType::kAny, bool leaf = false);
}  // namespace ista::sdc
