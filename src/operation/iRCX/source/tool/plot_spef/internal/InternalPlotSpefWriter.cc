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
 * @file InternalPlotSpefWriter.cc
 * @brief Direct plot_spef GDS output from iRCX internal RC data.
 */
#include "internal/InternalPlotSpefWriter.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Geometry.hh"
#include "LayerTable.hh"
#include "LayoutData.hh"
#include "PathUtils.hh"
#include "RCTable.hh"
#include "RCXData.hh"
#include "SpefContext.hh"
#include "StringUtils.hh"
#include "TopoPool.hh"
#include "config/PlotSpefConfig.hh"
#include "gds/PlotSpefGdsWriter.hh"
#include "log/Log.hh"
#include "lyp/PlotSpefLypWriter.hh"
#include "model/PlotSpefModel.hh"

namespace ircx {
namespace {

constexpr int kDefaultPlotDbu = 1000;

struct NameMaps
{
  std::unordered_map<std::string, int> net_name_to_id;
  std::unordered_map<std::string, int> port_name_to_id;
  std::unordered_map<std::string, int> inst_name_to_id;
};

auto buildNameMaps(const SpefContext& spef_context) -> NameMaps
{
  NameMaps maps;
  int next_id = 1;
  for (const std::string& name : spef_context.net_names) {
    maps.net_name_to_id[name] = next_id++;
  }
  for (const std::string& name : spef_context.port_names) {
    maps.port_name_to_id[name] = next_id++;
  }
  for (const std::string& name : spef_context.instance_names) {
    maps.inst_name_to_id[name] = next_id++;
  }
  return maps;
}

auto nodeSpefName(const TopoNode& node,
                  const LayoutData& layout,
                  const NameMaps& maps) -> std::string
{
  if (node.is_pin_node()) {
    const std::string& full_name = node.get_pin_name();
    const Size colon_pos = full_name.find(':');
    if (colon_pos == std::string::npos) {
      const auto it = maps.port_name_to_id.find(full_name);
      return it == maps.port_name_to_id.end()
                 ? full_name
                 : "*" + std::to_string(it->second);
    }

    const std::string inst_name = full_name.substr(0, colon_pos);
    const std::string pin_name = full_name.substr(colon_pos + 1);
    const auto it = maps.inst_name_to_id.find(inst_name);
    return it == maps.inst_name_to_id.end()
               ? full_name
               : "*" + std::to_string(it->second) + ":" + pin_name;
  }

  const Size net_id = node.get_net_id();
  const std::string net_name = net_id < layout.net_vec.size()
                                   ? layout.net_vec[net_id].name
                                   : "net" + std::to_string(net_id);
  const auto it = maps.net_name_to_id.find(net_name);
  const std::string net_prefix = it == maps.net_name_to_id.end()
                                     ? "*" + net_name
                                     : "*" + std::to_string(it->second);
  return net_prefix + ":" + std::to_string(node.get_id() + 1);
}

auto buildNodeNames(const TopoPool& topo,
                    const LayoutData& layout,
                    const SpefContext& spef_context) -> std::vector<std::string>
{
  const NameMaps maps = buildNameMaps(spef_context);
  const auto& nodes = topo.get_node_pool();
  std::vector<std::string> names(nodes.size());
  for (const TopoNode& node : nodes) {
    names[topo.get_node_index(node)] = nodeSpefName(node, layout, maps);
  }
  return names;
}

auto layerToInt(Size layer_id) -> int
{
  if (layer_id == kMaxSize || layer_id > static_cast<Size>(std::numeric_limits<int>::max())) {
    return 0;
  }
  return static_cast<int>(layer_id);
}

class CoordScaler
{
 public:
  CoordScaler(Dbu source_dbu,
              int target_dbu)
      : source_dbu_(std::max<Dbu>(source_dbu, 1)),
        target_dbu_(target_dbu > 0 ? target_dbu : kDefaultPlotDbu)
  {
  }

