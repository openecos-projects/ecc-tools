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
 * @file PlotSpefLypWriter.cc
 * @brief plot_spef implementation detail.
 */
#include "lyp/PlotSpefLypWriter.hh"

#include <algorithm>
#include <fstream>
#include <set>
#include <vector>

#include "config/PlotSpefConfig.hh"
#include "FormatUtils.hh"
#include "PathUtils.hh"
#include "StringUtils.hh"
#include "Logger.hpp"
#include "model/PlotSpefGdsType.hh"
#include "model/PlotSpefModel.hh"
#include "model/PlotSpefVisibility.hh"

namespace ircx::plot_spef {
namespace {

struct LayerProperty
{
  int layer = 0;
  int data_type = 0;
  std::string name;
  std::string frame_color;
  std::string fill_color;
  int dither_pattern = 0;
  int line_style = 0;
  int width = 1;
  bool filled = true;
};

constexpr const char* kMacaronColors[] = {
    "#FADADD", "#F8BBD0", "#F6D6E8", "#EADCF8", "#DCCEF8",
    "#D7E3FC", "#CDE7F0", "#BDE0FE", "#A2D2FF", "#A8DADC",
    "#B2F7EF", "#B8F2E6", "#CDEAC0", "#D0F4DE", "#E9F5DB",
    "#F1F7B5", "#FFF3B0", "#FFF9C4", "#FFE5B4", "#FFD6A5",
    "#FBC4AB", "#FFADAD", "#FFCBD1", "#F7C6C7", "#DDBEA9",
    "#D7CCC8", "#E2F0CB", "#B5EAD7", "#C7CEEA", "#C3B1E1",
    "#F1C0E8", "#CFBAF0", "#A3C4F3", "#90DBF4", "#8EECF5",
    "#98F5E1", "#B9FBC0", "#FBF8CC", "#FDE4CF", "#FFCFD2",
    "#E4C1F9", "#D0BFFF", "#C1D3FE", "#ABC4FF", "#CAE9FF",
    "#ADE8F4", "#CAF0F8", "#B7E4C7", "#D8F3DC", "#EDEEC9",
    "#FFF1A8", "#FFE8A3", "#FFD6BA", "#FFB4A2", "#EAC7C7",
    "#E7D8C9", "#D5ECC2", "#C9E4DE", "#C9DAEA", "#DBCDF0",
    "#F2D7EE", "#F0D7A7", "#D8F3F0", "#DCEBFF",
};

constexpr Size kMacaronColorCount = sizeof(kMacaronColors) / sizeof(kMacaronColors[0]);

class VisibilityReader
{
 public:
  explicit VisibilityReader(const Visibility& visibility)
      : visibility_(visibility)
  {
  }

  auto netVisible(Size net_index) const -> bool
  {
    return visibility_.netVisible(net_index);
  }

  auto netContextOnly(Size net_index) const -> bool
  {
    return visibility_.netContextOnly(net_index);
  }

  auto nodeVisible(Size net_index,
                   Size node_index) const -> bool
  {
    return visibility_.nets[net_index].nodeVisible(node_index);
  }

  auto resistorVisible(Size net_index,
                       Size resistor_index) const -> bool
  {
    return visibility_.nets[net_index].resistorVisible(resistor_index);
  }

 private:
  const Visibility& visibility_;
};

auto resistorPlotLayer(const Model& model,
                       const Resistor& resistor) -> int
{
  if (resistor.has_layer) {
    return resistor.layer;
  }

  const auto* node = findNode(model, resistor.node1);
  return node == nullptr ? 0 : node->layer;
}

auto collectLayers(const Model& model,
                   const VisibilityReader& visibility) -> std::vector<int>
{
  std::set<int> layers;
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    if (!visibility.netVisible(net_index)) {
      continue;
    }
    for (Size node_index = 0; node_index < net.nodes.size(); ++node_index) {
      const auto& node = net.nodes[node_index];
      if (!visibility.nodeVisible(net_index, node_index)) {
        continue;
      }
      layers.insert(node.layer);
    }
    for (Size resistor_index = 0; resistor_index < net.resistors.size(); ++resistor_index) {
      const auto& resistor = net.resistors[resistor_index];
      if (visibility.resistorVisible(net_index, resistor_index)) {
        layers.insert(resistorPlotLayer(model, resistor));
      }
    }
  }
  for (const auto& [layer, _] : model.layer_names) {
    layers.insert(layer);
  }
  if (layers.empty()) {
    layers.insert(0);
  }
  return {layers.begin(), layers.end()};
}

auto layerName(const Model& model,
               int layer) -> std::string
{
  const auto it = model.layer_names.find(layer);
  if (it != model.layer_names.end() && !it->second.empty()) {
    return it->second;
  }
  return "Layer" + std::to_string(layer);
}

auto macaronColor(Size index) -> std::string
{
  return kMacaronColors[index % kMacaronColorCount];
}

auto nodeColor(Size color_index) -> std::string
{
  return macaronColor(color_index * 2);
}

auto edgeColor(Size color_index) -> std::string
{
  return macaronColor(color_index * 2 + 1);
}

auto hasVisibleEdgeOnLayer(const Model& model,
                           const VisibilityReader& visibility,
                           int layer) -> bool
{
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    if (!visibility.netVisible(net_index)) {
      continue;
    }
    for (Size resistor_index = 0; resistor_index < net.resistors.size(); ++resistor_index) {
      const auto& resistor = net.resistors[resistor_index];
      if (visibility.resistorVisible(net_index, resistor_index)
          && resistorPlotLayer(model, resistor) == layer) {
        return true;
      }
    }
  }
  return false;
}

