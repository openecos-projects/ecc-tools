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
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ircx {
namespace compare_spef {

struct NodePair
{
  std::string first;
  std::string second;

  static auto ordered(std::string node1, std::string node2) -> NodePair
  {
    if (node2 < node1) {
      std::swap(node1, node2);
    }
    return NodePair{std::move(node1), std::move(node2)};
  }

  friend auto operator<(const NodePair& lhs, const NodePair& rhs) -> bool
  {
    if (lhs.first != rhs.first) {
      return lhs.first < rhs.first;
    }
    return lhs.second < rhs.second;
  }

  friend auto operator==(const NodePair& lhs, const NodePair& rhs) -> bool
  {
    return lhs.first == rhs.first && lhs.second == rhs.second;
  }
};

struct NodePairHash
{
  auto operator()(const NodePair& pair) const -> std::size_t
  {
    const std::size_t first_hash = std::hash<std::string>{}(pair.first);
    const std::size_t second_hash = std::hash<std::string>{}(pair.second);
    return first_hash ^ (second_hash + 0x9e3779b97f4a7c15ULL + (first_hash << 6) + (first_hash >> 2));
  }
};

struct Pin
{
  std::string name;
  std::string direction;
  std::string driving_cell;
  bool is_external = false;
  double x = 0.0;
  double y = 0.0;
  bool has_coordinate = false;
};

struct Resistor
{
  std::string node1;
  std::string node2;
  double resistance = 0.0;
};

using NetCouplingCapMap = std::unordered_map<NodePair, double, NodePairHash>;

struct Net
{
  std::string name;
  double total_cap = 0.0;
  NetCouplingCapMap node_coupling_caps;
  std::vector<Resistor> resistors;
  std::vector<Pin> pins;
};

using NetMap = std::map<std::string, Net>;

struct DataIndex
{
  std::unordered_map<std::string, const Net*> net_by_name;
  std::unordered_map<std::string, std::size_t> net_order;
  std::unordered_map<std::string, std::string> node_to_net;

  void reserve(std::size_t net_count)
  {
    net_by_name.reserve(net_count);
    net_order.reserve(net_count);
    node_to_net.reserve(net_count * 4);
  }

  void rememberNodeNet(const std::string& node_name, const std::string& net_name)
  {
    if (!node_name.empty() && !net_name.empty()) {
      node_to_net.try_emplace(node_name, net_name);
    }
  }

  void registerNet(const std::string& net_name, const Net& net)
  {
    net_order.try_emplace(net_name, net_order.size());
    net_by_name[net_name] = &net;
  }

  auto findNet(const std::string& net_name) const -> const Net*
  {
    const auto net_it = net_by_name.find(net_name);
    return net_it == net_by_name.end() ? nullptr : net_it->second;
  }

  auto containsNet(const std::string& net_name) const -> bool { return net_by_name.contains(net_name); }

  auto orderOf(const std::string& net_name) const -> std::size_t
  {
    const auto order_it = net_order.find(net_name);
    return order_it == net_order.end() ? 0 : order_it->second;
  }

  auto resolveNodeNet(const std::string& node_name) const -> std::string
  {
    const auto node_it = node_to_net.find(node_name);
    if (node_it != node_to_net.end()) {
      return node_it->second;
    }

    const auto colon = node_name.find(':');
    if (colon != std::string::npos) {
      const auto prefix = node_name.substr(0, colon);
      if (containsNet(prefix)) {
        return prefix;
      }
    }

    return containsNet(node_name) ? node_name : std::string{};
  }
};

struct CouplingCapStore
{
  using Value = std::pair<NodePair, double>;

  std::vector<Value> ordered;
  NetCouplingCapMap lookup;

  void clear()
  {
    ordered.clear();
    lookup.clear();
  }

  void reserve(std::size_t count)
  {
    ordered.reserve(count);
    lookup.reserve(count);
  }

  void add(NodePair pair, double capacitance) { lookup[std::move(pair)] += capacitance; }

  void rebuildOrdered()
  {
    ordered.clear();
    ordered.reserve(lookup.size());
    ordered.insert(ordered.end(), lookup.begin(), lookup.end());
    std::sort(ordered.begin(), ordered.end(), [](const Value& lhs, const Value& rhs) { return lhs.first < rhs.first; });
  }

  auto size() const -> std::size_t { return lookup.size(); }
  auto contains(const NodePair& pair) const -> bool { return lookup.contains(pair); }
  auto find(const NodePair& pair) const -> NetCouplingCapMap::const_iterator { return lookup.find(pair); }
  auto end() const -> NetCouplingCapMap::const_iterator { return lookup.end(); }
  auto beginOrdered() const -> std::vector<Value>::const_iterator { return ordered.begin(); }
  auto endOrdered() const -> std::vector<Value>::const_iterator { return ordered.end(); }
};

struct Data
{
  std::string file_name;
  std::string cap_unit;
  std::string res_unit;
  NetMap nets;
  DataIndex index;
  CouplingCapStore coupling_caps;
};

struct ValueRow
{
  std::string net;
  double reference = 0.0;
  double test = 0.0;
  double delta = 0.0;
  std::optional<double> relative_delta;
};

struct CcapRow
{
  std::string victim;
  std::string aggressor;
  double reference = 0.0;
  double test = 0.0;
  double delta = 0.0;
  double reference_victim_total_cap = 0.0;
  std::optional<double> relative_delta;
};

struct ResistanceRow : ValueRow
{
  std::string from_pin;
  std::string to_pin;
  bool reference_valid = false;
  bool test_valid = false;
};

struct Summary
{
  std::size_t reference_net_count = 0;
  std::size_t test_net_count = 0;
  std::size_t matched_net_count = 0;
  std::size_t reference_only_net_count = 0;
  std::size_t test_only_net_count = 0;
  std::size_t reference_coupling_count = 0;
  std::size_t test_coupling_count = 0;
  std::size_t reference_only_coupling_count = 0;
  std::size_t test_only_coupling_count = 0;
  std::size_t tcap_row_count = 0;
  std::size_t ccap_row_count = 0;
  std::size_t p2p_row_count = 0;
};

struct CcapMismatch
{
  NodePair nets;
  NodePair report_nets;
  std::size_t first_order = 0;
  std::size_t second_order = 0;
  bool first_external = false;
  bool second_external = false;
  double capacitance = 0.0;
};

struct Result
{
  std::vector<ValueRow> tcap_rows;
  std::vector<CcapRow> ccap_rows;
  std::vector<ResistanceRow> p2p_rows;
  std::vector<std::string> reference_only_nets;
  std::vector<std::string> test_only_nets;
  std::vector<CcapMismatch> reference_only_couplings;
  std::vector<CcapMismatch> test_only_couplings;
  Summary summary;
};

}  // namespace compare_spef
}  // namespace ircx
