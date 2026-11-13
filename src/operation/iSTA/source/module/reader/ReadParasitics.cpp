// SPDX-License-Identifier: MulanPSL-2.0
#include "ReadParasitics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include "Logger.hpp"
#include "idm.h"
#include "network/Network.hpp"
#include "parasitics/Parasitics.hpp"
#include "spef/SpefParser.hh"

namespace ista {

void ReadParasitics::read()
{
  _parasitics.set_spef_file_path(dmInst->get_config().get_spef_path());
  _parasitics.nets().clear();
  spef::SpefReader* spef_reader = dmInst->get_spef_reader();
  if (spef_reader == nullptr || spef_reader->getSpefFile() == nullptr) {
    return;
  }

  _parasitics.set_capacitive_unit(spef_reader->getSpefCapUnit());
  _parasitics.set_resistance_unit(spef_reader->getSpefResUnit());

  spef::Exchange* spef_file = spef_reader->getSpefFile();
  for (spef::Net& spef_net : spef_file->nets) {
    readNet(spef_net);
  }
}

void ReadParasitics::readNet(spef::Net& spef_net)
{
  std::string net_name = spef_net.name;
  if (!_network.nets().contains(net_name)) {
    // A top-level assign may be linked under its internal wire name while
    // extraction names the same connection after the port. Resolve only an
    // actual port alias, then validate every extracted terminal below.
    const auto port = _network.pins().find(net_name);
    if (port != _network.pins().end() && port->second.get_is_port())
      net_name = port->second.get_net_name();
  }
  // An ECO can leave the old extracted terminals in SPEF. Those nodes must
  // not replace the loads of the linked netlist or become a fictitious RC tree.
  if (!connectionsMatch(spef_net, net_name))
    return;
  ParasiticNet parasitic_net;
  parasitic_net.set_net_name(net_name);
  parasitic_net.set_lumped_capacitance(readCapacitance(spef_net.lcap));
  for (spef::ConnEntry& spef_conn : spef_net.conns) {
    readConnection(parasitic_net, spef_conn);
  }
  for (spef::ResCap& spef_res : spef_net.ress) {
    readResistance(parasitic_net, spef_res);
  }
  // REF2022 reverse-engineered on ip2 `_07829_` (`_11805_`/MUX4X1P4H7R driving
  // `_11807_`/AOI22X0P5H7R): the driver cell's own output-to-input coupling
  // (`Y | S1` = 0.062109 fF) is excluded from the wire capacitance. The liberty
  // cell tables already contain that intra-cell effect, so counting it again
  // inflates the load. The residual after excluding only this pair is 0.000308 fF
  // against REF's `report_delay_calculation`. ISTA_WIRE_CAP_EXCL_DRIVER_CC=0
  // restores the previous behaviour.
  // Excluding the driver's own output-to-input coupling closes ip2 `_11805_` to
  // 0.00031 fF against reference, but it was refuted at suite level: gcd 88.1% ->
  // 54.1%, ip2 60.0% -> 48.7%, ws2811 91.3% -> 58.4%, s713 90.3% -> 55.0%. ip2 has
  // 1434 such pairs totalling 77.3 fF (max 1.11 fF) while `_11805_` needs only
  // 0.062 fF, so the rule is not general. Keep it opt-in for further probing.
  static const bool excl_driver_cc = [] {
    const char* e = std::getenv("ISTA_WIRE_CAP_EXCL_DRIVER_CC");
    return e != nullptr && *e == '1';
  }();
  std::string driver_instance;
  std::string driver_pin;
  if (excl_driver_cc) {
    const auto net_it = _network.nets().find(net_name);
    if (net_it != _network.nets().end()) {
      for (const auto& pin_name : net_it->second.get_pin_name_list()) {
        const auto pin_it = _network.pins().find(pin_name);
        if (pin_it == _network.pins().end())
          continue;
        const auto direction = pin_it->second.get_direction();
        if (direction == PinDirection::kOutput || direction == PinDirection::kInout) {
          driver_instance = pin_it->second.get_instance_name();
          // SPEF node names use `instance:pin`; build the matching form.
          driver_pin = driver_instance + ":" + pin_it->second.get_pin_name();
          break;
        }
      }
    }
  }
  {
    static const char* dp = std::getenv("ISTA_DUMP_DRIVER_PIN");
    if (dp != nullptr && excl_driver_cc) {
      if (FILE* f = std::fopen(dp, "a"); f != nullptr) {
        std::fprintf(f, "NET,%s,inst=%s,pin=%s\n", net_name.c_str(), driver_instance.c_str(), driver_pin.c_str());
        std::fclose(f);
      }
    }
  }
  for (spef::ResCap& spef_cap : spef_net.caps) {
    readCapacitance(parasitic_net, spef_cap, driver_instance, driver_pin);
  }
  // The U-2022.12 model normalizes extracted nodes to at least 1 zF.
  // Apply this in pF after duplicate CAP entries have been summed, including
  // nodes present only in CONN/RES. Liberty pin capacitance is added later.
  constexpr double minimum_node_capacitance = 1e-9;
  for (auto& [name, node] : parasitic_net.get_node_map()) {
    node.set_capacitance(std::max(node.get_capacitance(), minimum_node_capacitance));
  }
  _parasitics.nets()[parasitic_net.get_net_name()] = parasitic_net;
}

bool ReadParasitics::connectionsMatch(const spef::Net& net, const std::string& net_name) const
{
  const auto found = _network.nets().find(net_name);
  if (found == _network.nets().end()) {
    STALOG.warn(Loc::current(), "Ignoring SPEF net absent from the timing network: ", net.name);
    return false;
  }
  const auto& pins = found->second.get_pin_name_list();
  const std::unordered_set<std::string_view> pin_names(pins.begin(), pins.end());
  for (const auto& connection : net.conns) {
    if (connection.conn_direction == spef::ConnectionDirection::kInternal)
      continue;  // *N entries describe wire geometry, not netlist terminals.
    const auto matches = [&](const std::string& name) {
      if (pin_names.contains(name))
        return true;
      const auto boundary = _network.hierarchyPins().find(name);
      return boundary != _network.hierarchyPins().end() && boundary->second.error.empty() && boundary->second.net == net_name;
    };
    std::string name = connection.pin_port_name;
    bool matched = matches(name);
    // Only replace an instance/pin separator, preserving hierarchy separators.
    for (auto slash = name.rfind('/'); !matched && slash != std::string::npos; slash = slash == 0 ? std::string::npos : name.rfind('/', slash - 1)) {
      name[slash] = ':';
      matched = matches(name);
      name[slash] = '/';
    }
    if (!matched) {
      STALOG.warn(Loc::current(), "Ignoring stale SPEF net ", net.name, ": terminal ", connection.pin_port_name,
                  " is not connected in the timing network; retaining netlist pin/port loads");
      return false;
    }
  }
  return true;
}

void ReadParasitics::readConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn)
{
  ParasiticNode& parasitic_node = ensureNode(parasitic_net, spef_conn.pin_port_name);
  parasitic_node.set_x(spef_conn.coordinate.x);
  parasitic_node.set_y(spef_conn.coordinate.y);
}

