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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file SpefTopologyBuilder.cc
 * @brief Build iRCX topology from StarRC SPEF connectivity and annotations.
 */
#include "topology/SpefTopologyBuilder.hh"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Geometry.hh"
#include "LayerTable.hh"
#include "LayoutData.hh"
#include "SpefParser.hh"
#include "TopoPool.hh"
#include "log/Log.hh"

namespace ircx::run_rcx_from_topology {
namespace {

struct NetTopo
{
  std::vector<TopoNode> nodes;
  std::vector<TopoEdge> edges;
};

struct NodeIndex
{
  Size local_id{kMaxSize};
  Size layer_id{kMaxSize};
};

auto hasCoord(const spef::Coord& coord) -> bool
{
  return coord.x >= 0.0 && coord.y >= 0.0;
}

auto normalizeSpefName(const std::string& name) -> std::string
{
  return spef::removeEscapes(spef::stripQuotes(name));
}

auto coordToPoint(const spef::Coord& coord,
                  Dbu dbu_per_micron) -> GtlPointI
{
  return GtlPointI{
      unit::toDbu(coord.x, dbu_per_micron),
      unit::toDbu(coord.y, dbu_per_micron)};
}

auto geometryCenter(const spef::GeometryAttr& geometry,
                    Dbu dbu_per_micron) -> GtlPointI
{
  const Micron x = (geometry.ll_coordinate.x + geometry.ur_coordinate.x) / 2.0;
  const Micron y = (geometry.ll_coordinate.y + geometry.ur_coordinate.y) / 2.0;
  return GtlPointI{unit::toDbu(x, dbu_per_micron), unit::toDbu(y, dbu_per_micron)};
}

auto makeRectFromGeometry(const spef::GeometryAttr& geometry,
                          Dbu dbu_per_micron) -> GtlRectI
{
  const Dbu lx = unit::toDbu(
      std::min(geometry.ll_coordinate.x, geometry.ur_coordinate.x),
      dbu_per_micron);
  const Dbu ly = unit::toDbu(
      std::min(geometry.ll_coordinate.y, geometry.ur_coordinate.y),
      dbu_per_micron);
  const Dbu hx = unit::toDbu(
      std::max(geometry.ll_coordinate.x, geometry.ur_coordinate.x),
      dbu_per_micron);
  const Dbu hy = unit::toDbu(
      std::max(geometry.ll_coordinate.y, geometry.ur_coordinate.y),
      dbu_per_micron);
  return geom::makeRect<GtlRectI>(lx, ly, hx, hy);
}

auto annotationWidthDbu(const spef::GeometryAttr& geometry,
                        Dbu dbu_per_micron) -> Dbu
{
  if (geometry.has_width && geometry.width > 0.0) {
    return std::max<Dbu>(unit::toDbu(geometry.width, dbu_per_micron), 2);
  }
  return 2;
}

void expandRangeToWidth(Dbu& lo,
                        Dbu& hi,
                        Dbu width)
{
  const Dbu center = lo + (hi - lo) / 2;
  lo = center - width / 2;
  hi = lo + width;
  if (hi <= lo) {
    hi = lo + 1;
  }
}

auto makeEdgeRectFromGeometry(const spef::GeometryAttr& geometry,
                              const GtlPointI& u,
                              const GtlPointI& v,
                              Dbu dbu_per_micron) -> GtlRectI
{
  Dbu lx = unit::toDbu(
      std::min(geometry.ll_coordinate.x, geometry.ur_coordinate.x),
      dbu_per_micron);
  Dbu ly = unit::toDbu(
      std::min(geometry.ll_coordinate.y, geometry.ur_coordinate.y),
      dbu_per_micron);
  Dbu hx = unit::toDbu(
      std::max(geometry.ll_coordinate.x, geometry.ur_coordinate.x),
      dbu_per_micron);
  Dbu hy = unit::toDbu(
      std::max(geometry.ll_coordinate.y, geometry.ur_coordinate.y),
      dbu_per_micron);

  if (hx <= lx || hy <= ly) {
    const Dbu width = annotationWidthDbu(geometry, dbu_per_micron);
    const bool is_horz = geom::isHorizontalDominant(u, v);
    if (is_horz) {
      if (hx <= lx) {
        lx = std::min(geom::x(u), geom::x(v));
        hx = std::max(geom::x(u), geom::x(v));
      }
      if (hy <= ly) {
        expandRangeToWidth(ly, hy, width);
      }
    } else {
      if (hy <= ly) {
        ly = std::min(geom::y(u), geom::y(v));
        hy = std::max(geom::y(u), geom::y(v));
      }
      if (hx <= lx) {
        expandRangeToWidth(lx, hx, width);
      }
    }
    if (hx <= lx) {
      expandRangeToWidth(lx, hx, width);
    }
    if (hy <= ly) {
      expandRangeToWidth(ly, hy, width);
    }
  }

  return geom::makeRect<GtlRectI>(lx, ly, hx, hy);
}

auto makeFallbackEdgeRect(const GtlPointI& u,
                          const GtlPointI& v) -> GtlRectI
{
  if (geom::x(u) == geom::x(v) && geom::y(u) == geom::y(v)) {
    return geom::boxAround(u, 1);
  }

  if (geom::isHorizontalDominant(u, v)) {
    const Dbu y = geom::y(u) + (geom::y(v) - geom::y(u)) / 2;
    return geom::makeRect<GtlRectI>(
        std::min(geom::x(u), geom::x(v)),
        y - 1,
        std::max(geom::x(u), geom::x(v)),
        y + 1);
  }

  const Dbu x = geom::x(u) + (geom::x(v) - geom::x(u)) / 2;
  return geom::makeRect<GtlRectI>(
      x - 1,
      std::min(geom::y(u), geom::y(v)),
      x + 1,
      std::max(geom::y(u), geom::y(v)));
}

auto makeLayoutNetMap(const LayoutData& layout) -> std::unordered_map<std::string, Size>
{
  std::unordered_map<std::string, Size> net_map;
  net_map.reserve(layout.net_vec.size());
  for (Size net_idx = 0; net_idx < layout.net_vec.size(); ++net_idx) {
    const Net& net = layout.net_vec[net_idx];
    if (net.name.empty()) {
      continue;
    }
    net_map[normalizeSpefName(net.name)] = net_idx;
  }
  return net_map;
}

auto makeLayoutPinMap(const Net& net) -> std::unordered_map<std::string, std::string>
{
  std::unordered_map<std::string, std::string> pin_map;
  pin_map.reserve(net.pins.size());
  for (const Pin& pin : net.pins) {
    pin_map[normalizeSpefName(pin.name)] = pin.name;
  }
  return pin_map;
}

auto resolveLayer(Size net_id,
                  const spef::Exchange& exchange,
                  const LayerTable& layer_table,
                  int annotation_layer,
                  bool strict) -> std::optional<Size>
{
  const auto layer_it = exchange.layer_map.find(annotation_layer);
  if (layer_it != exchange.layer_map.end()) {
    try {
      return layer_table.designId(layer_it->second.layer_name);
    } catch (const std::out_of_range&) {
      LOG_ERROR << "run_rcx_from_topology warning: layer map entry not found "
                << "in design layers, net_idx=" << net_id
                << ", annotation_layer=" << annotation_layer
                << ", layer_name=" << layer_it->second.layer_name << ".";
      return std::nullopt;
    }
  }

  if (annotation_layer < 0) {
    if (strict) {
      LOG_ERROR << "run_rcx_from_topology failed: invalid annotation layer "
                << annotation_layer << ", net_idx=" << net_id << ".";
    }
    return std::nullopt;
  }

  try {
    static_cast<void>(layer_table.designName(static_cast<Size>(annotation_layer)));
    return static_cast<Size>(annotation_layer);
  } catch (const std::out_of_range&) {
    if (strict) {
      LOG_ERROR << "run_rcx_from_topology failed: missing layer map for "
                << "annotation layer " << annotation_layer
                << ", net_idx=" << net_id << ".";
    }
  }
  return std::nullopt;
}

auto connLayer(Size net_id,
               const spef::Exchange& exchange,
               const LayerTable& layer_table,
               const spef::ConnEntry& conn,
               bool strict) -> std::optional<Size>
{
  if (conn.geometry.has_layer) {
    return resolveLayer(net_id, exchange, layer_table, conn.geometry.layer, strict);
  }
  if (conn.layer > 0) {
    return resolveLayer(net_id, exchange, layer_table, conn.layer, strict);
  }
  if (strict) {
    LOG_ERROR << "run_rcx_from_topology failed: node missing layer, net_idx="
              << net_id << ", node=" << conn.pin_port_name << ".";
  }
  return std::nullopt;
}

auto resLayer(Size net_id,
              const spef::Exchange& exchange,
              const LayerTable& layer_table,
              const spef::ResCap& res,
              Size fallback_layer,
              bool strict) -> std::optional<Size>
{
  if (res.geometry.has_layer) {
    return resolveLayer(net_id, exchange, layer_table, res.geometry.layer, strict);
  }
  if (fallback_layer != kMaxSize) {
    return fallback_layer;
  }
  if (strict) {
    LOG_ERROR << "run_rcx_from_topology failed: resistor missing layer, net_idx="
              << net_id << ", node1=" << res.node1 << ", node2=" << res.node2 << ".";
  }
  return std::nullopt;
}

auto connPoint(const spef::ConnEntry& conn,
               Dbu dbu_per_micron) -> std::optional<GtlPointI>
{
  if (hasCoord(conn.coordinate)) {
    return coordToPoint(conn.coordinate, dbu_per_micron);
  }
  if (conn.geometry.has_box) {
    return geometryCenter(conn.geometry, dbu_per_micron);
  }
  return std::nullopt;
}

auto shouldTreatAsPin(const spef::ConnEntry& conn) -> bool
{
  if (conn.conn_type == spef::ConnectionType::kExternal) {
    return true;
  }
  return conn.conn_direction == spef::ConnectionDirection::kInput
         || conn.conn_direction == spef::ConnectionDirection::kOutput
         || conn.conn_direction == spef::ConnectionDirection::kInout;
}

auto buildOneNet(const LayoutData& layout,
                 const LayerTable& layer_table,
                 const spef::Exchange& exchange,
                 const spef::Net& spef_net,
                 Size net_id,
                 bool strict) -> std::optional<NetTopo>
{
  NetTopo result;
  result.nodes.reserve(spef_net.conns.size());
  result.edges.reserve(spef_net.ress.size());

  std::unordered_map<std::string, NodeIndex> node_index_by_name;
  node_index_by_name.reserve(spef_net.conns.size());
  const auto pin_map = makeLayoutPinMap(layout.net_vec[net_id]);

  for (const spef::ConnEntry& conn : spef_net.conns) {
    const spef::GeometryAttr& geometry = conn.geometry;
    const std::string node_name = normalizeSpefName(conn.pin_port_name);
    if (node_index_by_name.contains(node_name)) {
      continue;
    }

    const auto layer_id = connLayer(net_id, exchange, layer_table, conn, strict);
    const auto point = connPoint(conn, layout.dbu_per_micron);
    if (!layer_id.has_value() || !point.has_value()) {
      if (strict) {
        LOG_ERROR << "run_rcx_from_topology failed: invalid node geometry, net="
                  << spef_net.name << ", node=" << conn.pin_port_name << ".";
        return std::nullopt;
      }
      continue;
    }

    TopoNode node(net_id);
    node.set_layer_id(*layer_id);
    node.set_point(*point);
    if (geometry.has_box) {
      node.set_shape(makeRectFromGeometry(geometry, layout.dbu_per_micron));
    } else {
      node.set_shape(geom::boxAround(*point, 1));
    }

    if (shouldTreatAsPin(conn)) {
      const auto pin_it = pin_map.find(node_name);
      if (pin_it != pin_map.end()) {
        node.set_pin_name(pin_it->second);
      }
    }

    const Size local_id = result.nodes.size();
    result.nodes.push_back(std::move(node));
    node_index_by_name.emplace(node_name, NodeIndex{local_id, *layer_id});
  }

  for (const spef::ResCap& res : spef_net.ress) {
    const spef::GeometryAttr& geometry = res.geometry;
    const std::string node1 = normalizeSpefName(res.node1);
    const std::string node2 = normalizeSpefName(res.node2);
    const auto node1_it = node_index_by_name.find(node1);
    const auto node2_it = node_index_by_name.find(node2);
    if (node1_it == node_index_by_name.end() || node2_it == node_index_by_name.end()) {
      if (strict) {
        LOG_ERROR << "run_rcx_from_topology failed: resistor endpoint missing "
                  << "from *CONN, net=" << spef_net.name
                  << ", node1=" << res.node1 << ", node2=" << res.node2 << ".";
        return std::nullopt;
      }
      continue;
    }

    const Size fallback_layer = node1_it->second.layer_id == node2_it->second.layer_id
                                    ? node1_it->second.layer_id
                                    : kMaxSize;
    const auto layer_id = resLayer(
        net_id,
        exchange,
        layer_table,
        res,
        fallback_layer,
        strict);
    if (!layer_id.has_value()) {
      if (strict) {
        return std::nullopt;
      }
      continue;
    }

    const GtlPointI& u_point = result.nodes[node1_it->second.local_id].get_point();
    const GtlPointI& v_point = result.nodes[node2_it->second.local_id].get_point();

    TopoEdge edge(net_id);
    edge.set_layer_id(*layer_id);
    if (geom::isLowerLeft(u_point, v_point)) {
      edge.set_u(node1_it->second.local_id);
      edge.set_v(node2_it->second.local_id);
    } else {
      edge.set_u(node2_it->second.local_id);
      edge.set_v(node1_it->second.local_id);
    }

    if (geometry.has_box) {
      edge.set_shape(makeEdgeRectFromGeometry(geometry, u_point, v_point, layout.dbu_per_micron));
    } else {
      edge.set_shape(makeFallbackEdgeRect(u_point, v_point));
    }

    const bool node_layers_differ = node1_it->second.layer_id != node2_it->second.layer_id;
    const bool non_routing_layer = !layout.routing_layers.contains(*layer_id);
    if (non_routing_layer && (node_layers_differ || geometry.has_area)) {
      edge.set_via_name("spef_via");
    }

    result.edges.push_back(std::move(edge));
  }

  return result;
}

void remapEdgesToGlobalNodeIds(TopoPool& topo_pool,
                               Size net_count)
{
  for (Size net_idx = 0; net_idx < net_count; ++net_idx) {
    auto edges = topo_pool.get_net_edges(net_idx);
    for (TopoEdge& edge : edges) {
      if (edge.get_u() == kMaxSize || edge.get_v() == kMaxSize) {
        continue;
      }
      edge.set_u(topo_pool.get_node_index(net_idx, edge.get_u()));
      edge.set_v(topo_pool.get_node_index(net_idx, edge.get_v()));
    }
  }
}

}  // namespace

auto SpefTopologyBuilder::build(const LayoutData& layout,
                                const LayerTable& layer_table,
                                const std::string& spef_file,
                                bool strict) const -> bool
{
  if (topo_pool_ == nullptr) {
    return false;
  }
  if (layout.get_regular_net_count() == 0) {
    LOG_ERROR << "run_rcx_from_topology failed: layout data is empty.";
    return false;
  }
  if (spef_file.empty()) {
    LOG_ERROR << "run_rcx_from_topology failed: SPEF file is empty.";
    return false;
  }

  spef::SpefReader reader;
  if (!reader.read(spef_file)) {
    LOG_ERROR << "run_rcx_from_topology failed: read SPEF failed: " << spef_file;
    return false;
  }
  reader.expandName();

  const spef::Exchange* exchange = reader.getSpefFile();
  if (exchange == nullptr) {
    LOG_ERROR << "run_rcx_from_topology failed: parser returned null exchange.";
    return false;
  }

  const auto layout_net_map = makeLayoutNetMap(layout);
  const Size net_count = layout.get_regular_net_count();
  std::vector<NetTopo> net_topologies(net_count);

  for (Size spef_net_idx = 0; spef_net_idx < exchange->nets.size(); ++spef_net_idx) {
    const spef::Net& spef_net = exchange->nets[spef_net_idx];
    const std::string normalized_net_name = normalizeSpefName(spef_net.name);
    const auto net_it = layout_net_map.find(normalized_net_name);
    if (net_it == layout_net_map.end()) {
      const char* message = strict
                                ? "run_rcx_from_topology failed"
                                : "run_rcx_from_topology warning";
      LOG_ERROR << message << ": net not found in layout, net=" << spef_net.name << ".";
      if (strict) {
        return false;
      }
      continue;
    }

    const Size net_id = net_it->second;
    auto net_topology = buildOneNet(
        layout,
        layer_table,
        *exchange,
        spef_net,
        net_id,
        strict);
    if (!net_topology.has_value()) {
      return false;
    }
    net_topologies[net_id] = std::move(*net_topology);
  }

  Size total_nodes = 0;
  Size total_edges = 0;
  for (const NetTopo& net_topology : net_topologies) {
    total_nodes += net_topology.nodes.size();
    total_edges += net_topology.edges.size();
  }
  if (total_edges == 0) {
    LOG_ERROR << "run_rcx_from_topology failed: no topology edges were built from "
              << spef_file << ".";
    return false;
  }

  topo_pool_->reserve(net_count, total_nodes, total_edges);
  for (NetTopo& net_topology : net_topologies) {
    topo_pool_->addNet(std::move(net_topology.nodes), std::move(net_topology.edges));
  }
  remapEdgesToGlobalNodeIds(*topo_pool_, net_count);

  LOG_INFO << "run_rcx_from_topology built " << total_nodes
           << " nodes and " << total_edges << " edges from " << spef_file;
  return true;
}

}  // namespace ircx::run_rcx_from_topology
