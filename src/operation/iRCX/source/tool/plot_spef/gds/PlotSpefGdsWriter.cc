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
#include "gds/PlotSpefGdsWriter.hh"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include "GTWriter.hpp"
#include "GdsData.hpp"
#include "GdsPath.hpp"
#include "GdsSref.hpp"
#include "GdsStruct.hpp"
#include "GdsText.hpp"
#include "FormatUtils.hh"
#include "log/Log.hh"
#include "model/PlotSpefGdsType.hh"

namespace ircx::plot_spef {
namespace {

constexpr int kNodeOutlineWidth = 2;
constexpr int kEdgeWidth = 4;
constexpr int kCcWidth = 2;

auto findNode(const Model& model, const std::string& name) -> const Node*
{
  const auto it = model.nodes_by_name.find(name);
  return it == model.nodes_by_name.end() ? nullptr : it->second;
}

auto addBoxOutline(idb::GdsStruct& gds_net, int layer, int data_type, int width, int llx, int lly, int urx, int ury) -> void
{
  idb::GdsPath path;
  path.layer = layer;
  path.data_type = data_type;
  path.width = width;
  path.add_coord(llx, lly);
  path.add_coord(urx, lly);
  path.add_coord(urx, ury);
  path.add_coord(llx, ury);
  path.add_coord(llx, lly);
  gds_net.add_element(path);
}

auto addRect(idb::GdsStruct& gds_net, const Node& node) -> void
{
  if (node.has_box) {
    addBoxOutline(gds_net, node.layer, kNode, kNodeOutlineWidth, node.llx, node.lly, node.urx, node.ury);
  }
}

auto addText(idb::GdsStruct& gds_net, int layer, int data_type, int x, int y, const std::string& text,
             idb::GdsPresentation presentation = idb::GdsPresentation::kBottomLeft) -> void
{
  idb::GdsText gds_text;
  gds_text.layer = layer;
  gds_text.text_type = data_type;
  gds_text.add_coord(x, y);
  gds_text.str = text;
  gds_text.presentation = presentation;
  gds_net.add_element(gds_text);
}

auto addPath(idb::GdsStruct& gds_net, int layer, int data_type, int width, const Node& node1, const Node& node2) -> void
{
  if (!node1.has_point || !node2.has_point) {
    return;
  }

  idb::GdsPath path;
  path.layer = layer;
  path.data_type = data_type;
  path.width = width;
  path.add_coord(node1.x, node1.y);
  path.add_coord(node2.x, node2.y);
  gds_net.add_element(path);
}

auto addRect(idb::GdsStruct& gds_net, const Resistor& resistor, int layer) -> void
{
  if (resistor.has_box) {
    addBoxOutline(gds_net, layer, kEdge, kEdgeWidth, resistor.llx, resistor.lly, resistor.urx, resistor.ury);
  }
}

auto addTopReference(idb::GdsData& gds_data, const std::string& name) -> void
{
  idb::GdsSref sref;
  sref.sname = name;
  sref.add_coord(0, 0);
  gds_data.get_top_struct()->add_element(sref);
}

}  // namespace

auto GdsWriter::safeStructName(const std::string& name) -> std::string
{
  std::string safe_name;
  safe_name.reserve(name.size());
  for (char ch : name) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$') {
      safe_name.push_back(ch);
    } else {
      safe_name.push_back('_');
    }
  }
  if (safe_name.empty()) {
    return "net";
  }
  if (std::isdigit(static_cast<unsigned char>(safe_name.front()))) {
    safe_name.insert(safe_name.begin(), 'n');
  }
  return safe_name;
}

auto GdsWriter::formatValue(double value, const std::string& unit) -> std::string
{
  return format::with_unit(value, unit, 3);
}

auto GdsWriter::write(const Model& model, const Config& config) const -> bool
{
  idb::GdsData gds_data;
  gds_data.set_lib_name(model.design_name);
  gds_data.set_unit(1.0 / model.dbu, 1e-6 / model.dbu);
  gds_data.set_top_struct(new idb::GdsStruct(safeStructName(model.design_name)));

  std::unordered_map<std::string, int> struct_name_count;
  struct_name_count.reserve(model.nets.size());

  idb::GdsiiTextWriter writer;
  if (!writer.init(config.output_file, &gds_data)) {
    LOG_ERROR << "plot_spef failed: cannot open GDS output file " << config.output_file;
    return false;
  }
  if (!writer.begin()) {
    LOG_ERROR << "plot_spef failed: cannot begin GDS output file " << config.output_file;
    return false;
  }

  for (const auto& net : model.nets) {
    std::string struct_name = safeStructName(net.name);
    const int count = struct_name_count[struct_name]++;
    if (count > 0) {
      struct_name += "_" + std::to_string(count);
    }

    addTopReference(gds_data, struct_name);
    idb::GdsStruct gds_net(struct_name);

    for (const auto& node : net.nodes) {
      addRect(gds_net, node);
      if (node.has_point) {
        addText(gds_net, node.layer, kTextNode, node.x, node.y, node.name);
      }
    }

    if (config.plotResistance()) {
      for (const auto& resistor : net.resistors) {
        const Node* node1 = findNode(model, resistor.node1);
        const Node* node2 = findNode(model, resistor.node2);
        if (node1 == nullptr || node2 == nullptr || !node1->has_point || !node2->has_point) {
          continue;
        }

        const int layer = resistor.has_layer ? resistor.layer : node1->layer;
        if (resistor.has_box) {
          addRect(gds_net, resistor, layer);
        } else {
          addPath(gds_net, layer, kEdge, kEdgeWidth, *node1, *node2);
        }
        addText(gds_net, layer, kTextRes, (node1->x + node2->x) / 2, (node1->y + node2->y) / 2,
                formatValue(resistor.value, model.res_unit), idb::GdsPresentation::kTopRight);
      }
    }

    if (config.plotCouplingCap()) {
      for (const auto& cap : net.coupling_caps) {
        const Node* node1 = findNode(model, cap.node1);
        const Node* node2 = findNode(model, cap.node2);
        if (node1 == nullptr || node2 == nullptr || !node1->has_point || !node2->has_point) {
          continue;
        }

        const int layer = node1->layer;
        addPath(gds_net, layer, kCc, kCcWidth, *node1, *node2);
        addText(gds_net, layer, kTextCc, (node1->x + node2->x) / 2, (node1->y + node2->y) / 2,
                formatValue(cap.value, model.cap_unit), idb::GdsPresentation::kTopRight);
      }
    }

    if (config.plotGroundCap()) {
      for (const auto& cap : net.ground_caps) {
        const Node* node = findNode(model, cap.node1);
        if (node == nullptr || !node->has_point) {
          continue;
        }
        addText(gds_net, node->layer, kTextCg, node->x, node->y, formatValue(cap.value, model.cap_unit),
                idb::GdsPresentation::kTopRight);
      }
    }

    gds_data.add_struct(gds_net);
  }
  return writer.finish();
}

}  // namespace ircx::plot_spef
