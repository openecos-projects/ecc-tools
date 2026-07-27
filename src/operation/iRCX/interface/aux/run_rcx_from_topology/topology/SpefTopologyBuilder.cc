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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "topology/SpefTopologyBuilder.hh"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Geometry.hh"
#include "LayerTable.hpp"
#include "LayoutData.hpp"
#include "Net.hpp"
#include "Pin.hpp"
#include "SpefParser.hh"
#include "TopoEdge.hpp"
#include "TopoNode.hpp"
#include "TopoPool.hpp"
#include "log/Log.hh"

namespace ircx::run_rcx_from_topology::detail {

class NetTopo
{
 public:
  std::vector<TopoNode> node_list;
  std::vector<TopoEdge> edge_list;
};

class NodeInfo
{
 public:
  NodeInfo() = default;
  NodeInfo(int32_t local_node_idx, int32_t layer_idx) : _local_node_idx(local_node_idx), _layer_idx(layer_idx) {}
  ~NodeInfo() = default;
  // getter
  int32_t get_local_node_idx() const { return _local_node_idx; }
  int32_t get_layer_idx() const { return _layer_idx; }

 private:
  int32_t _local_node_idx = -1;
  int32_t _layer_idx = -1;
};

bool getHasCoord(const spef::Coord& coord)
{
  return coord.x >= 0.0 && coord.y >= 0.0;
}

std::string getNormalizedSpefName(const std::string& name)
{
  return spef::removeEscapes(spef::stripQuotes(name));
}

GTLPointInt getPoint(const spef::Coord& coord, int32_t dbu_per_micron)
{
  return GTLPointInt(static_cast<int32_t>(std::llround(coord.x * dbu_per_micron)),
                     static_cast<int32_t>(std::llround(coord.y * dbu_per_micron)));
}

GTLPointInt getGeometryCenter(const spef::GeometryAttr& geometry, int32_t dbu_per_micron)
{
  spef::Coord center_coord;
  center_coord.x = (geometry.ll_coordinate.x + geometry.ur_coordinate.x) / 2.0;
  center_coord.y = (geometry.ll_coordinate.y + geometry.ur_coordinate.y) / 2.0;
  return getPoint(center_coord, dbu_per_micron);
}

GTLRectInt getGeometryRect(const spef::GeometryAttr& geometry, int32_t dbu_per_micron)
{
  int32_t lower_x = static_cast<int32_t>(std::llround(std::min(geometry.ll_coordinate.x, geometry.ur_coordinate.x) * dbu_per_micron));
  int32_t lower_y = static_cast<int32_t>(std::llround(std::min(geometry.ll_coordinate.y, geometry.ur_coordinate.y) * dbu_per_micron));
  int32_t upper_x = static_cast<int32_t>(std::llround(std::max(geometry.ll_coordinate.x, geometry.ur_coordinate.x) * dbu_per_micron));
  int32_t upper_y = static_cast<int32_t>(std::llround(std::max(geometry.ll_coordinate.y, geometry.ur_coordinate.y) * dbu_per_micron));
  return geom::makeRect<GTLRectInt>(lower_x, lower_y, upper_x, upper_y);
}

int32_t getWidth(const spef::GeometryAttr& geometry, int32_t dbu_per_micron)
{
  if (geometry.has_width && geometry.width > 0.0) {
    return std::max(static_cast<int32_t>(std::llround(geometry.width * dbu_per_micron)), 2);
  }
  return 2;
}

void expandRangeToWidth(int32_t& lower_coord, int32_t& upper_coord, int32_t width)
{
  int32_t center_coord = lower_coord + (upper_coord - lower_coord) / 2;
  lower_coord = center_coord - width / 2;
  upper_coord = lower_coord + width;
  if (upper_coord <= lower_coord) {
    upper_coord = lower_coord + 1;
  }
}

GTLRectInt getEdgeRect(const spef::GeometryAttr& geometry,
                       const GTLPointInt& first_point,
                       const GTLPointInt& second_point,
                       int32_t dbu_per_micron)
{
  int32_t lower_x = static_cast<int32_t>(std::llround(std::min(geometry.ll_coordinate.x, geometry.ur_coordinate.x) * dbu_per_micron));
  int32_t lower_y = static_cast<int32_t>(std::llround(std::min(geometry.ll_coordinate.y, geometry.ur_coordinate.y) * dbu_per_micron));
  int32_t upper_x = static_cast<int32_t>(std::llround(std::max(geometry.ll_coordinate.x, geometry.ur_coordinate.x) * dbu_per_micron));
  int32_t upper_y = static_cast<int32_t>(std::llround(std::max(geometry.ll_coordinate.y, geometry.ur_coordinate.y) * dbu_per_micron));

  if (upper_x <= lower_x || upper_y <= lower_y) {
    int32_t width = getWidth(geometry, dbu_per_micron);
    if (geom::isHorizontalDominant(first_point, second_point)) {
      if (upper_x <= lower_x) {
        lower_x = std::min(geom::x(first_point), geom::x(second_point));
        upper_x = std::max(geom::x(first_point), geom::x(second_point));
      }
      if (upper_y <= lower_y) {
        expandRangeToWidth(lower_y, upper_y, width);
      }
    } else {
      if (upper_y <= lower_y) {
        lower_y = std::min(geom::y(first_point), geom::y(second_point));
        upper_y = std::max(geom::y(first_point), geom::y(second_point));
      }
      if (upper_x <= lower_x) {
        expandRangeToWidth(lower_x, upper_x, width);
      }
    }
    if (upper_x <= lower_x) {
      expandRangeToWidth(lower_x, upper_x, width);
    }
    if (upper_y <= lower_y) {
      expandRangeToWidth(lower_y, upper_y, width);
    }
  }
  return geom::makeRect<GTLRectInt>(lower_x, lower_y, upper_x, upper_y);
}

GTLRectInt getFallbackEdgeRect(const GTLPointInt& first_point, const GTLPointInt& second_point)
{
  if (geom::x(first_point) == geom::x(second_point) && geom::y(first_point) == geom::y(second_point)) {
    return geom::boxAround(first_point, 1);
  }
  if (geom::isHorizontalDominant(first_point, second_point)) {
    int32_t coord_y = geom::y(first_point) + (geom::y(second_point) - geom::y(first_point)) / 2;
    return geom::makeRect<GTLRectInt>(std::min(geom::x(first_point), geom::x(second_point)), coord_y - 1,
                                      std::max(geom::x(first_point), geom::x(second_point)), coord_y + 1);
  }
  int32_t coord_x = geom::x(first_point) + (geom::x(second_point) - geom::x(first_point)) / 2;
  return geom::makeRect<GTLRectInt>(coord_x - 1, std::min(geom::y(first_point), geom::y(second_point)), coord_x + 1,
                                    std::max(geom::y(first_point), geom::y(second_point)));
}

std::unordered_map<std::string, int32_t> getLayoutNetIdxMap(LayoutData& layout_data)
{
  std::unordered_map<std::string, int32_t> net_name_to_idx_map;
  std::vector<Net>& net_list = layout_data.get_net_list();
  net_name_to_idx_map.reserve(net_list.size());
  for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(net_list.size()); ++net_idx) {
    std::string& net_name = net_list[net_idx].get_net_name();
    if (!net_name.empty()) {
      net_name_to_idx_map[getNormalizedSpefName(net_name)] = net_idx;
    }
  }
  return net_name_to_idx_map;
}

