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
 * @file PlotSpefSelect.cc
 * @brief Select the plot_spef objects that should be visible.
 */
#include "select/PlotSpefSelect.hh"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "SpefParser.hh"
#include "config/PlotSpefConfig.hh"
#include "Logger.hpp"
#include "model/PlotSpefModel.hh"
#include "model/PlotSpefVisibility.hh"

namespace ircx::plot_spef {
namespace {

struct EdgeName
{
  std::string net_name;
  Size res_index = 0;
};

struct NetRef
{
  Size index = 0;
  const Net* net = nullptr;
};

using NetMap = std::unordered_map<std::string, NetRef>;

auto owningNetName(const std::string& node_name) -> std::string
{
  const auto delimiter = node_name.find(':');
  return delimiter == std::string::npos ? node_name : node_name.substr(0, delimiter);
}

auto normalizeSpefName(const spef::Exchange& exchange,
                       const std::string& name) -> std::string
{
  return spef::removeEscapes(spef::stripQuotes(spef::expandName(exchange, name)));
}

auto sameEdgeRef(const EdgeRef& lhs,
                 const EdgeRef& rhs) -> bool
{
  return lhs.valid
         && rhs.valid
         && lhs.valid == rhs.valid
         && lhs.net_index == rhs.net_index
         && lhs.resistor_index == rhs.resistor_index;
}

auto edgeContainsNode(const Model& model,
                      const EdgeRef& edge_ref,
                      const std::string& node_name) -> bool
{
  if (!edge_ref.valid || edge_ref.net_index >= model.nets.size()) {
    return false;
  }

  const auto& net = model.nets[edge_ref.net_index];
  if (edge_ref.resistor_index >= net.resistors.size()) {
    return false;
  }

  const auto& resistor = net.resistors[edge_ref.resistor_index];
  return resistor.node1 == node_name || resistor.node2 == node_name;
}

auto capSideTouchesTargetEdge(const Model& model,
                              const EdgeRef& cap_edge,
                              const std::string& cap_node,
                              const EdgeRef& target_edge) -> bool
{
  if (sameEdgeRef(cap_edge, target_edge)) {
    return true;
  }

  return edgeContainsNode(model, target_edge, cap_node);
}

auto parseSize(std::string_view text,
               Size& value) -> bool
{
  if (text.empty()) {
    return false;
  }
  std::string value_text{text};
  char* end = nullptr;
  errno = 0;
  const auto parsed = std::strtoull(value_text.c_str(), &end, 10);
  if (errno != 0 || end == value_text.c_str() || *end != '\0') {
    return false;
  }
  value = static_cast<Size>(parsed);
  return true;
}

auto parseEdgeName(const spef::Exchange& exchange,
                   const Config& config) -> std::optional<EdgeName>
{
  const auto delimiter = config.edge_name.rfind(':');
  if (delimiter == std::string::npos || delimiter + 1 >= config.edge_name.size()) {
    RCXLOG.warn(Loc::current(), "plot_spef -edge expects net_name:index, got ", config.edge_name);
    return std::nullopt;
  }

  EdgeName edge_name;
  edge_name.net_name = normalizeSpefName(exchange, config.edge_name.substr(0, delimiter));
  if (!parseSize(std::string_view{config.edge_name}.substr(delimiter + 1), edge_name.res_index)) {
    RCXLOG.warn(Loc::current(), "plot_spef -edge expects a non-negative *RES index, got ", config.edge_name);
    return std::nullopt;
  }
  return edge_name;
}

auto buildNetMap(const Model& model) -> NetMap
{
  NetMap net_map;
  net_map.reserve(model.nets.size());
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    net_map[net.name] = NetRef{.index = net_index, .net = &net};
  }
  return net_map;
}

auto findEdgeRef(const Model& model,
                 const EdgeName& edge_name) -> EdgeRef
{
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    if (net.name != edge_name.net_name) {
      continue;
    }
    for (Size resistor_index = 0; resistor_index < net.resistors.size(); ++resistor_index) {
      if (net.resistors[resistor_index].index == edge_name.res_index) {
        return EdgeRef{
            .net_index = net_index,
            .resistor_index = resistor_index,
            .valid = true};
      }
    }
  }
  return {};
}

auto showNet(Visibility& visibility,
             Size net_index,
             bool context_only = true) -> void
{
  if (net_index >= visibility.nets.size()) {
    return;
  }
  auto& net_visibility = visibility.nets[net_index];
  net_visibility.visible = true;
  net_visibility.context_only = net_visibility.context_only && context_only;
}