  auto dbu() const -> int { return target_dbu_; }

  auto coord(Dbu value) const -> int
  {
    if (source_dbu_ == target_dbu_) {
      return static_cast<int>(value);
    }

    const auto scaled = std::llround(
        static_cast<F64>(value) * static_cast<F64>(target_dbu_)
        / static_cast<F64>(source_dbu_));
    return clampToInt(scaled);
  }

  auto micron(Dbu value) const -> F64
  {
    return static_cast<F64>(value) / static_cast<F64>(source_dbu_);
  }

 private:
  static auto clampToInt(long long value) -> int
  {
    if (value > std::numeric_limits<int>::max()) {
      return std::numeric_limits<int>::max();
    }
    if (value < std::numeric_limits<int>::min()) {
      return std::numeric_limits<int>::min();
    }
    return static_cast<int>(value);
  }

  Dbu source_dbu_{1};
  int target_dbu_{kDefaultPlotDbu};
};

auto hasBox(int llx,
            int lly,
            int urx,
            int ury) -> bool
{
  return llx != urx && lly != ury;
}

auto endpointName(Size node_idx,
                  const std::vector<std::string>& node_names) -> std::string
{
  return node_idx < node_names.size() ? node_names[node_idx] : std::string{};
}

auto edgeEndpointName(const TopoEdge& edge,
                      const std::vector<std::string>& node_names) -> std::string
{
  std::string name = endpointName(edge.get_u(), node_names);
  return name.empty() ? endpointName(edge.get_v(), node_names) : name;
}

auto buildNode(const TopoNode& topo_node,
               const CoordScaler& scaler,
               const std::string& name) -> plot_spef::Node
{
  plot_spef::Node node;
  node.name = name;
  node.layer = layerToInt(topo_node.get_layer_id());
  node.x = scaler.coord(geom::x(topo_node.get_point()));
  node.y = scaler.coord(geom::y(topo_node.get_point()));
  node.has_point = true;

  const GtlRectI& shape = topo_node.get_shape();
  node.llx = scaler.coord(geom::minX(shape));
  node.lly = scaler.coord(geom::minY(shape));
  node.urx = scaler.coord(geom::maxX(shape));
  node.ury = scaler.coord(geom::maxY(shape));
  node.has_box = hasBox(node.llx, node.lly, node.urx, node.ury);
  return node;
}

auto buildResistor(const TopoEdge& edge,
                   Size edge_idx,
                   F64 resistance,
                   const CoordScaler& scaler,
                   const std::vector<std::string>& node_names) -> plot_spef::Resistor
{
  plot_spef::Resistor resistor;
  resistor.node1 = endpointName(edge.get_u(), node_names);
  resistor.node2 = endpointName(edge.get_v(), node_names);
  resistor.value = resistance;
  resistor.index = edge_idx + 1;
  resistor.length = scaler.micron(edge.get_length());
  resistor.width = scaler.micron(edge.get_width());
  resistor.layer = layerToInt(edge.get_layer_id());
  resistor.direction = edge.is_horz() ? 0 : 1;
  resistor.has_length = true;
  resistor.has_width = true;
  resistor.has_layer = true;
  resistor.has_direction = true;

  const GtlRectI& shape = edge.get_shape();
  resistor.llx = scaler.coord(geom::minX(shape));
  resistor.lly = scaler.coord(geom::minY(shape));
  resistor.urx = scaler.coord(geom::maxX(shape));
  resistor.ury = scaler.coord(geom::maxY(shape));
  resistor.has_box = hasBox(resistor.llx, resistor.lly, resistor.urx, resistor.ury);
  return resistor;
}

auto addNodeIndex(plot_spef::Model& model,
                  Size net_index) -> void
{
  auto& net = model.nets[net_index];
  for (Size node_index = 0; node_index < net.nodes.size(); ++node_index) {
    model.node_refs_by_name[net.nodes[node_index].name] = plot_spef::NodeRef{
        .net_index = net_index,
        .node_index = node_index,
        .valid = true};
  }
}

auto makeEdgeRef(const TopoEdge& edge) -> plot_spef::EdgeRef
{
  return plot_spef::EdgeRef{
      .net_index = edge.get_net_id(),
      .resistor_index = edge.get_id(),
      .valid = true};
}

auto addGroundCaps(plot_spef::Net& net,
                   Size model_net_index,
                   std::span<const F64> gcap_pool,
                   std::span<const TopoEdge> edges,
                   const std::vector<std::string>& node_names) -> void
{
  net.ground_caps.reserve(gcap_pool.size());
  for (Size edge_idx = 0; edge_idx < edges.size() && edge_idx < gcap_pool.size(); ++edge_idx) {
    const F64 cap_ff = gcap_pool[edge_idx];
    if (cap_ff <= 0.0) {
      continue;
    }
    net.ground_caps.push_back(plot_spef::Capacitor{
        .node1 = edgeEndpointName(edges[edge_idx], node_names),
        .value = cap_ff,
        .edge1 = plot_spef::EdgeRef{
            .net_index = model_net_index,
            .resistor_index = edge_idx,
            .valid = true}});
  }
}

auto buildLayerNames(const LayerTable& layer_table) -> std::unordered_map<int, std::string>
{
  std::unordered_map<int, std::string> layer_names;
  for (const auto& [layer_id, layer_name] : layer_table.designLayers()) {
    layer_names[layerToInt(layer_id)] = layer_name;
  }
  return layer_names;
}

auto cornerName(const RCXData::CornerData& corner,
                Size corner_idx) -> std::string
{
  if (corner.process_corner.has_value()
      && !corner.process_corner->get_technology().empty()) {
    return corner.process_corner->get_technology();
  }
  if (!corner.name.empty()) {
    return corner.name;
  }
  return "corner" + std::to_string(corner_idx);
}

class DirectModelBuilder
{
 public:
  DirectModelBuilder(const RCXData& data,
                     const plot_spef::Config& config)
      : data_(data),
        config_(config),
        layout_(data.get_layout()),
        topo_(data.get_topo_pool()),
        rc_table_(data.get_rc_table()),
        node_names_(buildNodeNames(topo_, layout_, data.get_spef_context())),
        scaler_(layout_.dbu_per_micron, config.dbu)
  {
  }