void ReadParasitics::readCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap, const std::string& driver_instance,
                                    const std::string& driver_pin)
{
  double capacitance = readCapacitance(spef_cap.res_or_cap);
  // Skip a coupling whose endpoints both belong to the driving cell: it is an
  // intra-cell effect already modelled by the liberty tables.
  if (!driver_instance.empty() && !spef_cap.node2.empty()) {
    const auto instanceOf = [](const std::string& node) -> std::string {
      const auto colon = node.rfind(':');
      return colon == std::string::npos ? std::string() : node.substr(0, colon);
    };
    // Only the driver's own output-to-input coupling is an intra-cell effect.
    // Receiver input-to-input coupling stays in the wire capacitance.
    static const char* dc = std::getenv("ISTA_DUMP_DRIVER_CC");
    const bool both_driver = instanceOf(spef_cap.node1) == driver_instance && instanceOf(spef_cap.node2) == driver_instance;
    if (dc != nullptr && both_driver) {
      if (FILE* f = std::fopen(dc, "a"); f != nullptr) {
        std::fprintf(f, "CC,%s|%s,%.9f,drv=%s\n", spef_cap.node1.c_str(), spef_cap.node2.c_str(), capacitance, driver_pin.c_str());
        std::fclose(f);
      }
    }
    if (both_driver && (spef_cap.node1 == driver_pin || spef_cap.node2 == driver_pin))
      return;
  }
  ParasiticNode& parasitic_node = ensureNode(parasitic_net, spef_cap.node1);
  parasitic_node.set_capacitance(parasitic_node.get_capacitance() + capacitance);

  if (spef_cap.node2.empty()) {
    return;
  }
  if (parasitic_net.get_node_map().count(spef_cap.node2) > 0) {
    ParasiticNode& coupled_node = parasitic_net.get_node_map()[spef_cap.node2];
    coupled_node.set_capacitance(coupled_node.get_capacitance() + capacitance);
  }
}