auto showNode(const Model& model,
              Visibility& visibility,
              NetMap& net_map,
              const std::string& node_name) -> void
{
  const auto node_it = model.node_refs_by_name.find(node_name);
  if (node_it != model.node_refs_by_name.end()
      && node_it->second.valid
      && node_it->second.net_index < visibility.nets.size()) {
    setFlag(visibility.nets[node_it->second.net_index].nodes, node_it->second.node_index);
    showNet(visibility, node_it->second.net_index);
  }

  const auto net_it = net_map.find(owningNetName(node_name));
  if (net_it != net_map.end() && net_it->second.net != nullptr) {
    showNet(visibility, net_it->second.index);
  }
}

auto showEdge(const Model& model,
              Visibility& visibility,
              NetMap& net_map,
              const EdgeRef& edge_ref) -> void
{
  if (!edge_ref.valid || edge_ref.net_index >= model.nets.size()) {
    return;
  }

  const auto& net = model.nets[edge_ref.net_index];
  showNet(visibility, edge_ref.net_index);
  if (edge_ref.resistor_index >= net.resistors.size()
      || edge_ref.net_index >= visibility.nets.size()) {
    return;
  }

  const auto& resistor = net.resistors[edge_ref.resistor_index];
  setFlag(visibility.nets[edge_ref.net_index].resistors, edge_ref.resistor_index);
  showNode(model, visibility, net_map, resistor.node1);
  showNode(model, visibility, net_map, resistor.node2);
}

auto markEdgeTarget(Visibility& visibility,
                    const EdgeRef& edge_ref) -> void
{
  if (!edge_ref.valid || edge_ref.net_index >= visibility.nets.size()) {
    return;
  }
  setFlag(visibility.nets[edge_ref.net_index].target_resistors, edge_ref.resistor_index);
}

auto showIncidentEdges(const Model& model,
                       Visibility& visibility,
                       NetMap& net_map,
                       const std::string& node_name) -> void
{
  const auto net_it = net_map.find(owningNetName(node_name));
  if (net_it == net_map.end() || net_it->second.net == nullptr) {
    return;
  }

  const auto& net = *net_it->second.net;
  for (Size resistor_index = 0; resistor_index < net.resistors.size(); ++resistor_index) {
    const auto& resistor = net.resistors[resistor_index];
    if (resistor.node1 == node_name || resistor.node2 == node_name) {
      showEdge(
          model,
          visibility,
          net_map,
          EdgeRef{
              .net_index = net_it->second.index,
              .resistor_index = resistor_index,
              .valid = true});
    }
  }
}

auto showCapEndpoint(const Model& model,
                     Visibility& visibility,
                     NetMap& net_map,
                     const EdgeRef& edge_ref,
                     const std::string& node_name) -> void
{
  showNode(model, visibility, net_map, node_name);
  if (edge_ref.valid) {
    showEdge(model, visibility, net_map, edge_ref);
    return;
  }
  showIncidentEdges(model, visibility, net_map, node_name);
}

auto showOnlyNet(const Model& model,
                 Visibility& visibility,
                 const std::string& target_net) -> bool
{
  bool found_target = false;
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    const bool is_target_net = net.name == target_net;
    found_target = found_target || is_target_net;
    if (net_index >= visibility.nets.size()) {
      continue;
    }
    auto& net_visibility = visibility.nets[net_index];
    net_visibility.visible = is_target_net;
    net_visibility.context_only = !is_target_net;
    std::fill(net_visibility.nodes.begin(), net_visibility.nodes.end(), is_target_net ? 1 : 0);
    std::fill(net_visibility.resistors.begin(), net_visibility.resistors.end(), is_target_net ? 1 : 0);
    std::fill(net_visibility.target_resistors.begin(), net_visibility.target_resistors.end(), 0);
    std::fill(net_visibility.coupling_caps.begin(), net_visibility.coupling_caps.end(), is_target_net ? 1 : 0);
    std::fill(net_visibility.ground_caps.begin(), net_visibility.ground_caps.end(), is_target_net ? 1 : 0);
  }
  return found_target;
}

