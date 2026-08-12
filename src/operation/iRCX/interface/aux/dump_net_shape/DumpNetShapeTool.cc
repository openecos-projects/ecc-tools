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
#include "DumpNetShapeTool.hh"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "Direction.hpp"
#include "Geometry.hh"
#include "LayerShape.hpp"
#include "LayerTable.hpp"
#include "LayoutData.hpp"
#include "Net.hpp"
#include "Patch.hpp"
#include "Pin.hpp"
#include "RoutingLayer.hpp"
#include "Segment.hpp"
#include "Utility.hpp"
#include "Via.hpp"
#include "Logger.hpp"

namespace ircx::dump_net_shape {

enum class ShapeCode : int32_t
{
  kSegment,
  kPatch,
  kViaNonCut,
  kViaCut,
  kPinNonCut,
  kPinCut
};

class LayerCatalog
{
 public:
  std::vector<int32_t> layer_idx_list;
  std::set<int32_t> cut_layer_idx_set;
};

constexpr std::array<char, 6> kShapeCodeNameArray = {'A', 'B', 'C', 'D', 'E', 'F'};

char getShapeCodeName(ShapeCode shape_code)
{
  return kShapeCodeNameArray[static_cast<int32_t>(shape_code)];
}

bool getIsValidLayerIdx(int32_t layer_idx)
{
  return layer_idx >= 0;
}

void writeQuoted(std::ostream& stream, const std::string& value)
{
  stream << '"';
  for (char character : value) {
    if (character == '\\' || character == '"') {
      stream << '\\' << character;
    } else if (character == '\n') {
      stream << "\\n";
    } else {
      stream << character;
    }
  }
  stream << '"';
}

void writeBool(std::ostream& stream, bool value)
{
  stream << (value ? "true" : "false");
}

void writeLayerIdx(std::ostream& stream, int32_t layer_idx)
{
  if (!getIsValidLayerIdx(layer_idx)) {
    stream << "NA";
    return;
  }
  stream << layer_idx;
}

void writePoint(std::ostream& stream, const GTLPointInt& point)
{
  stream << '[' << geom::x(point) << ' ' << geom::y(point) << ']';
}

void writeRect(std::ostream& stream, const GTLRectInt& rect)
{
  stream << '[' << geom::minX(rect) << ' ' << geom::minY(rect) << ' ' << geom::maxX(rect) << ' ' << geom::maxY(rect) << ']';
}

bool getIsCutLayer(int32_t layer_idx, const std::set<int32_t>& cut_layer_idx_set)
{
  return cut_layer_idx_set.find(layer_idx) != cut_layer_idx_set.end();
}

void collectLayerShape(LayerShape& layer_shape,
                       std::set<int32_t>& layer_idx_set,
                       std::set<int32_t>& cut_layer_idx_set,
                       bool is_cut_layer)
{
  int32_t layer_idx = layer_shape.get_layer_idx();
  if (!getIsValidLayerIdx(layer_idx)) {
    return;
  }
  layer_idx_set.insert(layer_idx);
  if (is_cut_layer) {
    cut_layer_idx_set.insert(layer_idx);
  }
}

void collectNetLayerList(Net& net,
                         std::set<int32_t>& layer_idx_set,
                         std::set<int32_t>& cut_layer_idx_set,
                         std::map<std::pair<int32_t, int32_t>, int32_t>& routing_pair_to_cut_layer_idx_map)
{
  for (Segment& segment : net.get_segment_list()) {
    if (getIsValidLayerIdx(segment.get_layer_idx())) {
      layer_idx_set.insert(segment.get_layer_idx());
    }
  }
  for (Patch& patch : net.get_patch_list()) {
    if (getIsValidLayerIdx(patch.get_layer_idx())) {
      layer_idx_set.insert(patch.get_layer_idx());
    }
  }
  for (Via& via : net.get_via_list()) {
    LayerShape& bottom_layer_shape = via.get_bottom_layer_shape();
    LayerShape& cut_layer_shape = via.get_cut_layer_shape();
    LayerShape& top_layer_shape = via.get_top_layer_shape();
    collectLayerShape(bottom_layer_shape, layer_idx_set, cut_layer_idx_set, false);
    collectLayerShape(cut_layer_shape, layer_idx_set, cut_layer_idx_set, true);
    collectLayerShape(top_layer_shape, layer_idx_set, cut_layer_idx_set, false);

    int32_t bottom_layer_idx = bottom_layer_shape.get_layer_idx();
    int32_t top_layer_idx = top_layer_shape.get_layer_idx();
    int32_t cut_layer_idx = cut_layer_shape.get_layer_idx();
    if (getIsValidLayerIdx(bottom_layer_idx) && getIsValidLayerIdx(top_layer_idx) && getIsValidLayerIdx(cut_layer_idx)) {
      routing_pair_to_cut_layer_idx_map[{std::min(bottom_layer_idx, top_layer_idx), std::max(bottom_layer_idx, top_layer_idx)}]
          = cut_layer_idx;
    }
  }
  for (Pin& pin : net.get_pin_list()) {
    for (LayerShape& layer_shape : pin.get_layer_shape_list()) {
      collectLayerShape(layer_shape, layer_idx_set, cut_layer_idx_set, false);
    }
  }
}

LayerCatalog buildLayerCatalog(LayoutData& layout_data)
{
  LayerCatalog layer_catalog;
  std::map<std::pair<int32_t, int32_t>, int32_t> routing_pair_to_cut_layer_idx_map;
  std::set<int32_t> layer_idx_set;

  for (Net& net : layout_data.get_net_list()) {
    collectNetLayerList(net, layer_idx_set, layer_catalog.cut_layer_idx_set, routing_pair_to_cut_layer_idx_map);
  }

  std::vector<int32_t> routing_layer_idx_list;
  for (const std::pair<const int32_t, RoutingLayer>& routing_layer_pair : layout_data.get_routing_layer_map()) {
    routing_layer_idx_list.push_back(routing_layer_pair.first);
    layer_idx_set.insert(routing_layer_pair.first);
  }
  std::sort(routing_layer_idx_list.begin(), routing_layer_idx_list.end());

  int32_t first_cut_layer_idx = routing_layer_idx_list.empty() ? -1 : routing_layer_idx_list.back() + 1;
  for (int32_t routing_layer_list_idx = 0;
       routing_layer_list_idx + 1 < static_cast<int32_t>(routing_layer_idx_list.size());
       ++routing_layer_list_idx) {
    int32_t lower_routing_layer_idx = routing_layer_idx_list[routing_layer_list_idx];
    int32_t upper_routing_layer_idx = routing_layer_idx_list[routing_layer_list_idx + 1];
    std::pair<int32_t, int32_t> routing_layer_pair = {lower_routing_layer_idx, upper_routing_layer_idx};
    if (routing_pair_to_cut_layer_idx_map.find(routing_layer_pair) == routing_pair_to_cut_layer_idx_map.end()) {
      routing_pair_to_cut_layer_idx_map[routing_layer_pair] = first_cut_layer_idx + routing_layer_list_idx;
    }
    int32_t cut_layer_idx = routing_pair_to_cut_layer_idx_map[routing_layer_pair];
    layer_catalog.cut_layer_idx_set.insert(cut_layer_idx);
    layer_idx_set.insert(cut_layer_idx);
  }

  std::set<int32_t> emitted_layer_idx_set;
  for (int32_t routing_layer_list_idx = 0;
       routing_layer_list_idx < static_cast<int32_t>(routing_layer_idx_list.size());
       ++routing_layer_list_idx) {
    int32_t routing_layer_idx = routing_layer_idx_list[routing_layer_list_idx];
    if (emitted_layer_idx_set.insert(routing_layer_idx).second) {
      layer_catalog.layer_idx_list.push_back(routing_layer_idx);
    }
    if (routing_layer_list_idx + 1 >= static_cast<int32_t>(routing_layer_idx_list.size())) {
      continue;
    }
    std::pair<int32_t, int32_t> routing_layer_pair = {
        routing_layer_idx, routing_layer_idx_list[routing_layer_list_idx + 1]};
    std::map<std::pair<int32_t, int32_t>, int32_t>::iterator cut_layer_it = routing_pair_to_cut_layer_idx_map.find(routing_layer_pair);
    if (cut_layer_it != routing_pair_to_cut_layer_idx_map.end() && emitted_layer_idx_set.insert(cut_layer_it->second).second) {
      layer_catalog.layer_idx_list.push_back(cut_layer_it->second);
    }
  }
  for (int32_t layer_idx : layer_idx_set) {
    if (!getIsCutLayer(layer_idx, layer_catalog.cut_layer_idx_set) && emitted_layer_idx_set.insert(layer_idx).second) {
      layer_catalog.layer_idx_list.push_back(layer_idx);
    }
  }
  for (int32_t layer_idx : layer_catalog.cut_layer_idx_set) {
    if (emitted_layer_idx_set.insert(layer_idx).second) {
      layer_catalog.layer_idx_list.push_back(layer_idx);
    }
  }
  return layer_catalog;
}

std::string getLayerName(LayoutData& layout_data, LayerTable& layer_table, int32_t layer_idx)
{
  std::map<int32_t, RoutingLayer>::iterator routing_layer_it = layout_data.get_routing_layer_map().find(layer_idx);
  if (routing_layer_it != layout_data.get_routing_layer_map().end()) {
    return routing_layer_it->second.get_layer_name();
  }
  std::unordered_map<int32_t, std::string>::iterator layer_name_it = layer_table.get_design_idx_to_name_map().find(layer_idx);
  if (layer_name_it != layer_table.get_design_idx_to_name_map().end()) {
    return layer_name_it->second;
  }
  return "UNKNOWN";
}

void writeHeader(std::ostream& stream, LayoutData& layout_data, LayerTable& layer_table, LayerCatalog& layer_catalog)
{
  stream << "# dump_net_shape\n";
  stream << "# format_version: 2\n";
  stream << "# design_name: ";
  writeQuoted(stream, layout_data.get_design_name());
  stream << '\n';
  stream << "# dbu_per_micron: " << layout_data.get_dbu_per_micron() << '\n';
  stream << "# purpose: AI-readable dump of DEF/LEF-derived raw net shapes; "
            "compare with StarRC topology from 8_spef_topology_starrc.py\n";
  stream << "# shape_code: A=Segment B=Patch C=Via_non_cut_layer "
            "D=Via_cut_layer E=Pin_non_cut_layer F=Pin_cut_layer\n";
  stream << "# shape_code_note: C/E are non-cut shape types on the current layer; "
            "they do not imply upper/lower layer direction\n";
  stream << "# shape_code_note: D/F are cut-layer shape types on the current layer\n";
  stream << "# coordinate_format: point=[x y], rect=[llx lly urx ury], all in DBU\n";
  stream << "# records:\n";
  stream << "#   NET <id> <name> regular <segment_count> <patch_count> <via_count> <pin_count>\n";
  stream << "#   S  <id> <layer> <rect>\n";
  stream << "#   P  <id> <layer> <rect>\n";
  stream << "#   V  <id> <name> <point>\n";
  stream << "#   VS <via_id> <shape_id> <code:C|D> <layer> <rect>\n";
  stream << "#   PN <id> <name> <is_port> <is_driver> <is_input> <is_output>\n";
  stream << "#   PS <pin_id> <shape_id> <code:E|F> <layer> <rect>\n";
  stream << "# layer_order low_to_high: index layer_id design_layer_name\n";
  for (int32_t layer_list_idx = 0; layer_list_idx < static_cast<int32_t>(layer_catalog.layer_idx_list.size()); ++layer_list_idx) {
    int32_t layer_idx = layer_catalog.layer_idx_list[layer_list_idx];
    stream << "# L " << layer_list_idx << ' ' << layer_idx << ' ';
    writeQuoted(stream, getLayerName(layout_data, layer_table, layer_idx));
    stream << '\n';
  }
}

void writeViaLayerShape(std::ostream& stream,
                        int32_t via_idx,
                        int32_t shape_idx,
                        ShapeCode shape_code,
                        LayerShape& layer_shape)
{
  stream << "VS " << via_idx << ' ' << shape_idx << ' ' << getShapeCodeName(shape_code) << ' ';
  writeLayerIdx(stream, layer_shape.get_layer_idx());
  stream << ' ';
  writeRect(stream, layer_shape.get_shape());
  stream << '\n';
}

void writeNetShapeList(std::ostream& stream, Net& net, LayerCatalog& layer_catalog)
{
  for (int32_t segment_idx = 0; segment_idx < static_cast<int32_t>(net.get_segment_list().size()); ++segment_idx) {
    Segment& segment = net.get_segment_list()[segment_idx];
    stream << "S " << segment_idx << ' ';
    writeLayerIdx(stream, segment.get_layer_idx());
    stream << ' ';
    writeRect(stream, segment.get_shape());
    stream << '\n';
  }
  for (int32_t patch_idx = 0; patch_idx < static_cast<int32_t>(net.get_patch_list().size()); ++patch_idx) {
    Patch& patch = net.get_patch_list()[patch_idx];
    stream << "P " << patch_idx << ' ';
    writeLayerIdx(stream, patch.get_layer_idx());
    stream << ' ';
    writeRect(stream, patch.get_shape());
    stream << '\n';
  }
  for (int32_t via_idx = 0; via_idx < static_cast<int32_t>(net.get_via_list().size()); ++via_idx) {
    Via& via = net.get_via_list()[via_idx];
    stream << "V " << via_idx << ' ';
    writeQuoted(stream, via.get_via_name());
    stream << ' ';
    writePoint(stream, via.get_point());
    stream << '\n';
    writeViaLayerShape(stream, via_idx, 0, ShapeCode::kViaNonCut, via.get_bottom_layer_shape());
    writeViaLayerShape(stream, via_idx, 1, ShapeCode::kViaCut, via.get_cut_layer_shape());
    writeViaLayerShape(stream, via_idx, 2, ShapeCode::kViaNonCut, via.get_top_layer_shape());
  }
  for (int32_t pin_idx = 0; pin_idx < static_cast<int32_t>(net.get_pin_list().size()); ++pin_idx) {
    Pin& pin = net.get_pin_list()[pin_idx];
    Direction direction = pin.get_direction();
    stream << "PN " << pin_idx << ' ';
    writeQuoted(stream, pin.get_pin_name());
    stream << ' ';
    writeBool(stream, pin.get_is_port());
    stream << ' ';
    writeBool(stream, pin.get_is_driver());
    stream << ' ';
    writeBool(stream, direction == Direction::kInput);
    stream << ' ';
    writeBool(stream, direction == Direction::kOutput);
    stream << '\n';
    for (int32_t layer_shape_idx = 0; layer_shape_idx < static_cast<int32_t>(pin.get_layer_shape_list().size()); ++layer_shape_idx) {
      LayerShape& layer_shape = pin.get_layer_shape_list()[layer_shape_idx];
      ShapeCode shape_code = getIsCutLayer(layer_shape.get_layer_idx(), layer_catalog.cut_layer_idx_set) ? ShapeCode::kPinCut
                                                                                                           : ShapeCode::kPinNonCut;
      stream << "PS " << pin_idx << ' ' << layer_shape_idx << ' ' << getShapeCodeName(shape_code) << ' ';
      writeLayerIdx(stream, layer_shape.get_layer_idx());
      stream << ' ';
      writeRect(stream, layer_shape.get_shape());
      stream << '\n';
    }
  }
}

void writeNet(std::ostream& stream, Net& net, LayerCatalog& layer_catalog)
{
  stream << "NET " << net.get_net_idx() << ' ';
  writeQuoted(stream, net.get_net_name());
  stream << " regular " << net.get_segment_list().size() << ' ' << net.get_patch_list().size() << ' ' << net.get_via_list().size()
         << ' ' << net.get_pin_list().size() << '\n';
  writeNetShapeList(stream, net, layer_catalog);
  stream << "END_NET\n";
}

}  // namespace ircx::dump_net_shape