void ReadParasitics::readResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res)
{
  ParasiticResistor parasitic_resistor;
  parasitic_resistor.set_source_node(spef_res.node1);
  parasitic_resistor.set_sink_node(spef_res.node2);
  parasitic_resistor.set_resistance(readResistance(spef_res.res_or_cap));
  parasitic_net.get_resistor_list().push_back(parasitic_resistor);
  ensureNode(parasitic_net, spef_res.node1);
  ensureNode(parasitic_net, spef_res.node2);
}

double ReadParasitics::readCapacitance(double spef_capacitance)
{
  std::string spef_unit = _parasitics.get_capacitive_unit();
  std::string target_unit = "PF";
  return spef_capacitance * unitScale(spef_unit, target_unit);
}

double ReadParasitics::readResistance(double spef_resistance)
{
  std::string spef_unit = _parasitics.get_resistance_unit();
  std::string target_unit = "OHM";
  // Normalize in physical ohms, independently of the SPEF R_UNIT. The
  // U-2022.12 minimum also affects the total-RC choice of interconnect model.
  constexpr double minimum_resistance = .01;
  return std::max(spef_resistance * unitScale(spef_unit, target_unit), minimum_resistance);
}

double ReadParasitics::unitScale(std::string& spef_unit, std::string& target_unit)
{
  double unit_value = 1.0;
  std::string unit_name;
  std::stringstream spef_unit_stream(spef_unit);
  spef_unit_stream >> unit_value >> unit_name;
  std::transform(unit_name.begin(), unit_name.end(), unit_name.begin(), ::toupper);
  std::transform(target_unit.begin(), target_unit.end(), target_unit.begin(), ::toupper);

  if (unit_name == target_unit) {
    return unit_value;
  }
  if (unit_name == "FF" && target_unit == "PF") {
    return unit_value * 1E-3;
  }
  if (unit_name == "PF" && target_unit == "FF") {
    return unit_value * 1E3;
  }
  if (unit_name == "F" && target_unit == "PF") {
    return unit_value * 1E12;
  }
  if (unit_name == "PF" && target_unit == "F") {
    return unit_value * 1E-12;
  }
  if (unit_name == "KOHM" && target_unit == "OHM") {
    return unit_value * 1E3;
  }
  if (unit_name == "OHM" && target_unit == "KOHM") {
    return unit_value * 1E-3;
  }
  return unit_value;
}

ParasiticNode& ReadParasitics::ensureNode(ParasiticNet& parasitic_net, const std::string& node_name)
{
  ParasiticNode& parasitic_node = parasitic_net.get_node_map()[node_name];
  parasitic_node.set_node_name(node_name);
  return parasitic_node;
}

}  // namespace ista