std::unordered_map<std::string, std::string> getLayoutPinNameMap(Net& net)
{
  std::unordered_map<std::string, std::string> normalized_name_to_pin_name_map;
  normalized_name_to_pin_name_map.reserve(net.get_pin_list().size());
  for (Pin& pin : net.get_pin_list()) {
    normalized_name_to_pin_name_map[getNormalizedSpefName(pin.get_pin_name())] = pin.get_pin_name();
  }
  return normalized_name_to_pin_name_map;
}

std::optional<int32_t> getLayerIdx(int32_t net_idx,
                                   const spef::Exchange& exchange,
                                   LayerTable& layer_table,
                                   int32_t annotation_layer_idx,
                                   bool strict)
{
  std::unordered_map<int, spef::LayerMapEntry>::const_iterator layer_map_it = exchange.layer_map.find(annotation_layer_idx);
  if (layer_map_it != exchange.layer_map.end()) {
    std::unordered_map<std::string, int32_t>::iterator design_layer_it
        = layer_table.get_design_name_to_idx_map().find(layer_map_it->second.layer_name);
    if (design_layer_it != layer_table.get_design_name_to_idx_map().end()) {
      return design_layer_it->second;
    }
    LOG_ERROR << "run_rcx_from_topology warning: layer map entry not found in design layers, net_idx=" << net_idx
              << ", annotation_layer=" << annotation_layer_idx << ", layer_name=" << layer_map_it->second.layer_name << ".";
    return std::nullopt;
  }
  if (annotation_layer_idx < 0) {
    if (strict) {
      LOG_ERROR << "run_rcx_from_topology failed: invalid annotation layer " << annotation_layer_idx << ", net_idx=" << net_idx << ".";
    }
    return std::nullopt;
  }
  std::unordered_map<int32_t, std::string>::iterator design_layer_it
      = layer_table.get_design_idx_to_name_map().find(annotation_layer_idx);
  if (design_layer_it != layer_table.get_design_idx_to_name_map().end()) {
    return annotation_layer_idx;
  }
  if (strict) {
    LOG_ERROR << "run_rcx_from_topology failed: missing layer map for annotation layer " << annotation_layer_idx
              << ", net_idx=" << net_idx << ".";
  }
  return std::nullopt;
}