auto showCoupledNetContext(const Model& model,
                           Visibility& visibility,
                           NetMap& net_map,
                           const std::string& target_net) -> void
{
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    for (Size cap_index = 0; cap_index < net.coupling_caps.size(); ++cap_index) {
      const auto& cap = net.coupling_caps[cap_index];
      const std::string net1 = owningNetName(cap.node1);
      const std::string net2 = owningNetName(cap.node2);
      if (net1 == target_net || net2 == target_net) {
        if (net_index < visibility.nets.size()) {
          setFlag(visibility.nets[net_index].coupling_caps, cap_index);
          showNet(visibility, net_index);
        }
        showNode(model, visibility, net_map, cap.node1);
        showNode(model, visibility, net_map, cap.node2);
        showEdge(model, visibility, net_map, cap.edge1);
        showEdge(model, visibility, net_map, cap.edge2);
      }
    }
  }
}

auto makeNetVisibleObjects(const Model& model,
                           const spef::Exchange& exchange,
                           const Config& config) -> Visibility
{
  auto visibility = makeVisibility(model, false);
  const std::string target_net = normalizeSpefName(exchange, config.net_name);
  auto net_map = buildNetMap(model);
  const bool found_target = showOnlyNet(model, visibility, target_net);
  showCoupledNetContext(model, visibility, net_map, target_net);
  if (!found_target) {
    RCXLOG.warn(Loc::current(), "plot_spef warning: target net not found: ", config.net_name);
  }
  return visibility;
}

auto makeEdgeVisibleObjectsImpl(const Model& model,
                                const spef::Exchange& exchange,
                                const Config& config) -> Visibility
{
  auto visibility = makeVisibility(model, false);
  auto edge_name = parseEdgeName(exchange, config);
  if (!edge_name.has_value()) {
    return visibility;
  }

  auto net_map = buildNetMap(model);
  const EdgeRef target_edge = findEdgeRef(model, *edge_name);
  if (!target_edge.valid) {
    RCXLOG.warn(Loc::current(), "plot_spef warning: target edge not found: ", config.edge_name);
    return visibility;
  }

  showEdge(model, visibility, net_map, target_edge);
  markEdgeTarget(visibility, target_edge);
  const auto& target_net = model.nets[target_edge.net_index];
  const auto& target_resistor = target_net.resistors[target_edge.resistor_index];
  showIncidentEdges(model, visibility, net_map, target_resistor.node1);
  showIncidentEdges(model, visibility, net_map, target_resistor.node2);
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    for (Size cap_index = 0; cap_index < net.ground_caps.size(); ++cap_index) {
      const auto& cap = net.ground_caps[cap_index];
      if (!capSideTouchesTargetEdge(model, cap.edge1, cap.node1, target_edge)) {
        continue;
      }
      if (net_index < visibility.nets.size()) {
        setFlag(visibility.nets[net_index].ground_caps, cap_index);
        showNet(visibility, net_index);
      }
      showCapEndpoint(model, visibility, net_map, target_edge, cap.node1);
    }

    for (Size cap_index = 0; cap_index < net.coupling_caps.size(); ++cap_index) {
      const auto& cap = net.coupling_caps[cap_index];
      const bool side1_is_target = capSideTouchesTargetEdge(
          model, cap.edge1, cap.node1, target_edge);
      const bool side2_is_target = capSideTouchesTargetEdge(
          model, cap.edge2, cap.node2, target_edge);
      if (!side1_is_target && !side2_is_target) {
        continue;
      }

      if (net_index < visibility.nets.size()) {
        setFlag(visibility.nets[net_index].coupling_caps, cap_index);
        showNet(visibility, net_index);
      }
      showCapEndpoint(
          model,
          visibility,
          net_map,
          target_edge,
          side1_is_target ? cap.node1 : cap.node2);
      if (side1_is_target) {
        showCapEndpoint(model, visibility, net_map, cap.edge2, cap.node2);
      }
      if (side2_is_target) {
        showCapEndpoint(model, visibility, net_map, cap.edge1, cap.node1);
      }
    }
  }
  return visibility;
}

}  // namespace

auto makeVisibleObjects(const Model& model,
                        const spef::Exchange& exchange,
                        const Config& config) -> Visibility
{
  if (config.hasEdgeFilter()) {
    return makeEdgeVisibleObjects(model, exchange, config);
  }
  if (config.hasNetFilter()) {
    return makeNetVisibleObjects(model, exchange, config);
  }
  return makeVisibility(model, true);
}

auto makeEdgeVisibleObjects(const Model& model,
                            const spef::Exchange& exchange,
                            const Config& config) -> Visibility
{
  return makeEdgeVisibleObjectsImpl(model, exchange, config);
}

}  // namespace ircx::plot_spef
