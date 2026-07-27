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
 * @file CompareSpefData.hh
 * @brief compare_spef implementation detail.
 */
#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Types.hh"

namespace ircx {
namespace compare_spef {

struct NodePair
{
  std::string first;
  std::string second;

  static auto ordered(std::string node1,
                      std::string node2) -> NodePair
  {
    if (node2 < node1) {
      std::swap(node1, node2);
    }
    return NodePair{std::move(node1), std::move(node2)};
  }

  friend auto operator<(const NodePair& lhs,
                        const NodePair& rhs) -> bool
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
  auto operator()(const NodePair& pair) const -> Size
  {
    const Size first_hash = std::hash<std::string>{}(pair.first);
    const Size second_hash = std::hash<std::string>{}(pair.second);
    return first_hash
           ^ (second_hash
              + 0x9e3779b97f4a7c15ULL
              + (first_hash << 6)
              + (first_hash >> 2));
  }
};

struct Pin
{
  std::string name;
  std::string direction;
  std::string driving_cell;
  bool is_external = false;
  F64 x = 0.0;
  F64 y = 0.0;
  bool has_coordinate = false;
  Size connection_order = 0;
  Size name_map_index = 0;
  bool has_name_map_index = false;
};

struct Resistor
{
  std::string node1;
  std::string node2;
  F64 resistance = 0.0;
};

using NetCouplingCapMap = std::unordered_map<NodePair, F64, NodePairHash>;
using NodeGroundCapMap = std::unordered_map<std::string, F64>;

struct Net
{
  std::string name;
  F64 total_cap = 0.0;
  NodeGroundCapMap node_ground_caps;
  NetCouplingCapMap node_coupling_caps;
  std::vector<Resistor> resistors;
  std::vector<Pin> pins;
};

using NetList = std::vector<Net>;

struct DataIndex
{
  std::unordered_map<std::string, Size> net_by_name;
  std::unordered_map<std::string, Size> net_order;
  std::unordered_map<std::string, std::string> node_to_net;

  void reserve(Size net_count)
  {
    net_by_name.reserve(net_count);
    net_order.reserve(net_count);
    node_to_net.reserve(net_count * 4);
  }

  void rememberNodeNet(const std::string& node_name,
                       const std::string& net_name)
  {
    if (!node_name.empty() && !net_name.empty()) {
      node_to_net.try_emplace(node_name, net_name);
    }
  }

  void registerNet(const std::string& net_name,
                   Size net_index)
  {
    net_order.try_emplace(net_name, net_order.size());
    net_by_name[net_name] = net_index;
  }

  auto containsNet(const std::string& net_name) const -> bool
  {
    return net_by_name.contains(net_name);
  }

  auto orderOf(const std::string& net_name) const -> Size
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

struct CouplingCapEntry
{
  NodePair nets;
  F64 capacitance = 0.0;
};

struct CouplingCapStore
{
  std::vector<CouplingCapEntry> entries;
  std::unordered_map<NodePair, Size, NodePairHash> index_by_pair;

  void clear()
  {
    entries.clear();
    index_by_pair.clear();
  }

  void reserve(Size count)
  {
    entries.reserve(count);
    index_by_pair.reserve(count);
  }

  void add(NodePair pair,
           F64 capacitance)
  {
    const auto [it, inserted] = index_by_pair.emplace(pair, entries.size());
    if (inserted) {
      entries.push_back(CouplingCapEntry{.nets = std::move(pair), .capacitance = capacitance});
      return;
    }
    entries[it->second].capacitance += capacitance;
  }

  auto empty() const -> bool { return entries.empty(); }
  auto size() const -> Size { return entries.size(); }
  auto contains(const NodePair& pair) const -> bool { return index_by_pair.contains(pair); }
  auto find(const NodePair& pair) const -> const CouplingCapEntry*
  {
    const auto it = index_by_pair.find(pair);
    return it == index_by_pair.end() ? nullptr : &entries[it->second];
  }
};

struct Data
{
  std::string file_name;
  std::string cap_unit;
  std::string res_unit;
  NetList nets;
  DataIndex index;
  CouplingCapStore coupling_caps;

  void reserveNets(Size net_count)
  {
    nets.reserve(net_count);
    index.reserve(net_count);
  }

  auto findNet(const std::string& net_name) const -> const Net*
  {
    const auto net_it = index.net_by_name.find(net_name);
    if (net_it == index.net_by_name.end() || net_it->second >= nets.size()) {
      return nullptr;
    }
    return &nets[net_it->second];
  }

  auto addOrAssignNet(Net net) -> Net&
  {
    const std::string net_name = net.name;
    const auto net_it = index.net_by_name.find(net_name);
    if (net_it != index.net_by_name.end() && net_it->second < nets.size()) {
      nets[net_it->second] = std::move(net);
      index.registerNet(net_name, net_it->second);
      return nets[net_it->second];
    }

    nets.push_back(std::move(net));
    Net& stored_net = nets.back();
    index.registerNet(stored_net.name, nets.size() - 1);
    return stored_net;
  }
};

struct ValueRow
{
  std::string net;
  F64 reference = 0.0;
  F64 test = 0.0;
  F64 delta = 0.0;
  std::optional<F64> relative_delta;
};

struct CcapRow
{
  std::string victim;
  std::string aggressor;
  F64 reference = 0.0;
  F64 test = 0.0;
  F64 delta = 0.0;
  F64 reference_victim_total_cap = 0.0;
  std::optional<F64> relative_delta;
};

struct GcapRow : ValueRow
{
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
  Size reference_net_count = 0;
  Size test_net_count = 0;
  Size matched_net_count = 0;
  Size reference_only_net_count = 0;
  Size test_only_net_count = 0;
  Size reference_coupling_count = 0;
  Size test_coupling_count = 0;
  Size reference_only_coupling_count = 0;
  Size test_only_coupling_count = 0;
  Size tcap_row_count = 0;
  Size gcap_row_count = 0;
  Size ccap_row_count = 0;
  Size p2p_row_count = 0;
};

struct CcapMismatch
{
  NodePair nets;
  NodePair report_nets;
  Size first_order = 0;
  Size second_order = 0;
  bool first_external = false;
  bool second_external = false;
  F64 capacitance = 0.0;
};

struct Result
{
  std::vector<ValueRow> tcap_rows;
  std::vector<GcapRow> gcap_rows;
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