std::optional<int32_t> getConnLayerIdx(int32_t net_idx,
                                       const spef::Exchange& exchange,
                                       LayerTable& layer_table,
                                       const spef::ConnEntry& conn,
                                       bool strict)
{
  if (conn.geometry.has_layer) {
    return getLayerIdx(net_idx, exchange, layer_table, conn.geometry.layer, strict);
  }
  if (conn.layer > 0) {
    return getLayerIdx(net_idx, exchange, layer_table, conn.layer, strict);
  }
  if (strict) {
    LOG_ERROR << "run_rcx_from_topology failed: node missing layer, net_idx=" << net_idx << ", node=" << conn.pin_port_name << ".";
  }
  return std::nullopt;
}

std::optional<int32_t> getResLayerIdx(int32_t net_idx,
                                      const spef::Exchange& exchange,
                                      LayerTable& layer_table,
                                      const spef::ResCap& res,
                                      int32_t fallback_layer_idx,
                                      bool strict)
{
  if (res.geometry.has_layer) {
    return getLayerIdx(net_idx, exchange, layer_table, res.geometry.layer, strict);
  }
  if (fallback_layer_idx >= 0) {
    return fallback_layer_idx;
  }
  if (strict) {
    LOG_ERROR << "run_rcx_from_topology failed: resistor missing layer, net_idx=" << net_idx << ", node1=" << res.node1
              << ", node2=" << res.node2 << ".";
  }
  return std::nullopt;
}

std::optional<GTLPointInt> getConnPoint(const spef::ConnEntry& conn, int32_t dbu_per_micron)
{
  if (getHasCoord(conn.coordinate)) {
    return getPoint(conn.coordinate, dbu_per_micron);
  }
  if (conn.geometry.has_box) {
    return getGeometryCenter(conn.geometry, dbu_per_micron);
  }
  return std::nullopt;
}

bool getIsPin(const spef::ConnEntry& conn)
{
  return conn.conn_type == spef::ConnectionType::kExternal || conn.conn_direction == spef::ConnectionDirection::kInput
         || conn.conn_direction == spef::ConnectionDirection::kOutput || conn.conn_direction == spef::ConnectionDirection::kInout;
}

