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
/**
 * @file SpefReader.cc
 * @brief compare_spef implementation detail.
 */
#include "reader/SpefReader.hh"

#include "RCXHeader.hpp"
#include "SpefParser.hh"
#include "utils/SpefUnit.hh"

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

auto nameMapIndex(const std::string& name) -> std::optional<Size>
{
  if (name.size() < 2 || name.front() != '*' || !std::isdigit(static_cast<unsigned char>(name[1]))) {
    return std::nullopt;
  }

  const Size begin = 1;
  const Size colon = name.find(':', begin);
  return static_cast<Size>(std::strtoull(name.substr(begin, colon - begin).c_str(), nullptr, 10));
}

}  // namespace

class NameExpander
{
 public:
  explicit NameExpander(const spef::Exchange& exchange) : _exchange(exchange) { _expanded_base_names.reserve(exchange.index_to_name_map.size()); }

  auto expand(const std::string& name) -> std::string
  {
    if (!startsWithNameIndex(name)) {
      return name;
    }

    const Size begin = 1;
    const Size colon = name.find(':', begin);
    const Size index = static_cast<Size>(std::strtoull(name.substr(begin, colon - begin).c_str(), nullptr, 10));
    const std::string* base_name = get_expanded_base_name(index);
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

  auto get_expanded_base_name(Size index) -> const std::string*
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
  std::unordered_map<Size, std::string> _expanded_base_names;
};

void SpefReader::buildNetCouplingCaps(Data& data) const
{
  Size coupling_count = 0;
  for (const Net& net : data.nets) {
    coupling_count += net.node_coupling_caps.size();
  }

  data.coupling_caps.clear();
  data.coupling_caps.reserve(coupling_count);

  for (const Net& net : data.nets) {
    for (const auto& [node_pair, capacitance] : net.node_coupling_caps) {
      const auto net1 = data.index.resolveNodeNet(node_pair.first);
      const auto net2 = data.index.resolveNodeNet(node_pair.second);
      if (net1.empty() || net2.empty() || net1 == net2) {
        continue;
      }

      std::string aggressor;
      if (net1 == net.name) {
        aggressor = net2;
      } else if (net2 == net.name) {
        aggressor = net1;
      } else {
        continue;
      }
      data.coupling_caps.add(NodePair{net.name, std::move(aggressor)}, capacitance);
    }
  }
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
  const F64 cap_scale = spef_unit::capacitanceScaleToFf(data.cap_unit);
  const F64 res_scale = spef_unit::resistanceScaleToOhm(data.res_unit);
  data.cap_unit = "1.0 FF";
  data.res_unit = "1.0 OHM";
  data.reserveNets(spef_file->nets.size());
  NameExpander name_expander(*spef_file);

  for (const auto& spef_net : spef_file->nets) {
    Net net;
    net.name = name_expander.expand(spef_net.name);
    net.total_cap = spef_net.lcap * cap_scale;
    net.pins.reserve(spef_net.conns.size());
    net.resistors.reserve(spef_net.ress.size());

    Size ground_cap_count = 0;
    Size coupling_cap_count = 0;
    for (const auto& cap : spef_net.caps) {
      if (cap.node2.empty()) {
        ground_cap_count++;
      } else {
        coupling_cap_count++;
      }
    }
    net.node_ground_caps.reserve(ground_cap_count);
    net.node_coupling_caps.reserve(coupling_cap_count);

    for (Size conn_index = 0; conn_index < spef_net.conns.size(); ++conn_index) {
      const auto& conn = spef_net.conns[conn_index];
      Pin pin;
      pin.name = name_expander.expand(conn.pin_port_name);
      pin.direction = directionName(conn.conn_direction);
      pin.driving_cell = conn.driving_cell;
      pin.is_external = conn.conn_type == spef::ConnectionType::kExternal;
      pin.x = conn.coordinate.x;
      pin.y = conn.coordinate.y;
      pin.has_coordinate = conn.coordinate.x >= 0.0 && conn.coordinate.y >= 0.0;
      pin.connection_order = conn_index;
      if (const auto index = nameMapIndex(conn.pin_port_name)) {
        pin.name_map_index = *index;
        pin.has_name_map_index = true;
      }
      data.index.rememberNodeNet(pin.name, net.name);
      net.pins.push_back(std::move(pin));
    }

    for (const auto& cap : spef_net.caps) {
      std::string node1 = name_expander.expand(cap.node1);
      if (cap.node2.empty()) {
        data.index.rememberNodeNet(node1, net.name);
        net.node_ground_caps[std::move(node1)] += cap.res_or_cap * cap_scale;
      } else {
        std::string node2 = name_expander.expand(cap.node2);
        data.index.rememberNodeNet(node1, net.name);
        net.node_coupling_caps[NodePair::ordered(std::move(node1), std::move(node2))] += cap.res_or_cap * cap_scale;
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
      net.resistors.push_back(Resistor{std::move(node1), std::move(node2), res.res_or_cap * res_scale});
    }

    data.addOrAssignNet(std::move(net));
  }

  buildNetCouplingCaps(data);
  return true;
}

}  // namespace compare_spef
}  // namespace ircx
