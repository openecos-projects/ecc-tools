// SPDX-License-Identifier: MulanPSL-2.0
#include "SupplyPower.hpp"

#include <stdexcept>

namespace ipw {

std::string resolvePowerSupply(TimingCell& cell, const std::string& pg_pin, const std::string& signal_pin)
{
  const auto& supplies = cell.get_power_supplies();
  if (supplies.empty()) {
    return "";
  }
  std::string name = pg_pin;
  if (name.empty()) {
    auto port = cell.get_port_map().find(signal_pin);
    if (port != cell.get_port_map().end()) {
      name = port->second.get_related_power_pin();
    }
  }
  if (name.empty() && supplies.size() == 1) {
    return supplies.begin()->first;
  }
  if (!supplies.contains(name)) {
    throw std::runtime_error(
        "Missing or invalid power supply '" + name + "' for signal '" + signal_pin + "' in library "
        + cell.get_library_name()
    );
  }
  return name;
}

double supplyVoltage(TimingCell& cell, const std::string& pg_pin)
{
  return pg_pin.empty() ? cell.get_nom_voltage() : cell.get_power_supplies().at(pg_pin).voltage;
}

}  // namespace ipw
