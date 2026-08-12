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
 * @file PlotSpefModel.hh
 * @brief plot_spef implementation detail.
 */
#pragma once

#include <unordered_map>
#include <vector>

#include "Types.hh"

namespace ircx::plot_spef {

struct Node
{
  std::string name;
  int layer = 0;
  int x = 0;
  int y = 0;
  int llx = 0;
  int lly = 0;
  int urx = 0;
  int ury = 0;
  bool has_point = false;
  bool has_box = false;
};

struct Resistor
{
  std::string node1;
  std::string node2;
  F64 value = 0.0;
  Size index = 0;
  F64 length = 0.0;
  F64 width = 0.0;
  int layer = 0;
  int direction = -1;
  int llx = 0;
  int lly = 0;
  int urx = 0;
  int ury = 0;
  bool has_length = false;
  bool has_width = false;
  bool has_layer = false;
  bool has_direction = false;
  bool has_box = false;
};

struct NodeRef
{
  Size net_index = 0;
  Size node_index = 0;
  bool valid = false;
};

inline auto resistorLength(const Resistor& resistor) -> F64
{
  return resistor.has_length ? resistor.length : 0.0;
}

inline auto resistorWidth(const Resistor& resistor) -> F64
{
  return resistor.has_width ? resistor.width : 0.0;
}

inline auto isWireResistor(const Resistor& resistor) -> bool
{
  return resistor.has_layer
         && resistor.layer >= 1
         && resistor.layer <= 10
         && resistorLength(resistor) > 1e-12
         && resistorWidth(resistor) < 1.0;
}

struct EdgeRef
{
  Size net_index = 0;
  Size resistor_index = 0;
  bool valid = false;
};

inline auto edgeRefKey(const EdgeRef& ref) -> std::string
{
  return std::to_string(ref.net_index) + ":" + std::to_string(ref.resistor_index);
}

inline auto edgeRefTieValue(const EdgeRef& ref) -> Size
{
  return ref.net_index * 1000000U + ref.resistor_index;
}

inline auto nodeEdgeVoteKey(const std::string& node,
                            const EdgeRef& ref) -> std::string
{
  return node + "\n" + edgeRefKey(ref);
}

struct Capacitor
{
  std::string node1;
  std::string node2;
  F64 value = 0.0;
  EdgeRef edge1;
  EdgeRef edge2;
};

struct Net
{
  std::string name;
  std::vector<Node> nodes;
  std::unordered_map<std::string, Size> node_index_by_name;
  std::vector<Resistor> resistors;
  std::vector<Capacitor> coupling_caps;
  std::vector<Capacitor> ground_caps;
};

inline auto findNode(Net& net,
                     const std::string& name) -> Node*
{
  const auto it = net.node_index_by_name.find(name);
  return it == net.node_index_by_name.end() || it->second >= net.nodes.size()
             ? nullptr
             : &net.nodes[it->second];
}

inline auto findNode(const Net& net,
                     const std::string& name) -> const Node*
{
  const auto it = net.node_index_by_name.find(name);
  return it == net.node_index_by_name.end() || it->second >= net.nodes.size()
             ? nullptr
             : &net.nodes[it->second];
}

struct Model
{
  std::string design_name = "plot_spef";
  std::string vendor_name;
  std::string program_name;
  std::string cap_unit;
  std::string res_unit;
  int dbu = 1000;
  std::vector<Net> nets;
  std::unordered_map<std::string, NodeRef> node_refs_by_name;
  std::unordered_map<int, std::string> layer_names;
};

inline auto findNode(Model& model,
                     const std::string& name) -> Node*
{
  const auto it = model.node_refs_by_name.find(name);
  if (it == model.node_refs_by_name.end()
      || !it->second.valid
      || it->second.net_index >= model.nets.size()) {
    return nullptr;
  }
  auto& net = model.nets[it->second.net_index];
  return it->second.node_index >= net.nodes.size()
             ? nullptr
             : &net.nodes[it->second.node_index];
}

inline auto findNode(const Model& model,
                     const std::string& name) -> const Node*
{
  const auto it = model.node_refs_by_name.find(name);
  if (it == model.node_refs_by_name.end()
      || !it->second.valid
      || it->second.net_index >= model.nets.size()) {
    return nullptr;
  }
  const auto& net = model.nets[it->second.net_index];
  return it->second.node_index >= net.nodes.size()
             ? nullptr
             : &net.nodes[it->second.node_index];
}

}  // namespace ircx::plot_spef
