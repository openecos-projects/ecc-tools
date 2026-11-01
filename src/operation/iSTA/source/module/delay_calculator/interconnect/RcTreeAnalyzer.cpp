// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "RcTreeAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ista {

bool RcTreeAnalyzer::hasDisabledDriver(const std::string& name, const std::string& source) const
{
  const auto found = _database.network().nets().find(name);
  if (found == _database.network().nets().end())
    return false;
  const auto& drivers = found->second.get_driver_pin_list();
  return drivers.size() > 1
         && std::any_of(drivers.begin(), drivers.end(), [&](const auto& pin) { return pin != source && _database.graph().simValues().isDriverDisabled(pin); });
}

double RcTreeAnalyzer::balancedDelay(ParasiticNet& net, const std::string& source, std::string& sink, AnalysisType analysis, TransType edge)
{
  const auto found = _database.network().nets().find(net.get_net_name());
  if (found == _database.network().nets().end())
    return 0.;
  const auto& loads = found->second.get_load_pin_list();
  const auto count = std::count_if(loads.begin(), loads.end(), [&](const auto& pin) { return pin != source; });
  if (count == 0)
    return 0.;
  double wire_cap = 0.;
  for (const auto& [name, node] : net.get_node_map())
    wire_cap += node.get_capacitance();
  if (net.get_node_map().empty())
    wire_cap = net.get_lumped_capacitance();
  return totalResistance(net) * 1e-3 / count * (_pin_capacitance.pinCapacitance(sink, analysis, edge) + wire_cap / count);
}

double RcTreeAnalyzer::totalResistance(ParasiticNet& parasitic_net)
{
  double resistance = 0.0;
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    resistance += parasitic_resistor.get_resistance();
  }
  return resistance;
}

double RcTreeAnalyzer::nodeCapacitance(ParasiticNet& parasitic_net, std::string& pin_name)
{
  std::string spef_pin_name = pin_name;
  std::replace(spef_pin_name.begin(), spef_pin_name.end(), ':', '/');
  if (parasitic_net.get_node_map().count(spef_pin_name) > 0) {
    return parasitic_net.get_node_map()[spef_pin_name].get_capacitance();
  }
  if (parasitic_net.get_node_map().count(pin_name) > 0) {
    return parasitic_net.get_node_map()[pin_name].get_capacitance();
  }
  return parasitic_net.get_lumped_capacitance();
}

double RcTreeAnalyzer::nodeLoad(ParasiticNet& parasitic_net, std::string& node_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  double node_load = 0.0;
  if (parasitic_net.get_node_map().count(node_name) > 0) {
    node_load += parasitic_net.get_node_map()[node_name].get_capacitance();
  }

  std::string pin_name = pinName(node_name);
  if (database.network().pins().count(pin_name) > 0) {
    node_load += _pin_capacitance.pinCapacitance(pin_name, analysis_type, trans_type);
  }
  return node_load;
}

const RcTreeAnalyzer::RcTiming* RcTreeAnalyzer::timing(const std::string& net, AnalysisType analysis, TransType trans) const
{
  const auto n = _timing.find(net);
  if (n == _timing.end())
    return nullptr;
  const auto a = n->second.find(analysis);
  if (a == n->second.end())
    return nullptr;
  const auto t = a->second.find(trans);
  return t == a->second.end() ? nullptr : &t->second;
}

double RcTreeAnalyzer::load(const std::string& net, AnalysisType analysis, TransType trans, const std::string& node) const
{
  const auto* values = timing(net, analysis, trans);
  if (values) {
    const auto found = values->loads.find(node);
    if (found != values->loads.end())
      return found->second;
  }
  return 0.;
}

std::optional<double> RcTreeAnalyzer::delay(const std::string& net, AnalysisType analysis, TransType trans, const std::string& node) const
{
  const auto* values = timing(net, analysis, trans);
  if (values) {
    const auto found = values->delays.find(node);
    if (found != values->delays.end())
      return found->second;
  }
  return std::nullopt;
}

std::optional<double> RcTreeAnalyzer::impulse(const std::string& net, AnalysisType analysis, TransType trans, const std::string& node) const
{
  const auto* values = timing(net, analysis, trans);
  if (values) {
    const auto found = values->impulses.find(node);
    if (found != values->impulses.end())
      return found->second;
  }
  return std::nullopt;
}

void RcTreeAnalyzer::analyze(ParasiticNet& net, std::string& root, AnalysisType analysis, TransType trans)
{
  auto& values = _timing[net.get_net_name()][analysis][trans];
  if (values.root == root && !root.empty())
    return;
  values = RcTiming{};
  auto& tree = rootedTree(net, root);
  const auto count = tree.nodes.size();
  std::vector<double> capacitance;
  capacitance.reserve(count);
  for (auto& node : tree.nodes)
    capacitance.push_back(nodeLoad(net, node, analysis, trans));
  auto loads = capacitance;
  auto driver = pinName(root);
  if (_database.network().pins().contains(driver)) {
    loads[0] -= _pin_capacitance.pinCapacitance(driver, analysis, trans);
    loads[0] += _pin_capacitance.driverCapacitance(driver, analysis, trans);
  }
  for (std::size_t node = count; node-- > 0;)
    for (const auto child : tree.children[node])
      loads[node] += loads[child];

  std::vector<double> delays(count, 0.);
  for (std::size_t node = 1; node < count; ++node)
    delays[node] = delays[tree.parents[node]] + tree.resistances[node] * loads[node] * 1E-3;

  std::vector<double> load_delays(count, 0.);
  for (std::size_t node = count; node-- > 0;) {
    load_delays[node] = capacitance[node] * delays[node];
    for (const auto child : tree.children[node])
      load_delays[node] += load_delays[child];
  }
  std::vector<double> beta(count, 0.);
  for (std::size_t node = 0; node < count; ++node) {
    if (node != 0)
      beta[node] = beta[tree.parents[node]] + tree.resistances[node] * load_delays[node] * 1E-3;
    values.loads.emplace(tree.nodes[node], loads[node]);
    values.delays.emplace(tree.nodes[node], delays[node]);
    values.impulses.emplace(tree.nodes[node], std::max(0., 2. * beta[node] - std::pow(delays[node], 2)));
  }
  values.root = root;
}

