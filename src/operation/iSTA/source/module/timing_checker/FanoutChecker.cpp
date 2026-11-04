// SPDX-License-Identifier: MulanPSL-2.0
#include "FanoutChecker.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "graph/SimValues.hpp"
#include "graph/TimingStateQuery.hpp"
#include "liberty/LibertyPort.hpp"
#include "network/Net.hpp"
#include "network/Pin.hpp"
#include "sdc/Sdc.hpp"

namespace ista {
std::vector<FanoutCheck> FanoutChecker::check() const
{
  const auto& _network = _database.network();
  const auto& _liberty = _database.liberty();
  const auto& _sdc = _database.sdc();
  const auto& _sim_values = _database.graph().simValues();
  std::vector<FanoutCheck> checks;
  for (const auto& [net_name, net] : _network.nets()) {
    double fanout_load = 0.0;
    const std::set<std::string> loads(net.get_load_pin_list().begin(), net.get_load_pin_list().end());
    for (const std::string& load : loads) {
      const Pin& pin = _network.pins().at(load);
      // External port fanout defaults to zero. Capacitance is a separate quantity.
      if (!pin.get_is_port()) {
        const LibertyPort* port = _network.libertyPort(_liberty, pin);
        fanout_load += port == nullptr ? 1.0 : port->get_fanout_load();
      }
    }
    std::set<std::string> drivers(net.get_driver_pin_list().begin(), net.get_driver_pin_list().end());
    if (!net.get_driver_pin().empty()) {
      drivers.insert(net.get_driver_pin());
    }
    for (const std::string& driver : drivers) {
      const Pin& pin = _network.pins().at(driver);
      if (_sim_values.isDriverDisabled(driver) || isIdealClockPin(_database, driver)) {
        continue;
      }
      double driver_fanout_load = fanout_load;
      if (!pin.get_is_port() && loads.contains(driver)) {
        const LibertyPort* port = _network.libertyPort(_liberty, pin);
        driver_fanout_load -= port == nullptr ? 1.0 : port->get_fanout_load();
      }
      const auto limit = _sdc.fanoutLimit(_network, _liberty, driver);
      if (!limit) {
        continue;
      }
      FanoutCheck check;
      check.set_net_name(net_name);
      check.set_driver_pin(driver);
      check.set_fanout_load(driver_fanout_load);
      check.set_limit(*limit);
      checks.push_back(check);
    }
  }
  std::sort(checks.begin(), checks.end(), [](const FanoutCheck& lhs, const FanoutCheck& rhs) {
    return std::make_tuple(lhs.get_slack(), lhs.get_driver_pin(), lhs.get_net_name())
           < std::make_tuple(rhs.get_slack(), rhs.get_driver_pin(), rhs.get_net_name());
  });
  return checks;
}

}  // namespace ista