auto makePropertiesForLayer(const Model& model,
                            const VisibilityReader& visibility,
                            const Config& config,
                            int layer,
                            Size color_index) -> std::vector<LayerProperty>
{
  const std::string prefix = layerName(model, layer) + "_";
  std::vector<LayerProperty> properties = {
      {.layer = layer,
       .data_type = kNode,
       .name = prefix + "Node",
       .frame_color = nodeColor(color_index),
       .fill_color = nodeColor(color_index),
       .line_style = 0,
       .width = 1,
       .filled = false},
      {.layer = layer,
       .data_type = kTextNode,
       .name = prefix + "TextNode",
       .frame_color = "#0050d8",
       .fill_color = "#0050d8"},
  };
  const bool plot_edge = hasVisibleEdgeOnLayer(model, visibility, layer);
  if (plot_edge) {
    properties.push_back({.layer = layer,
                          .data_type = kEdge,
                          .name = prefix + "Edge",
                          .frame_color = "#000000",
                          .fill_color = edgeColor(color_index),
                          .line_style = 0,
                          .width = 1,
                          .filled = true});
    properties.push_back({.layer = layer,
                          .data_type = kTargetEdge,
                          .name = prefix + "TargetEdge",
                          .frame_color = "#e00000",
                          .fill_color = "#ff3030",
                          .line_style = 0,
                          .width = 2,
                          .filled = true});
  }
  if (config.plotResistance()) {
    properties.push_back({.layer = layer,
                          .data_type = kTextRes,
                          .name = prefix + "TextRes",
                          .frame_color = "#b000e8",
                          .fill_color = "#b000e8"});
  }
  if (config.plotGroundCap()) {
    properties.push_back({.layer = layer,
                          .data_type = kTextCg,
                          .name = prefix + "TextCg",
                          .frame_color = "#008a00",
                          .fill_color = "#008a00"});
  }
  if (config.plotCouplingCap()) {
    properties.push_back({.layer = layer,
                          .data_type = kCc,
                          .name = prefix + "Cc",
                          .frame_color = "#ff9900",
                          .fill_color = "#ff9900",
                          .line_style = 2,
                          .width = 1,
                          .filled = false});
    properties.push_back({.layer = layer,
                          .data_type = kTextCc,
                          .name = prefix + "TextCc",
                          .frame_color = "#d06b00",
                          .fill_color = "#d06b00"});
  }
  return properties;
}

auto makeProperties(const Model& model,
                    const VisibilityReader& visibility,
                    const Config& config) -> std::vector<LayerProperty>
{
  std::vector<LayerProperty> properties;
  const auto layers = collectLayers(model, visibility);
  for (Size index = 0; index < layers.size(); ++index) {
    const auto layer_properties = makePropertiesForLayer(model, visibility, config, layers[index], index);
    properties.insert(properties.end(), layer_properties.begin(), layer_properties.end());
  }
  return properties;
}

auto writeProperty(std::ostream& os,
                   const LayerProperty& property) -> void
{
  os << "<properties>\n";
  os << "<frame-color>" << property.frame_color << "</frame-color>\n";
  os << "<fill-color>" << property.fill_color << "</fill-color>\n";
  os << "<frame-brightness>0</frame-brightness>\n";
  os << "<fill-brightness>0</fill-brightness>\n";
  os << "<dither-pattern>I" << property.dither_pattern << "</dither-pattern>\n";
  os << "<line-style>I" << property.line_style << "</line-style>\n";
  os << "<valid>true</valid>\n";
  os << "<visible>true</visible>\n";
  os << "<transparent>false</transparent>\n";
  os << "<width>" << property.width << "</width>\n";
  os << "<marked>false</marked>\n";
  os << "<xfill>" << (property.filled ? "false" : "true") << "</xfill>\n";
  os << "<animation>0</animation>\n";
  os << "<name>" << format::escapeXml(property.name) << "</name>\n";
  os << "<source>" << property.layer << "/" << property.data_type << "@1</source>\n";
  os << "</properties>\n";
}

}  // namespace

auto LypWriter::write(const Model& model,
                      const Visibility& visibility,
                      const Config& config) const -> bool
{
  const auto lyp_file = path::fileUnderDir(
      config.output_dir,
      string::identifier(model.design_name, "plot_spef"),
      ".lyp");
  std::ofstream os(lyp_file);
  if (!os.is_open()) {
    RCXLOG.warn(Loc::current(), "plot_spef failed: cannot open LYP output file ", lyp_file.string());
    return false;
  }

  os << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
  os << "<layer-properties>\n";
  const VisibilityReader visibility_reader(visibility);
  for (const auto& property : makeProperties(model, visibility_reader, config)) {
    writeProperty(os, property);
  }
  os << "</layer-properties>\n";

  RCXLOG.info(Loc::current(), "plot_spef wrote LYP to ", lyp_file.string());
  return os.good();
}

}  // namespace ircx::plot_spef