double RcTreeAnalyzer::totalLoad(ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type)
{
  double total_load = 0.0;
  for (std::pair<const std::string, ParasiticNode>& node_pair : parasitic_net.get_node_map()) {
    std::string node_name = node_pair.first;
    total_load += nodeLoad(parasitic_net, node_name, analysis_type, trans_type);
  }
  return total_load;
}

void RcTreeAnalyzer::buildParasiticResistorMap(ParasiticNet& parasitic_net, std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map)
{
  resistor_map.clear();
  for (ParasiticResistor& parasitic_resistor : parasitic_net.get_resistor_list()) {
    resistor_map[parasitic_resistor.get_source_node()].push_back(std::make_pair(parasitic_resistor.get_sink_node(), parasitic_resistor.get_resistance()));
    resistor_map[parasitic_resistor.get_sink_node()].push_back(std::make_pair(parasitic_resistor.get_source_node(), parasitic_resistor.get_resistance()));
  }
}

std::string RcTreeAnalyzer::nodeName(ParasiticNet& parasitic_net, const std::string& pin_name)
{
  if (parasitic_net.get_node_map().count(pin_name) > 0) {
    return pin_name;
  }

  std::string spef_pin_name = pin_name;
  std::replace(spef_pin_name.begin(), spef_pin_name.end(), ':', '/');
  if (parasitic_net.get_node_map().count(spef_pin_name) > 0) {
    return spef_pin_name;
  }
  return "";
}

std::string RcTreeAnalyzer::pinName(std::string& node_name)
{
  Database& database = _database;
  if (database.network().pins().count(node_name) > 0) {
    return node_name;
  }

  std::string pin_name = node_name;
  // Only the instance/pin separator changes; hierarchy and escaped slashes
  // remain part of their names. Try each split against actual connectivity.
  for (size_t slash = pin_name.rfind('/'); slash != std::string::npos; slash = slash == 0 ? std::string::npos : pin_name.rfind('/', slash - 1)) {
    pin_name[slash] = ':';
    if (database.network().pins().contains(pin_name))
      return pin_name;
    pin_name[slash] = '/';
  }
  return node_name;
}

RcTreeAnalyzer::RcTree& RcTreeAnalyzer::rootedTree(ParasiticNet& net, const std::string& root)
{
  auto& tree = _trees[net.get_net_name()];
  if (tree.root == root && !tree.nodes.empty())
    return tree;
  if (!_parasitic_resistor_map_cache.contains(net.get_net_name()))
    buildParasiticResistorMap(net, _parasitic_resistor_map_cache[net.get_net_name()]);
  const auto& resistors = _parasitic_resistor_map_cache.at(net.get_net_name());
  tree = RcTree{};
  tree.nodes.push_back(root);
  tree.parents.push_back(-1);
  tree.resistances.push_back(0.);
  tree.children.emplace_back();
  std::set<std::string> visited{root};
  std::vector<std::pair<std::size_t, std::size_t>> stack{{0, 0}};
  while (!stack.empty()) {
    auto& [node, edge_index] = stack.back();
    const auto edges = resistors.find(tree.nodes[node]);
    if (edges == resistors.end() || edge_index == edges->second.size()) {
      stack.pop_back();
      continue;
    }
    const auto& [next, resistance] = edges->second[edge_index++];
    if (!visited.insert(next).second)
      continue;
    const auto child = tree.nodes.size();
    tree.children[node].push_back(child);
    tree.nodes.push_back(next);
    tree.parents.push_back(static_cast<int32_t>(node));
    tree.resistances.push_back(resistance);
    tree.children.emplace_back();
    stack.emplace_back(child, 0);
  }
  tree.root = root;
  return tree;
}

void RcTreeAnalyzer::buildTree(ParasiticNet& net, std::string& root, AnalysisType analysis, TransType trans, std::vector<std::string>& names,
                               std::vector<int32_t>& parents, std::vector<double>& resistances, std::vector<double>& capacitances)
{
  auto& tree = rootedTree(net, root);
  names.insert(names.end(), tree.nodes.begin(), tree.nodes.end());
  parents.insert(parents.end(), tree.parents.begin(), tree.parents.end());
  for (const auto resistance : tree.resistances)
    resistances.push_back(resistance * 1E-3);
  // Arnoldi's source capacitance retains the receiver capacitance of an inout
  // driver; preserve its original root formula separately from subtree loads.
  capacitances.push_back(net.get_node_map()[root].get_capacitance() + _pin_capacitance.driverCapacitance(pinName(root), analysis, trans));
  for (std::size_t node = 1; node < tree.nodes.size(); ++node)
    capacitances.push_back(nodeLoad(net, tree.nodes[node], analysis, trans));
}

}  // namespace ista
