// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <unordered_map>
#include <unordered_set>

#include "IdbDesign.h"

namespace idb {
std::size_t IdbDesign::canonicalizeNetNames()
{
  std::unordered_map<std::string, IdbNet*> port_nets;
  std::unordered_set<std::string> instance_names, used;
  for (auto* pin : _io_pin_list->get_pin_list()) {
    port_nets.emplace(pin->get_pin_name(), pin->get_net());
    used.insert(pin->get_pin_name());
  }
  for (auto* instance : _instance_list->get_instance_list()) {
    instance_names.insert(instance->get_name());
    used.insert(instance->get_name());
  }
  for (auto* net : _net_list->get_net_list())
    used.insert(net->get_net_name());

  std::size_t renamed = 0, next = 0;
  for (auto* net : _net_list->get_net_list()) {
    const auto& name = net->get_net_name();
    const auto port = port_nets.find(name);
    if ((port == port_nets.end() || port->second == net) && !instance_names.contains(name))
      continue;
    std::string candidate;
    do {
      candidate = "__ecc_net_" + std::to_string(next++);
    } while (!used.insert(candidate).second);
    // renameNet updates the lookup index and every connected pin's cached net name.
    renameNet(net, candidate);
    ++renamed;
  }

  // A scalar inout needs the same internal and external symbol: a directional
  // assign cannot represent its bidirectional connection. Conflicting nets have
  // already moved out of the port namespace above.
  std::unordered_set<IdbNet*> inouts;
  for (auto* pin : _io_pin_list->get_pin_list()) {
    auto* net = pin->get_net();
    if (!net || !pin->get_term() || pin->get_term()->get_direction() != IdbConnectDirection::kInOut || !inouts.insert(net).second)
      continue;
    if (net->get_net_name() != pin->get_pin_name() && !instance_names.contains(pin->get_pin_name())) {
      renameNet(net, pin->get_pin_name());
      ++renamed;
    }
  }
  return renamed;
}
}  // namespace idb