std::optional<NetTopo> buildNetTopo(LayoutData& layout_data,
                                    LayerTable& layer_table,
                                    const spef::Exchange& exchange,
                                    const spef::Net& spef_net,
                                    int32_t net_idx,
                                    bool strict)
{
  NetTopo net_topo;
  net_topo.node_list.reserve(spef_net.conns.size());
  net_topo.edge_list.reserve(spef_net.ress.size());
  std::unordered_map<std::string, NodeInfo> node_name_to_info_map;
  node_name_to_info_map.reserve(spef_net.conns.size());
  std::unordered_map<std::string, std::string> pin_name_map = getLayoutPinNameMap(layout_data.get_net_list()[net_idx]);

  for (const spef::ConnEntry& conn : spef_net.conns) {
    std::string node_name = getNormalizedSpefName(conn.pin_port_name);
    if (node_name_to_info_map.find(node_name) != node_name_to_info_map.end()) {
      continue;
    }
    std::optional<int32_t> layer_idx = getConnLayerIdx(net_idx, exchange, layer_table, conn, strict);
    std::optional<GTLPointInt> point = getConnPoint(conn, layout_data.get_dbu_per_micron());
    if (!layer_idx.has_value() || !point.has_value()) {
      if (strict) {
        LOG_ERROR << "run_rcx_from_topology failed: invalid node geometry, net=" << spef_net.name << ", node=" << conn.pin_port_name
                  << ".";
        return std::nullopt;
      }
      continue;
    }
    TopoNode node(net_idx);
    node.set_layer_idx(*layer_idx);
    node.set_point(*point);
    node.set_shape(conn.geometry.has_box ? getGeometryRect(conn.geometry, layout_data.get_dbu_per_micron()) : geom::boxAround(*point, 1));
    node.set_is_shape_valid(true);
    if (getIsPin(conn)) {
      std::unordered_map<std::string, std::string>::iterator pin_name_it = pin_name_map.find(node_name);
      if (pin_name_it != pin_name_map.end()) {
        node.set_pin_name(pin_name_it->second);
      }
    }
    int32_t local_node_idx = static_cast<int32_t>(net_topo.node_list.size());
    net_topo.node_list.push_back(std::move(node));
    node_name_to_info_map.emplace(node_name, NodeInfo(local_node_idx, *layer_idx));
  }

  for (const spef::ResCap& res : spef_net.ress) {
    std::string first_node_name = getNormalizedSpefName(res.node1);
    std::string second_node_name = getNormalizedSpefName(res.node2);
    std::unordered_map<std::string, NodeInfo>::iterator first_node_it = node_name_to_info_map.find(first_node_name);
    std::unordered_map<std::string, NodeInfo>::iterator second_node_it = node_name_to_info_map.find(second_node_name);
    if (first_node_it == node_name_to_info_map.end() || second_node_it == node_name_to_info_map.end()) {
      if (strict) {
        LOG_ERROR << "run_rcx_from_topology failed: resistor endpoint missing from *CONN, net=" << spef_net.name
                  << ", node1=" << res.node1 << ", node2=" << res.node2 << ".";
        return std::nullopt;
      }
      continue;
    }
    int32_t fallback_layer_idx = first_node_it->second.get_layer_idx() == second_node_it->second.get_layer_idx()
                                     ? first_node_it->second.get_layer_idx()
                                     : -1;
    std::optional<int32_t> layer_idx = getResLayerIdx(net_idx, exchange, layer_table, res, fallback_layer_idx, strict);
    if (!layer_idx.has_value()) {
      if (strict) {
        return std::nullopt;
      }
      continue;
    }
    GTLPointInt& first_point = net_topo.node_list[first_node_it->second.get_local_node_idx()].get_point();
    GTLPointInt& second_point = net_topo.node_list[second_node_it->second.get_local_node_idx()].get_point();
    TopoEdge edge(net_idx);
    edge.set_layer_idx(*layer_idx);
    if (geom::isLowerLeft(first_point, second_point)) {
      edge.set_start_node_idx(first_node_it->second.get_local_node_idx());
      edge.set_end_node_idx(second_node_it->second.get_local_node_idx());
    } else {
      edge.set_start_node_idx(second_node_it->second.get_local_node_idx());
      edge.set_end_node_idx(first_node_it->second.get_local_node_idx());
    }
    edge.set_shape(res.geometry.has_box ? getEdgeRect(res.geometry, first_point, second_point, layout_data.get_dbu_per_micron())
                                        : getFallbackEdgeRect(first_point, second_point));
    bool node_layer_diff = first_node_it->second.get_layer_idx() != second_node_it->second.get_layer_idx();
    bool is_routing_layer = layout_data.get_routing_layer_map().find(*layer_idx) != layout_data.get_routing_layer_map().end();
    if (!is_routing_layer && (node_layer_diff || res.geometry.has_area)) {
      edge.set_via_name("spef_via");
    }
    net_topo.edge_list.push_back(std::move(edge));
  }
  return net_topo;
}

