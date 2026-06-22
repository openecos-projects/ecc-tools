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
#include "lyp/PlotSpefLypWriter.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "FormatUtils.hh"
#include "log/Log.hh"
#include "model/PlotSpefGdsType.hh"

namespace ircx::plot_spef {
namespace {

struct LayerProperty
{
  int layer = 0;
  int data_type = 0;
  std::string name;
  std::string color;
  int dither_pattern = 0;
  int line_style = 0;
  int width = 1;
  bool filled = true;
};

auto collectLayers(const Model& model) -> std::vector<int>
{
  std::set<int> layers;
  for (const auto& net : model.nets) {
    for (const auto& node : net.nodes) {
      layers.insert(node.layer);
    }
    for (const auto& resistor : net.resistors) {
      if (resistor.has_layer) {
        layers.insert(resistor.layer);
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

auto layerName(const Model& model, int layer) -> std::string
{
  const auto it = model.layer_names.find(layer);
  if (it != model.layer_names.end() && !it->second.empty()) {
    return it->second;
  }
  return "Layer" + std::to_string(layer);
}

auto makePropertiesForLayer(const Model& model, const Config& config, int layer) -> std::vector<LayerProperty>
{
  const std::string prefix = layerName(model, layer) + "_";
  std::vector<LayerProperty> properties = {
      {.layer = layer, .data_type = kNode, .name = prefix + "Node", .color = "#0066ff", .line_style = 0, .width = 2, .filled = false},
      {.layer = layer, .data_type = kTextNode, .name = prefix + "TextNode", .color = "#0050d8"},
  };
  if (config.plotResistance()) {
    properties.push_back({.layer = layer, .data_type = kEdge, .name = prefix + "Edge", .color = "#ff2d00", .line_style = 0, .width = 5, .filled = false});
    properties.push_back({.layer = layer, .data_type = kTextRes, .name = prefix + "TextRes", .color = "#b000e8"});
  }
  if (config.plotGroundCap()) {
    properties.push_back({.layer = layer, .data_type = kTextCg, .name = prefix + "TextCg", .color = "#008a00"});
  }
  if (config.plotCouplingCap()) {
    properties.push_back({.layer = layer, .data_type = kCc, .name = prefix + "Cc", .color = "#ff9900", .line_style = 2, .width = 2, .filled = false});
    properties.push_back({.layer = layer, .data_type = kTextCc, .name = prefix + "TextCc", .color = "#d06b00"});
  }
  return properties;
}

auto makeProperties(const Model& model, const Config& config) -> std::vector<LayerProperty>
{
  std::vector<LayerProperty> properties;
  for (const int layer : collectLayers(model)) {
    const auto layer_properties = makePropertiesForLayer(model, config, layer);
    properties.insert(properties.end(), layer_properties.begin(), layer_properties.end());
  }
  return properties;
}

auto writeProperty(std::ostream& os, const LayerProperty& property) -> void
{
  os << "<properties>\n";
  os << "<frame-color>" << property.color << "</frame-color>\n";
  os << "<fill-color>" << property.color << "</fill-color>\n";
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
  os << "<name>" << format::escape_xml(property.name) << "</name>\n";
  os << "<source>" << property.layer << "/" << property.data_type << "@1</source>\n";
  os << "</properties>\n";
}

}  // namespace

auto LypWriter::getLypFile(const std::string& output_file) -> std::string
{
  auto path = std::filesystem::path(output_file);
  path.replace_extension(".lyp");
  return path.string();
}

auto LypWriter::write(const Model& model, const Config& config) const -> bool
{
  const auto lyp_file = getLypFile(config.output_file);
  std::ofstream os(lyp_file);
  if (!os.is_open()) {
    LOG_ERROR << "plot_spef failed: cannot open LYP output file " << lyp_file;
    return false;
  }

  os << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
  os << "<layer-properties>\n";
  for (const auto& property : makeProperties(model, config)) {
    writeProperty(os, property);
  }
  os << "</layer-properties>\n";

  LOG_INFO << "plot_spef wrote LYP to " << lyp_file;
  return os.good();
}

}  // namespace ircx::plot_spef
