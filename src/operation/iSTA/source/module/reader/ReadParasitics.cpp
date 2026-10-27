// SPDX-License-Identifier: MulanPSL-2.0
#include "ReadParasitics.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

#include "idm.h"
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
  ParasiticNet parasitic_net;
  parasitic_net.set_net_name(spef_net.name);
  parasitic_net.set_lumped_capacitance(readCapacitance(spef_net.lcap));
  for (spef::ConnEntry& spef_conn : spef_net.conns) {
    readConnection(parasitic_net, spef_conn);
  }
  for (spef::ResCap& spef_res : spef_net.ress) {
    readResistance(parasitic_net, spef_res);
  }
  for (spef::ResCap& spef_cap : spef_net.caps) {
    readCapacitance(parasitic_net, spef_cap);
  }
  // The default O-2018.06-SP1 model normalizes extracted nodes to at least 1 aF.
  // Apply this in pF after duplicate CAP entries have been summed, including
  // nodes present only in CONN/RES. Liberty pin capacitance is added later.
  constexpr double minimum_node_capacitance = 1e-6;
  for (auto& [name, node] : parasitic_net.get_node_map()) {
    node.set_capacitance(std::max(node.get_capacitance(), minimum_node_capacitance));
  }
  _parasitics.nets()[parasitic_net.get_net_name()] = parasitic_net;
}

void ReadParasitics::readConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn)
{
  ParasiticNode& parasitic_node = ensureNode(parasitic_net, spef_conn.pin_port_name);
  parasitic_node.set_x(spef_conn.coordinate.x);
  parasitic_node.set_y(spef_conn.coordinate.y);
}

void ReadParasitics::readCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap)
{
  double capacitance = readCapacitance(spef_cap.res_or_cap);
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
  return spef_resistance * unitScale(spef_unit, target_unit);
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
