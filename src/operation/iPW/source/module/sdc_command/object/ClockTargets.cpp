#include "object/ObjectQuery.hpp"
#include "sdc_data_manager/SdcObject.hpp"

namespace ipw::sdc {
std::vector<std::pair<QueryObjectType, std::string>> resolveClockTargets(
    Database& database,
    const std::vector<Tcl_Obj*>& objects,
    bool clocks_only
)
{
  std::set<std::pair<QueryObjectType, std::string>> result;
  for (auto* object : objects) {
    const auto type = SdcObject::getType(object);
    const std::string name = Tcl_GetString(object);
    if (type == SdcObjectType::kClock) {
      if (!database.get_timing_constraint().get_clock_map().contains(name))
        throw std::invalid_argument("clock no longer exists: " + name);
      result.emplace(QueryObjectType::kClock, name);
      continue;
    }
    if (type == SdcObjectType::kNone) {
      const auto clocks = findObjects(database, {name}, QueryObjectType::kClock);
      for (const auto& clock : clocks)
        result.emplace(QueryObjectType::kClock, clock);
      if (!clocks.empty())
        continue;
    }
    if (clocks_only)
      throw std::invalid_argument("constraint requires clock objects: " + name);
    if (type != SdcObjectType::kNone && type != SdcObjectType::kPin && type != SdcObjectType::kPort)
      throw std::invalid_argument("constraint requires clock, pin or port objects: " + name);
    bool found = false;
    ObjectQueryOptions options;
    options.exact = type != SdcObjectType::kNone;
    for (auto query : {QueryObjectType::kPin, QueryObjectType::kPort}) {
      if ((type == SdcObjectType::kPort && query != QueryObjectType::kPort)
          || (type == SdcObjectType::kPin && query != QueryObjectType::kPin))
        continue;
      for (const auto& pin : findObjects(database, {name}, query, options)) {
        result.emplace(query, pin);
        found = true;
      }
    }
    if (!found)
      throw std::invalid_argument("clock, pin or port not found: " + name);
  }
  if (result.empty())
    throw std::invalid_argument("constraint requires a non-empty clock, pin or port collection");
  return {result.begin(), result.end()};
}
std::set<std::string> findClocks(Database& database, const std::vector<Tcl_Obj*>& objects)
{
  std::set<std::string> result;
  for (const auto& [type, name] : resolveClockTargets(database, objects, true))
    result.insert(name);
  return result;
}
}  // namespace ipw::sdc
