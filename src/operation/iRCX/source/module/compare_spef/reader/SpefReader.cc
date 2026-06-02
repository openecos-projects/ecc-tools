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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "reader/SpefReader.hh"

#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "SpefParser.hh"

namespace ircx {
namespace compare_spef {
namespace {

auto directionName(spef::ConnectionDirection direction) -> std::string
{
  switch (direction) {
    case spef::ConnectionDirection::kInput:
      return "I";
    case spef::ConnectionDirection::kOutput:
      return "O";
    case spef::ConnectionDirection::kInout:
      return "B";
    case spef::ConnectionDirection::kInternal:
      return "N";
    case spef::ConnectionDirection::kUninitialized:
      break;
  }
  return "";
}

}  // namespace

class NameExpander
{
 public:
  explicit NameExpander(const spef::Exchange& exchange) : _exchange(exchange)
  {
    _expanded_base_names.reserve(exchange.index_to_name_map.size());
  }

  auto expand(const std::string& name) -> std::string
  {
    if (!startsWithNameIndex(name)) {
      return name;
    }

    const std::size_t begin = 1;
    const std::size_t colon = name.find(':', begin);
    const std::size_t index = static_cast<std::size_t>(std::strtoull(name.substr(begin, colon - begin).c_str(), nullptr, 10));
    const std::string* base_name = expandedBaseName(index);
    if (base_name == nullptr) {
      return name;
    }

    if (colon == std::string::npos) {
      return *base_name;
    }

    std::string expanded = *base_name;
    expanded.reserve(base_name->size() + name.size() - colon);
    expanded += name.substr(colon);
    return expanded;
  }

 private:
  static auto startsWithNameIndex(const std::string& name) -> bool
  {
    return name.size() >= 2 && name.front() == '*' && std::isdigit(static_cast<unsigned char>(name[1]));
  }

  auto expandedBaseName(std::size_t index) -> const std::string*
  {
    const auto cache_it = _expanded_base_names.find(index);
    if (cache_it != _expanded_base_names.end()) {
      return &cache_it->second;
    }

    const auto map_it = _exchange.index_to_name_map.find(index);
    if (map_it == _exchange.index_to_name_map.end()) {
      return nullptr;
    }

    auto [it, inserted] = _expanded_base_names.emplace(index, spef::removeEscapes(map_it->second));
    return &it->second;
  }

  const spef::Exchange& _exchange;
  std::unordered_map<std::size_t, std::string> _expanded_base_names;
};

void SpefReader::buildNetCouplingCaps(Data& data) const
{
  std::size_t coupling_count = 0;
  for (const auto& [net_name, net] : data.nets) {
    coupling_count += net.node_coupling_caps.size();
  }

  std::unordered_set<NodePair, NodePairHash> seen_node_pairs;
  seen_node_pairs.reserve(coupling_count);
  data.coupling_caps.clear();
  data.coupling_caps.reserve(coupling_count);

  for (const auto& [net_name, net] : data.nets) {
    for (const auto& [node_pair, capacitance] : net.node_coupling_caps) {
      if (!seen_node_pairs.insert(node_pair).second) {
        continue;
      }

      const auto net1 = data.index.resolveNodeNet(node_pair.first);
      const auto net2 = data.index.resolveNodeNet(node_pair.second);
      if (net1.empty() || net2.empty() || net1 == net2) {
        continue;
      }
      data.coupling_caps.add(NodePair::ordered(net1, net2), capacitance);
    }
  }

  data.coupling_caps.rebuildOrdered();
}

auto SpefReader::read(const std::string& path, Data& data) const -> bool
{
  spef::SpefReader reader;
  if (!reader.read(path)) {
    return false;
  }

  const auto* spef_file = reader.getSpefFile();
  if (spef_file == nullptr) {
    return false;
  }

  data = Data{};
  data.file_name = path;
  data.cap_unit = reader.getSpefCapUnit();
  data.res_unit = reader.getSpefResUnit();
  data.index.reserve(spef_file->nets.size());
  NameExpander name_expander(*spef_file);

  for (const auto& spef_net : spef_file->nets) {
    Net net;
    net.name = name_expander.expand(spef_net.name);
    net.total_cap = spef_net.lcap;
    net.pins.reserve(spef_net.conns.size());
    net.resistors.reserve(spef_net.ress.size());
    net.node_coupling_caps.reserve(spef_net.caps.size());

    for (const auto& conn : spef_net.conns) {
      Pin pin;
      pin.name = name_expander.expand(conn.pin_port_name);
      pin.direction = directionName(conn.conn_direction);
      pin.driving_cell = conn.driving_cell;
      pin.is_external = conn.conn_type == spef::ConnectionType::kExternal;
      pin.x = conn.coordinate.x;
      pin.y = conn.coordinate.y;
      pin.has_coordinate = conn.coordinate.x >= 0.0 && conn.coordinate.y >= 0.0;
      data.index.rememberNodeNet(pin.name, net.name);
      net.pins.push_back(std::move(pin));
    }

    for (const auto& cap : spef_net.caps) {
      std::string node1 = name_expander.expand(cap.node1);
      if (cap.node2.empty()) {
        data.index.rememberNodeNet(node1, net.name);
      } else {
        std::string node2 = name_expander.expand(cap.node2);
        data.index.rememberNodeNet(node1, net.name);
        net.node_coupling_caps[NodePair::ordered(std::move(node1), std::move(node2))] += cap.res_or_cap;
      }
    }

    for (const auto& res : spef_net.ress) {
      if (res.node1.empty() || res.node2.empty()) {
        continue;
      }
      std::string node1 = name_expander.expand(res.node1);
      std::string node2 = name_expander.expand(res.node2);
      data.index.rememberNodeNet(node1, net.name);
      data.index.rememberNodeNet(node2, net.name);
      net.resistors.push_back(Resistor{std::move(node1), std::move(node2), res.res_or_cap});
    }

    const std::string net_name = net.name;
    auto net_it = data.nets.insert_or_assign(net_name, std::move(net)).first;
    data.index.registerNet(net_it->first, net_it->second);
  }

  buildNetCouplingCaps(data);
  return true;
}

}  // namespace compare_spef
}  // namespace ircx