  auto build(Size corner_idx) const -> plot_spef::Model
  {
    plot_spef::Model model;
    model.design_name = layout_.design_name + "_"
                        + cornerName(data_.get_corner_data()[corner_idx], corner_idx);
    model.vendor_name = "ECOS";
    model.program_name = "iRCX";
    model.cap_unit = "FF";
    model.res_unit = "OHM";
    model.dbu = scaler_.dbu();
    model.layer_names = buildLayerNames(data_.get_layer_table());

    const Size net_count = std::min(layout_.get_regular_net_count(), rc_table_.get_net_num());
    model.nets.reserve(net_count);
    model.node_refs_by_name.reserve(node_names_.size());
    for (Size net_idx = 0; net_idx < net_count; ++net_idx) {
      model.nets.push_back(buildNet(corner_idx, net_idx));
      addNodeIndex(model, net_idx);
    }

    appendCouplingCaps(model, corner_idx);
    applyScope(model);
    return model;
  }

 private:
  auto buildNet(Size corner_idx,
                Size net_idx) const -> plot_spef::Net
  {
    plot_spef::Net net;
    net.name = layout_.net_vec[net_idx].name;

    const auto nodes = topo_.get_net_nodes(net_idx);
    net.nodes.reserve(nodes.size());
    net.node_index_by_name.reserve(nodes.size());
    for (const TopoNode& topo_node : nodes) {
      const Size global_node_idx = topo_.get_node_index(net_idx, topo_node.get_id());
      const std::string& node_name = node_names_[global_node_idx];
      net.nodes.push_back(buildNode(topo_node, scaler_, node_name));
      net.node_index_by_name[node_name] = net.nodes.size() - 1;
    }

    const auto edges = topo_.get_net_edges(net_idx);
    const auto res_pool = rc_table_.get_corner_net_res_pool({corner_idx, net_idx});
    net.resistors.reserve(edges.size());
    for (Size edge_idx = 0; edge_idx < edges.size(); ++edge_idx) {
      const F64 resistance = edge_idx < res_pool.size() ? res_pool[edge_idx] : 0.0;
      net.resistors.push_back(
          buildResistor(edges[edge_idx], edge_idx, resistance, scaler_, node_names_));
    }

    if (config_.plotGroundCap()) {
      const auto gcap_pool = rc_table_.get_corner_net_gcap_pool({corner_idx, net_idx});
      addGroundCaps(net, net_idx, gcap_pool, edges, node_names_);
    }
    return net;
  }