namespace ircx {

bool DumpNetShapeTool::run()
{
  Database& database = RCXDM.getDatabase();
  LayoutData& layout_data = database.get_layout_data();
  if (layout_data.get_is_empty()) {
    RCXLOG.warn(Loc::current(), "dump_net_shape failed: layout data is empty. Run init_rcx before dumping shapes.");
    return false;
  }

  std::string design_name = layout_data.get_design_name();
  std::string output_file_path = (design_name.empty() ? "design" : design_name) + ".shape";
  std::ofstream* output_file_stream = RCXUTIL.getOutputFileStream(output_file_path);
  if (!output_file_stream->is_open()) {
    RCXLOG.warn(Loc::current(), "dump_net_shape failed: cannot open output file ", output_file_path);
    RCXUTIL.closeFileStream(output_file_stream);
    return false;
  }

  dump_net_shape::LayerCatalog layer_catalog = dump_net_shape::buildLayerCatalog(layout_data);
  dump_net_shape::writeHeader(*output_file_stream, layout_data, database.get_layer_table(), layer_catalog);
  for (Net& net : layout_data.get_net_list()) {
    dump_net_shape::writeNet(*output_file_stream, net, layer_catalog);
  }
  if (!output_file_stream->good()) {
    RCXLOG.warn(Loc::current(), "dump_net_shape failed: cannot write output file ", output_file_path);
    RCXUTIL.closeFileStream(output_file_stream);
    return false;
  }
  RCXUTIL.closeFileStream(output_file_stream);

  RCXLOG.info(Loc::current(), "dump_net_shape wrote ", output_file_path);
  return true;
}

}  // namespace ircx
