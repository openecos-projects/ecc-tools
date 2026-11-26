// SPDX-License-Identifier: MulanPSL-2.0
#include "LibertySupplies.hpp"

#include <cmath>
#include <stdexcept>

#include "Lib.hh"
#include "TimingCell.hpp"

namespace ipw {

void wrapLibertySupplies(TimingCell& cell, idb::LibCell& raw_cell)
{
  auto& library = *raw_cell.get_owner_lib();
  const std::string context = "Liberty supplies for " + std::string(raw_cell.get_cell_name()) + ": ";
  std::string unit = library.get_voltage_unit_name().value_or("1V");
  std::size_t suffix = 0;
  double scale = std::stod(unit, &suffix);
  if (unit.substr(suffix) == "mV") {
    scale *= 1E-3;
  } else if (unit.substr(suffix) != "V") {
    throw std::runtime_error(context + "unsupported voltage_unit " + unit);
  }
  if (!std::isfinite(scale) || scale <= 0.0) {
    throw std::runtime_error(context + "invalid voltage_unit " + unit);
  }
  cell.set_nom_voltage(library.get_nom_voltage() * scale);
  auto& pins = raw_cell.get_supply_pins();
  std::set<std::string> power_pins;
  std::set<std::string> ground_pins;
  for (const auto& [name, pin] : pins) {
    if (pin.pg_type.ends_with("_power") || pin.pg_type == "nwell") {
      power_pins.insert(name);
    } else if (pin.pg_type.ends_with("_ground")) {
      ground_pins.insert(name);
    }
  }
  if (pins.empty()) {
    return;  // Legacy single-supply libraries and extracted macro models.
  }
  std::map<std::string, std::set<std::string>> returns;
  auto add_port = [&](idb::LibPort& port) {
    const auto& power = port.get_related_power_pin();
    const auto& ground = port.get_related_ground_pin();
    if (!power.empty() && !power_pins.contains(power)) {
      throw std::runtime_error(context + "unknown related_power_pin " + power);
    }
    if (!ground.empty() && !ground_pins.contains(ground)) {
      throw std::runtime_error(context + "unknown related_ground_pin " + ground);
    }
    if (!power.empty() && !ground.empty()) {
      returns[power].insert(ground);
    }
  };
  for (auto& port : raw_cell.get_cell_ports()) {
    add_port(*port);
  }
  for (auto& bus : raw_cell.get_cell_buses()) {
    for (auto& port : bus->get_ports()) {
      add_port(*port);
    }
  }
  for (const auto& name : power_pins) {
    auto voltage = library.get_voltage_map().find(pins.at(name).voltage_name);
    double volts = cell.get_nom_voltage();
    if (voltage != library.get_voltage_map().end()) {
      volts = voltage->second * scale;
    } else if (power_pins.size() != 1 || !library.get_voltage_map().empty()) {
      throw std::runtime_error(context + "missing voltage_map for " + name);
    }
    if (!std::isfinite(volts) || volts <= 0.0) {
      throw std::runtime_error(context + "invalid supply voltage for " + name);
    }
    auto& candidates = returns[name];
    if (candidates.empty() && ground_pins.size() == 1) {
      candidates = ground_pins;
    }
    // Power-only pads may have no signal pins declaring a return rail. Preserve
    // that absence; the EMIR consumer requires an explicit return before use.
    cell.get_power_supplies()[name] = {volts, candidates.size() == 1 ? *candidates.begin() : ""};
  }
}

}  // namespace ipw
