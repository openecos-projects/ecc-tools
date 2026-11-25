// SPDX-License-Identifier: MulanPSL-2.0
#include "object/ObjectQuery.hpp"

namespace ipw::sdc {

std::vector<std::string> findClockSources(Database& database, const std::vector<Tcl_Obj*>& objects)
{
  std::set<std::string> sources;
  for (auto* object : objects) {
    const auto type = SdcObject::getType(object);
    const std::string name = Tcl_GetString(object);
    if (type == SdcObjectType::kNone) {
      const auto matches = findClockSources(database, std::vector<std::string>{name});
      sources.insert(matches.begin(), matches.end());
    } else if (type == SdcObjectType::kPort || type == SdcObjectType::kPin) {
      // Query results carry canonical identity, not another name/glob pattern.
      const auto pin = database.get_pin_map().find(name);
      if (pin == database.get_pin_map().end() || pin->second.get_is_port() != (type == SdcObjectType::kPort))
        throw std::invalid_argument("clock source pin or port no longer exists: " + name);
      sources.insert(name);
    } else if (type == SdcObjectType::kNet) {
      const auto net = database.get_net_map().find(name);
      if (net == database.get_net_map().end())
        throw std::invalid_argument("clock source net no longer exists: " + name);
      sources.insert(net->second.get_driver_pin_list().begin(), net->second.get_driver_pin_list().end());
    } else {
      throw std::invalid_argument("clock sources must be ports, pins, or nets: " + name);
    }
  }
  return {sources.begin(), sources.end()};
}

}  // namespace ipw::sdc