void remapEdgeNodeIdx(TopoPool& topo_pool, int32_t net_num)
{
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    std::span<TopoEdge> edge_list = topo_pool.get_net_edge_list(net_idx);
    for (TopoEdge& edge : edge_list) {
      if (edge.get_start_node_idx() < 0 || edge.get_end_node_idx() < 0) {
        continue;
      }
      edge.set_start_node_idx(topo_pool.get_node_idx(net_idx, edge.get_start_node_idx()));
      edge.set_end_node_idx(topo_pool.get_node_idx(net_idx, edge.get_end_node_idx()));
    }
  }
}

}  // namespace ircx::run_rcx_from_topology::detail

namespace ircx::run_rcx_from_topology {

bool SpefTopologyBuilder::build(LayoutData& layout_data, LayerTable& layer_table, const std::string& spef_file_path, bool strict)
{
  int32_t net_num = layout_data.get_regular_net_num();
  if (net_num == 0) {
    LOG_ERROR << "run_rcx_from_topology failed: layout data is empty.";
    return false;
  }
  if (spef_file_path.empty()) {
    LOG_ERROR << "run_rcx_from_topology failed: SPEF file is empty.";
    return false;
  }

  spef::SpefReader reader;
  if (!reader.read(spef_file_path)) {
    LOG_ERROR << "run_rcx_from_topology failed: read SPEF failed: " << spef_file_path;
    return false;
  }
  reader.expandName();
  const spef::Exchange* exchange = reader.getSpefFile();
  if (exchange == nullptr) {
    LOG_ERROR << "run_rcx_from_topology failed: parser returned null exchange.";
    return false;
  }

  std::unordered_map<std::string, int32_t> layout_net_name_to_idx_map = detail::getLayoutNetIdxMap(layout_data);
  std::vector<detail::NetTopo> net_topo_list(net_num);
  for (const spef::Net& spef_net : exchange->nets) {
    std::unordered_map<std::string, int32_t>::iterator net_idx_it = layout_net_name_to_idx_map.find(detail::getNormalizedSpefName(spef_net.name));
    if (net_idx_it == layout_net_name_to_idx_map.end()) {
      LOG_ERROR << (strict ? "run_rcx_from_topology failed" : "run_rcx_from_topology warning")
                << ": net not found in layout, net=" << spef_net.name << ".";
      if (strict) {
        return false;
      }
      continue;
    }
    std::optional<detail::NetTopo> net_topo = detail::buildNetTopo(layout_data, layer_table, *exchange, spef_net, net_idx_it->second, strict);
    if (!net_topo.has_value()) {
      return false;
    }
    net_topo_list[net_idx_it->second] = std::move(*net_topo);
  }

  int32_t node_num = 0;
  int32_t edge_num = 0;
  for (detail::NetTopo& net_topo : net_topo_list) {
    node_num += static_cast<int32_t>(net_topo.node_list.size());
    edge_num += static_cast<int32_t>(net_topo.edge_list.size());
  }
  if (edge_num == 0) {
    LOG_ERROR << "run_rcx_from_topology failed: no topology edges were built from " << spef_file_path << ".";
    return false;
  }

  _topo_pool.reserve(net_num, node_num, edge_num);
  for (detail::NetTopo& net_topo : net_topo_list) {
    _topo_pool.add_net(std::move(net_topo.node_list), std::move(net_topo.edge_list));
  }
  detail::remapEdgeNodeIdx(_topo_pool, net_num);
  LOG_INFO << "run_rcx_from_topology built " << node_num << " nodes and " << edge_num << " edges from " << spef_file_path;
  return true;
}

}  // namespace ircx::run_rcx_from_topology