  auto appendCouplingCaps(plot_spef::Model& model,
                          Size corner_idx) const -> void
  {
    if (!config_.plotCouplingCap() && !config_.hasNetFilter()) {
      return;
    }

    const auto& edge_pool = topo_.get_edge_pool();
    for (const auto& [key, cap_vec] : rc_table_.get_merged_ccap()) {
      if (corner_idx >= cap_vec.size()) {
        continue;
      }

      const F64 cap_ff = static_cast<F64>(cap_vec[corner_idx]);
      if (cap_ff <= 0.0) {
        continue;
      }

      const Size edge_a_idx = key.first;
      const Size edge_b_idx = key.second;
      if (edge_a_idx >= edge_pool.size() || edge_b_idx >= edge_pool.size()) {
        continue;
      }

      const TopoEdge& edge_a = edge_pool[edge_a_idx];
      const TopoEdge& edge_b = edge_pool[edge_b_idx];
      if (edge_a.get_net_id() >= model.nets.size()
          || edge_b.get_net_id() >= model.nets.size()) {
        continue;
      }

      const auto ref_a = makeEdgeRef(edge_a);
      const auto ref_b = makeEdgeRef(edge_b);
      const std::string node_a = edgeEndpointName(edge_a, node_names_);
      const std::string node_b = edgeEndpointName(edge_b, node_names_);

      model.nets[edge_a.get_net_id()].coupling_caps.push_back(plot_spef::Capacitor{
          .node1 = node_a,
          .node2 = node_b,
          .value = cap_ff,
          .edge1 = ref_a,
          .edge2 = ref_b});

      if (edge_b.get_net_id() != edge_a.get_net_id()) {
        model.nets[edge_b.get_net_id()].coupling_caps.push_back(plot_spef::Capacitor{
            .node1 = node_b,
            .node2 = node_a,
            .value = cap_ff,
            .edge1 = ref_b,
            .edge2 = ref_a});
      }
    }
  }

  auto applyScope(plot_spef::Model& model) const -> void
  {
    const std::string target_net = string::trim(config_.net_name);
    if (target_net.empty()) {
      for (auto& net : model.nets) {
        net.visible = true;
        net.context_only = false;
        for (auto& node : net.nodes) {
          node.visible = true;
        }
        for (auto& resistor : net.resistors) {
          resistor.visible = true;
        }
      }
      return;
    }

    Size target_net_index = kMaxSize;
    for (Size net_idx = 0; net_idx < model.nets.size(); ++net_idx) {
      auto& net = model.nets[net_idx];
      const bool is_target = net.name == target_net;
      if (is_target) {
        target_net_index = net_idx;
      }
      net.visible = is_target;
      net.context_only = !is_target;
      for (auto& node : net.nodes) {
        node.visible = is_target;
      }
      for (auto& resistor : net.resistors) {
        resistor.visible = is_target;
      }
    }

    if (target_net_index == kMaxSize) {
      LOG_ERROR << "plot_spef warning: target net not found: " << target_net;
      return;
    }

    for (const auto& net : model.nets) {
      for (const auto& cap : net.coupling_caps) {
        if (cap.edge1.net_index == target_net_index || cap.edge2.net_index == target_net_index) {
          showEdge(model, cap.edge1);
          showEdge(model, cap.edge2);
        }
      }
    }
  }

