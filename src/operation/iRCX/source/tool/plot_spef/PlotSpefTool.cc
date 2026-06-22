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
#include "PlotSpefTool.hh"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "SpefParser.hh"
#include "StringUtils.hh"
#include "Types.hh"
#include "gds/PlotSpefGdsWriter.hh"
#include "log/Log.hh"
#include "lyp/PlotSpefLypWriter.hh"
#include "model/PlotSpefModel.hh"

namespace ircx {
namespace {

enum class ScanSection
{
  kNone,
  kConn,
  kCap,
  kRes
};

struct ScanGeometry
{
  int layer = 0;
  int llx = 0;
  int lly = 0;
  int urx = 0;
  int ury = 0;
  bool has_layer = false;
  bool has_box = false;
};

auto hasCoord(const spef::Coord& coord) -> bool
{
  return coord.x >= 0.0 && coord.y >= 0.0;
}

auto parseIndexedId(std::string_view token) -> std::optional<int>
{
  if (token.size() < 2 || token.front() != '*' || !std::isdigit(static_cast<unsigned char>(token[1]))) {
    return std::nullopt;
  }
  return string::parse_number<int>(token.substr(1));
}

auto expandScanName(const spef::Exchange& exchange, std::string_view name) -> std::string
{
  return spef::removeEscapes(spef::stripQuotes(spef::expandName(exchange, std::string{name})));
}

auto extractGeometry(std::string_view text, int dbu) -> ScanGeometry
{
  ScanGeometry geometry;
  bool has_llx = false;
  bool has_lly = false;
  bool has_urx = false;
  bool has_ury = false;

  while (true) {
    const auto token = string::take_token(text);
    if (token.empty()) {
      break;
    }

    if (auto llx = string::parse_double_after_prefix(token, "$llx=")) {
      geometry.llx = unit::to_dbu(*llx, dbu);
      has_llx = true;
    } else if (auto lly = string::parse_double_after_prefix(token, "$lly=")) {
      geometry.lly = unit::to_dbu(*lly, dbu);
      has_lly = true;
    } else if (auto urx = string::parse_double_after_prefix(token, "$urx=")) {
      geometry.urx = unit::to_dbu(*urx, dbu);
      has_urx = true;
    } else if (auto ury = string::parse_double_after_prefix(token, "$ury=")) {
      geometry.ury = unit::to_dbu(*ury, dbu);
      has_ury = true;
    } else if (auto layer = string::parse_int_after_prefix(token, "$lvl=")) {
      geometry.layer = *layer;
      geometry.has_layer = true;
    }
  }

  geometry.has_box = has_llx && has_lly && has_urx && has_ury && geometry.llx != geometry.urx && geometry.lly != geometry.ury;
  return geometry;
}

auto getDesignName(const spef::Exchange& exchange, const std::string& spef_file) -> std::string
{
  for (const auto& header : exchange.header) {
    if (header.key == "*DESIGN" && !header.value.empty()) {
      return header.value;
    }
  }

  const auto stem = std::filesystem::path(spef_file).stem().string();
  return stem.empty() ? std::string{"plot_spef"} : stem;
}

auto fallbackBox(plot_spef::Node& node) -> void
{
  constexpr int kHalfSize = 2;
  node.llx = node.x - kHalfSize;
  node.lly = node.y - kHalfSize;
  node.urx = node.x + kHalfSize;
  node.ury = node.y + kHalfSize;
  node.has_box = true;
}

auto buildNode(const spef::ConnEntry& conn, int dbu) -> plot_spef::Node
{
  plot_spef::Node node;
  node.name = conn.pin_port_name;
  node.layer = std::max(conn.layer, 0);
  if (hasCoord(conn.coordinate)) {
    node.x = unit::to_dbu(conn.coordinate.x, dbu);
    node.y = unit::to_dbu(conn.coordinate.y, dbu);
    node.has_point = true;
  }
  if (hasCoord(conn.ll_coordinate) && hasCoord(conn.ur_coordinate)) {
    node.llx = unit::to_dbu(conn.ll_coordinate.x, dbu);
    node.lly = unit::to_dbu(conn.ll_coordinate.y, dbu);
    node.urx = unit::to_dbu(conn.ur_coordinate.x, dbu);
    node.ury = unit::to_dbu(conn.ur_coordinate.y, dbu);
    node.has_box = node.llx != node.urx && node.lly != node.ury;
  }
  if (!node.has_box && node.has_point) {
    fallbackBox(node);
  }
  return node;
}

auto applyGeometry(plot_spef::Node& node, const ScanGeometry& geometry) -> void
{
  if (geometry.has_layer) {
    node.layer = geometry.layer;
  }
  if (geometry.has_box) {
    node.llx = geometry.llx;
    node.lly = geometry.lly;
    node.urx = geometry.urx;
    node.ury = geometry.ury;
    node.has_box = true;
  }
}

auto applyGeometry(plot_spef::Resistor& resistor, const ScanGeometry& geometry) -> void
{
  if (geometry.has_layer) {
    resistor.layer = geometry.layer;
    resistor.has_layer = true;
  }
  if (geometry.has_box) {
    resistor.llx = geometry.llx;
    resistor.lly = geometry.lly;
    resistor.urx = geometry.urx;
    resistor.ury = geometry.ury;
    resistor.has_box = true;
  }
}

auto applyNodeGeometry(plot_spef::Model& model, plot_spef::Net& net, const std::string& node_name, const ScanGeometry& geometry) -> void
{
  auto local_node_it = net.nodes_by_name.find(node_name);
  if (local_node_it != net.nodes_by_name.end()) {
    applyGeometry(*local_node_it->second, geometry);
    return;
  }

  auto node_it = model.nodes_by_name.find(node_name);
  if (node_it != model.nodes_by_name.end()) {
    applyGeometry(*node_it->second, geometry);
  }
}

auto parseLayerMapLine(std::string_view line, plot_spef::Model& model) -> void
{
  auto payload = string::trim_view(line.substr(2));
  const auto index = string::take_token(payload);
  const auto name = string::take_token(payload);
  auto layer_id = parseIndexedId(index);
  if (!layer_id.has_value() || name.empty()) {
    return;
  }
  model.layer_names[*layer_id] = spef::removeEscapes(spef::stripQuotes(std::string{name}));
}

auto buildNetMap(plot_spef::Model& model) -> std::unordered_map<std::string, plot_spef::Net*>
{
  std::unordered_map<std::string, plot_spef::Net*> net_map;
  net_map.reserve(model.nets.size());
  for (auto& net : model.nets) {
    net_map[net.name] = &net;
  }
  return net_map;
}

auto augmentModelFromSpefText(const spef::Exchange& exchange, plot_spef::Model& model, const plot_spef::Config& config) -> void
{
  std::ifstream file(config.spef_file);
  if (!file.is_open()) {
    return;
  }

  auto net_map = buildNetMap(model);
  plot_spef::Net* current_net = nullptr;
  ScanSection section = ScanSection::kNone;
  bool in_layer_map = false;
  std::size_t res_index = 0;

  std::string line;
  while (std::getline(file, line)) {
    const auto stripped = string::trim_view(line);
    if (stripped.empty()) {
      continue;
    }

    if (string::starts_with(stripped, "//")) {
      if (string::starts_with(stripped, "// *LAYER_MAP")) {
        in_layer_map = true;
        continue;
      }
      if (in_layer_map) {
        parseLayerMapLine(stripped, model);
      }
      continue;
    }
    in_layer_map = false;

    const auto comment_pos = line.find("//");
    const auto content = string::trim_view(comment_pos == std::string::npos ? std::string_view{line} : std::string_view{line}.substr(0, comment_pos));
    const auto comment = comment_pos == std::string::npos ? std::string_view{} : std::string_view{line}.substr(comment_pos + 2);
    if (content.empty()) {
      continue;
    }

    if (string::starts_with(content, "*D_NET")) {
      auto content_tail = content;
      static_cast<void>(string::take_token(content_tail));
      auto net_name_token = string::take_token(content_tail);
      if (net_name_token.empty()) {
        current_net = nullptr;
        section = ScanSection::kNone;
        continue;
      }
      std::string net_name = expandScanName(exchange, net_name_token);
      const auto net_it = net_map.find(net_name);
      current_net = net_it == net_map.end() ? nullptr : net_it->second;
      section = ScanSection::kNone;
      res_index = 0;
      continue;
    }
    if (string::starts_with(content, "*END")) {
      current_net = nullptr;
      section = ScanSection::kNone;
      continue;
    }
    if (string::starts_with(content, "*CONN")) {
      section = ScanSection::kConn;
      continue;
    }
    if (string::starts_with(content, "*CAP")) {
      section = ScanSection::kCap;
      continue;
    }
    if (string::starts_with(content, "*RES")) {
      section = ScanSection::kRes;
      res_index = 0;
      continue;
    }
    if (current_net == nullptr) {
      continue;
    }

    if (section == ScanSection::kConn
        && (string::starts_with(content, "*I") || string::starts_with(content, "*P") || string::starts_with(content, "*N"))) {
      auto content_tail = content;
      static_cast<void>(string::take_token(content_tail));
      const auto node_name_token = string::take_token(content_tail);
      if (!node_name_token.empty()) {
        const std::string node_name = expandScanName(exchange, node_name_token);
        applyNodeGeometry(model, *current_net, node_name, extractGeometry(comment, config.dbu));
      }
      continue;
    }

    if (section == ScanSection::kRes && !content.empty() && std::isdigit(static_cast<unsigned char>(content.front()))) {
      if (res_index < current_net->resistors.size()) {
        applyGeometry(current_net->resistors[res_index], extractGeometry(comment, config.dbu));
      }
      ++res_index;
    }
  }
}

auto buildModel(const spef::Exchange& exchange, const plot_spef::Config& config) -> plot_spef::Model
{
  plot_spef::Model model;
  model.design_name = getDesignName(exchange, config.spef_file);
  model.cap_unit = spef::getSpefCapUnit(exchange);
  model.res_unit = spef::getSpefResUnit(exchange);
  model.dbu = config.dbu;
  model.nets.reserve(exchange.nets.size());
  std::size_t node_count = 0;
  for (const auto& spef_net : exchange.nets) {
    node_count += spef_net.conns.size();
  }
  model.nodes_by_name.reserve(node_count);

  for (const auto& spef_net : exchange.nets) {
    plot_spef::Net net;
    net.name = spef_net.name;
    net.nodes.reserve(spef_net.conns.size());
    net.nodes_by_name.reserve(spef_net.conns.size());
    if (config.plotResistance()) {
      net.resistors.reserve(spef_net.ress.size());
    }
    if (config.plotCouplingCap() || config.plotGroundCap()) {
      std::size_t coupling_cap_count = 0;
      std::size_t ground_cap_count = 0;
      for (const auto& cap : spef_net.caps) {
        if (cap.node2.empty()) {
          ground_cap_count++;
        } else {
          coupling_cap_count++;
        }
      }
      if (config.plotCouplingCap()) {
        net.coupling_caps.reserve(coupling_cap_count);
      }
      if (config.plotGroundCap()) {
        net.ground_caps.reserve(ground_cap_count);
      }
    }

    for (const auto& conn : spef_net.conns) {
      auto node = buildNode(conn, config.dbu);
      net.nodes.push_back(std::move(node));
    }

    if (config.plotResistance()) {
      for (const auto& res : spef_net.ress) {
        net.resistors.push_back(plot_spef::Resistor{.node1 = res.node1, .node2 = res.node2, .value = res.res_or_cap});
      }
    }

    if (config.plotCouplingCap() || config.plotGroundCap()) {
      for (const auto& cap : spef_net.caps) {
        plot_spef::Capacitor capacitor{.node1 = cap.node1, .node2 = cap.node2, .value = cap.res_or_cap};
        if (cap.node2.empty()) {
          if (config.plotGroundCap()) {
            net.ground_caps.push_back(std::move(capacitor));
          }
        } else if (config.plotCouplingCap()) {
          net.coupling_caps.push_back(std::move(capacitor));
        }
      }
    }

    model.nets.push_back(std::move(net));
    for (auto& node : model.nets.back().nodes) {
      model.nodes_by_name[node.name] = &node;
      model.nets.back().nodes_by_name[node.name] = &node;
    }
  }

  augmentModelFromSpefText(exchange, model, config);
  return model;
}

}  // namespace

auto PlotSpefTool::run(plot_spef::Config config) -> bool
{
  const plot_spef::ConfigValidator validator;
  if (!validator.validate(config)) {
    return false;
  }

  spef::SpefReader reader;
  if (!reader.read(config.spef_file)) {
    LOG_ERROR << "plot_spef failed: read SPEF failed: " << config.spef_file;
    return false;
  }
  reader.expandName();

  const spef::Exchange* exchange = reader.getSpefFile();
  if (exchange == nullptr) {
    LOG_ERROR << "plot_spef failed: SPEF reader returned empty data.";
    return false;
  }

  const auto model = buildModel(*exchange, config);
  const plot_spef::GdsWriter writer;
  if (!writer.write(model, config)) {
    return false;
  }
  LOG_INFO << "plot_spef wrote GDS to " << config.output_file;

  const plot_spef::LypWriter lyp_writer;
  if (!lyp_writer.write(model, config)) {
    return false;
  }

  return true;
}

}  // namespace ircx