  static auto showEdge(plot_spef::Model& model,
                       const plot_spef::EdgeRef& edge_ref) -> void
  {
    if (!edge_ref.valid || edge_ref.net_index >= model.nets.size()) {
      return;
    }

    auto& net = model.nets[edge_ref.net_index];
    net.visible = true;
    if (edge_ref.resistor_index >= net.resistors.size()) {
      return;
    }

    auto& resistor = net.resistors[edge_ref.resistor_index];
    resistor.visible = true;
    showNode(model, resistor.node1);
    showNode(model, resistor.node2);
  }

  static auto showNode(plot_spef::Model& model,
                       const std::string& node_name) -> void
  {
    if (auto* node = plot_spef::findNode(model, node_name)) {
      node->visible = true;
    }
  }

  const RCXData& data_;
  const plot_spef::Config& config_;
  const LayoutData& layout_;
  const TopoPool& topo_;
  const RCTable& rc_table_;
  std::vector<std::string> node_names_;
  CoordScaler scaler_;
};

auto makePlotSpefConfig(const plot_spef::Config& rcx_config,
                        const LayoutData& layout) -> plot_spef::Config
{
  plot_spef::Config config;
  config.output_dir = rcx_config.output_dir;
  config.net_name = rcx_config.net_name;
  config.dbu = rcx_config.dbu > 0
                   ? rcx_config.dbu
                   : (layout.dbu_per_micron > 0 ? layout.dbu_per_micron : kDefaultPlotDbu);
  config.cores = rcx_config.cores > 0 ? rcx_config.cores : 1;
  config.output_resistance = rcx_config.output_resistance;
  config.output_coupling_cap = rcx_config.output_coupling_cap;
  config.output_ground_cap = rcx_config.output_ground_cap;
  return config;
}

}  // namespace

auto writeInternalPlotSpef(const RCXData& data,
                           const plot_spef::Config& config) -> bool
{
  if (!config.spef_file.empty()) {
    return true;
  }

  if (data.get_corner_data().empty()) {
    LOG_ERROR << "plot_spef failed: process corners not loaded.";
    return false;
  }
  if (data.get_layout().get_regular_net_count() == 0
      || data.get_topo_pool().get_node_pool().empty()
      || data.get_rc_table().get_corner_num() == 0
      || data.get_rc_table().get_net_num() == 0) {
    LOG_ERROR << "plot_spef failed: internal iRCX data is not available.";
    return false;
  }

  const plot_spef::Config plot_config = makePlotSpefConfig(config, data.get_layout());
  if (plot_config.output_dir.empty()) {
    LOG_ERROR << "plot_spef requires an output directory.";
    return false;
  }
  if (plot_config.dbu <= 0) {
    LOG_ERROR << "plot_spef requires a positive DBU.";
    return false;
  }
  if (plot_config.cores <= 0) {
    LOG_ERROR << "plot_spef requires a positive core count.";
    return false;
  }
  if (!path::ensureDir(plot_config.output_dir, "plot_spef output directory")) {
    return false;
  }

  const DirectModelBuilder builder(data, plot_config);
  const plot_spef::GdsWriter gds_writer;
  const plot_spef::LypWriter lyp_writer;
  for (Size corner_idx = 0; corner_idx < data.get_corner_data().size(); ++corner_idx) {
    const plot_spef::Model model = builder.build(corner_idx);
    if (!gds_writer.write(model, plot_config)) {
      return false;
    }
    if (!lyp_writer.write(model, plot_config)) {
      return false;
    }
  }

  LOG_INFO << "plot_spef wrote direct iRCX GDS to " << plot_config.output_dir;
  return true;
}

}  // namespace ircx
