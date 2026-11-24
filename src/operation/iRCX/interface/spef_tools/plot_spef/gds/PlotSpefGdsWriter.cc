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
 * @file PlotSpefGdsWriter.cc
 * @brief GDS writer for plot_spef visualization.
 */
#include "gds/PlotSpefGdsWriter.hh"

#include "FormatUtils.hh"
#include "GTWriter.hpp"
#include "GdsBoundary.hpp"
#include "GdsData.hpp"
#include "GdsPath.hpp"
#include "GdsSref.hpp"
#include "GdsStruct.hpp"
#include "GdsText.hpp"
#include "Logger.hpp"
#include "ParallelUtils.hh"
#include "PathUtils.hh"
#include "RCXHeader.hpp"
#include "StringUtils.hh"
#include "config/PlotSpefConfig.hh"
#include "model/PlotSpefGdsType.hh"
#include "model/PlotSpefModel.hh"
#include "model/PlotSpefVisibility.hh"

namespace ircx::plot_spef {
namespace {

constexpr int kMissingBoxEdgeWidth = 1;
constexpr int kCcWidth = 1;
constexpr int kTargetEdgePathWidth = 3;
constexpr std::string_view kEdgeGdsDir = "edge_gds";

struct PlotPoint
{
  int x = 0;
  int y = 0;
  int layer = 0;
  bool valid = false;
};

struct GroundCapPlot
{
  std::string key;
  EdgeRef edge;
  std::string node;
  F64 value = 0.0;
};

struct CouplingCapPlot
{
  std::string key;
  EdgeRef edge1;
  EdgeRef edge2;
  std::string node1;
  std::string node2;
  F64 value = 0.0;
};

struct CouplingCapOwner
{
  Size net_index = 0;
  int rank = 0;
};

struct NetGdsJob
{
  const Net* net = nullptr;
  Size net_index = 0;
  std::string struct_name;
};

class VisibilityReader
{
 public:
  explicit VisibilityReader(const Visibility& visibility) : visibility_(visibility) {}

  auto netVisible(Size net_index) const -> bool { return visibility_.netVisible(net_index); }

  auto netCount() const -> Size { return visibility_.nets.size(); }

  auto netContextOnly(Size net_index) const -> bool { return visibility_.netContextOnly(net_index); }

  auto nodeVisible(Size net_index, Size node_index) const -> bool { return visibility_.nets[net_index].nodeVisible(node_index); }

  auto resistorVisible(Size net_index, Size resistor_index) const -> bool { return visibility_.nets[net_index].resistorVisible(resistor_index); }

  auto resistorTarget(Size net_index, Size resistor_index) const -> bool { return visibility_.nets[net_index].resistorTarget(resistor_index); }

  auto couplingCapVisible(Size net_index, Size cap_index) const -> bool { return visibility_.nets[net_index].couplingCapVisible(cap_index); }

  auto groundCapVisible(Size net_index, Size cap_index) const -> bool { return visibility_.nets[net_index].groundCapVisible(cap_index); }

 private:
  const Visibility& visibility_;
};

auto trimUnderscores(std::string text) -> std::string
{
  const auto first = text.find_first_not_of('_');
  if (first == std::string::npos) {
    return {};
  }
  const auto last = text.find_last_not_of('_');
  return text.substr(first, last - first + 1);
}

auto filePart(std::string_view value, std::string_view fallback) -> std::string
{
  std::string part = trimUnderscores(string::identifier(value, fallback));
  if (part.empty()) {
    part = fallback.empty() ? "id" : std::string(fallback);
  }
  return part;
}

auto edgeIndexFilePart(std::string_view value) -> std::string
{
  std::string part;
  part.reserve(value.size());
  for (const char ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$') {
      part.push_back(ch);
    } else {
      part.push_back('_');
    }
  }
  part = trimUnderscores(part);
  return part.empty() ? "index" : part;
}

auto edgeGdsStem(const Config& config) -> std::string
{
  const auto delimiter = config.edge_name.rfind(':');
  if (delimiter == std::string::npos || delimiter + 1 >= config.edge_name.size()) {
    return filePart(config.edge_name, "edge");
  }

  const auto net_part = filePart(std::string_view{config.edge_name}.substr(0, delimiter), "net");
  const auto index_part = edgeIndexFilePart(std::string_view{config.edge_name}.substr(delimiter + 1));
  return net_part + "_" + index_part;
}

auto gdsOutputFile(const Config& config, const std::string& gds_name) -> std::filesystem::path
{
  if (!config.hasEdgeFilter()) {
    return path::fileUnderDir(config.output_dir, gds_name, ".gds");
  }

  const auto edge_dir = std::filesystem::path(config.output_dir) / std::string(kEdgeGdsDir);
  if (!path::ensureDir(edge_dir, "plot_spef edge GDS directory")) {
    return {};
  }
  return path::fileUnderDir(edge_dir, edgeGdsStem(config), ".gds");
}

auto findEdge(const Model& model, const EdgeRef& ref) -> const Resistor*
{
  if (!ref.valid || ref.net_index >= model.nets.size()) {
    return nullptr;
  }
  const auto& net = model.nets[ref.net_index];
  if (ref.resistor_index >= net.resistors.size()) {
    return nullptr;
  }
  return &net.resistors[ref.resistor_index];
}

auto addBoxBoundary(idb::GdsStruct& gds_net, int layer, int data_type, int llx, int lly, int urx, int ury) -> void
{
  idb::GdsBoundary boundary;
  boundary.layer = layer;
  boundary.data_type = data_type;
  boundary.add_coord(llx, lly);
  boundary.add_coord(urx, lly);
  boundary.add_coord(urx, ury);
  boundary.add_coord(llx, ury);
  boundary.add_coord(llx, lly);
  gds_net.add_element(boundary);
}

auto addRect(idb::GdsStruct& gds_net, const Node& node) -> void
{
  if (node.has_box) {
    addBoxBoundary(gds_net, node.layer, kNode, node.llx, node.lly, node.urx, node.ury);
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

auto addPath(idb::GdsStruct& gds_net, int layer, int data_type, int width, const PlotPoint& point1, const PlotPoint& point2) -> void
{
  if (!point1.valid || !point2.valid) {
    return;
  }

  idb::GdsPath path;
  path.layer = layer;
  path.data_type = data_type;
  path.width = width;
  path.add_coord(point1.x, point1.y);
  path.add_coord(point2.x, point2.y);
  gds_net.add_element(path);
}

auto addRect(idb::GdsStruct& gds_net, const Resistor& resistor, int layer, int data_type = kEdge) -> void
{
  if (resistor.has_box) {
    addBoxBoundary(gds_net, layer, data_type, resistor.llx, resistor.lly, resistor.urx, resistor.ury);
  }
}

auto edgeCenter(const Model& model, const Resistor& edge) -> PlotPoint
{
  PlotPoint point;
  if (edge.has_layer) {
    point.layer = edge.layer;
  }
  if (edge.has_box) {
    point.x = (edge.llx + edge.urx) / 2;
    point.y = (edge.lly + edge.ury) / 2;
    point.valid = true;
    return point;
  }

  const Node* node1 = findNode(model, edge.node1);
  const Node* node2 = findNode(model, edge.node2);
  if (node1 == nullptr || node2 == nullptr || !node1->has_point || !node2->has_point) {
    return {};
  }

  point.x = (node1->x + node2->x) / 2;
  point.y = (node1->y + node2->y) / 2;
  if (!edge.has_layer) {
    point.layer = node1->layer;
  }
  point.valid = true;
  return point;
}

auto capEdgeCenter(const Model& model, const EdgeRef& ref) -> PlotPoint
{
  const Resistor* edge = findEdge(model, ref);
  return edge == nullptr ? PlotPoint{} : edgeCenter(model, *edge);
}

auto nodePoint(const Model& model, const std::string& node_name) -> PlotPoint
{
  const Node* node = findNode(model, node_name);
  if (node == nullptr || !node->has_point) {
    return {};
  }
  return PlotPoint{.x = node->x, .y = node->y, .layer = node->layer, .valid = true};
}

auto capPoint(const Model& model, const EdgeRef& edge, const std::string& node_name) -> PlotPoint
{
  PlotPoint point = capEdgeCenter(model, edge);
  return point.valid ? point : nodePoint(model, node_name);
}

auto isVisibleEdge(const Model& model, const VisibilityReader& visibility, const EdgeRef& ref) -> bool
{
  if (!ref.valid) {
    return true;
  }
  if (ref.net_index >= model.nets.size()) {
    return false;
  }
  const auto& net = model.nets[ref.net_index];
  return visibility.netVisible(ref.net_index) && ref.resistor_index < net.resistors.size() && visibility.resistorVisible(ref.net_index, ref.resistor_index);
}

auto capEndpointKey(const EdgeRef& edge, const std::string& node) -> std::string
{
  return edge.valid ? "E:" + edgeRefKey(edge) : "N:" + node;
}

auto couplingKey(const Capacitor& cap) -> std::string
{
  std::string key1 = capEndpointKey(cap.edge1, cap.node1);
  std::string key2 = capEndpointKey(cap.edge2, cap.node2);
  if (key2 < key1) {
    std::swap(key1, key2);
  }
  return key1 + "\n" + key2;
}

using CouplingCapOwners = std::unordered_map<std::string, CouplingCapOwner>;

auto groundKey(const Capacitor& cap) -> std::string
{
  return capEndpointKey(cap.edge1, cap.node1);
}

auto isTargetEdge(const VisibilityReader& visibility, const EdgeRef& edge) -> bool
{
  return edge.valid && edge.net_index < visibility.netCount() && visibility.resistorTarget(edge.net_index, edge.resistor_index);
}

auto couplingOwnerRank(const VisibilityReader& visibility, const Capacitor& cap) -> int
{
  if (isTargetEdge(visibility, cap.edge1)) {
    return 0;
  }
  if (isTargetEdge(visibility, cap.edge2)) {
    return 1;
  }
  return 2;
}

auto shouldReplaceOwner(const CouplingCapOwner& old_owner, const CouplingCapOwner& new_owner) -> bool
{
  if (new_owner.rank != old_owner.rank) {
    return new_owner.rank < old_owner.rank;
  }
  return new_owner.net_index < old_owner.net_index;
}

auto collectCouplingCapOwners(const Model& model, const VisibilityReader& visibility) -> CouplingCapOwners
{
  CouplingCapOwners owners;
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    for (Size cap_index = 0; cap_index < net.coupling_caps.size(); ++cap_index) {
      const auto& cap = net.coupling_caps[cap_index];
      if (!visibility.couplingCapVisible(net_index, cap_index)) {
        continue;
      }
      if (!isVisibleEdge(model, visibility, cap.edge1) || !isVisibleEdge(model, visibility, cap.edge2)) {
        continue;
      }

      const std::string key = couplingKey(cap);
      const CouplingCapOwner new_owner{.net_index = net_index, .rank = couplingOwnerRank(visibility, cap)};
      const auto [it, inserted] = owners.emplace(key, new_owner);
      if (!inserted && shouldReplaceOwner(it->second, new_owner)) {
        it->second = new_owner;
      }
    }
  }
  return owners;
}

auto makeCouplingCapPlot(const VisibilityReader& visibility, const Capacitor& cap, const std::string& key) -> CouplingCapPlot
{
  if (!isTargetEdge(visibility, cap.edge1) && isTargetEdge(visibility, cap.edge2)) {
    return CouplingCapPlot{.key = key, .edge1 = cap.edge2, .edge2 = cap.edge1, .node1 = cap.node2, .node2 = cap.node1};
  }

  return CouplingCapPlot{.key = key, .edge1 = cap.edge1, .edge2 = cap.edge2, .node1 = cap.node1, .node2 = cap.node2};
}

auto collectGroundCapPlots(const Model& model, const VisibilityReader& visibility, Size net_index) -> std::vector<GroundCapPlot>
{
  const auto& net = model.nets[net_index];
  std::unordered_map<std::string, Size> index_by_key;
  std::vector<GroundCapPlot> plots;
  for (Size cap_index = 0; cap_index < net.ground_caps.size(); ++cap_index) {
    const auto& cap = net.ground_caps[cap_index];
    if (!visibility.groundCapVisible(net_index, cap_index)) {
      continue;
    }
    if (!isVisibleEdge(model, visibility, cap.edge1)) {
      continue;
    }
    const std::string key = groundKey(cap);
    const auto [it, inserted] = index_by_key.emplace(key, plots.size());
    if (inserted) {
      plots.push_back(GroundCapPlot{.key = key, .edge = cap.edge1, .node = cap.node1});
    }
    plots[it->second].value += cap.value;
  }
  std::sort(plots.begin(), plots.end(), [](const GroundCapPlot& lhs, const GroundCapPlot& rhs) { return lhs.key < rhs.key; });
  return plots;
}

auto collectCouplingCapPlots(const Model& model, const VisibilityReader& visibility, const CouplingCapOwners& owners, Size net_index)
    -> std::vector<CouplingCapPlot>
{
  const auto& net = model.nets[net_index];
  std::unordered_map<std::string, Size> index_by_key;
  std::vector<CouplingCapPlot> plots;
  for (Size cap_index = 0; cap_index < net.coupling_caps.size(); ++cap_index) {
    const auto& cap = net.coupling_caps[cap_index];
    if (!visibility.couplingCapVisible(net_index, cap_index)) {
      continue;
    }
    if (!isVisibleEdge(model, visibility, cap.edge1) || !isVisibleEdge(model, visibility, cap.edge2)) {
      continue;
    }
    const std::string key = couplingKey(cap);
    const auto owner_it = owners.find(key);
    if (owner_it != owners.end() && owner_it->second.net_index != net_index) {
      continue;
    }
    const auto [it, inserted] = index_by_key.emplace(key, plots.size());
    if (inserted) {
      plots.push_back(makeCouplingCapPlot(visibility, cap, key));
    }
    plots[it->second].value += cap.value;
  }
  std::sort(plots.begin(), plots.end(), [](const CouplingCapPlot& lhs, const CouplingCapPlot& rhs) { return lhs.key < rhs.key; });
  return plots;
}

auto addTopReference(idb::GdsData& gds_data, const std::string& name) -> void
{
  idb::GdsSref sref;
  sref.sname = name;
  sref.add_coord(0, 0);
  gds_data.get_top_struct()->add_element(sref);
}

auto formatCapacitance(F64 value, const std::string& unit) -> std::string
{
  return format::withUnit(value, format::unitSymbol(unit), 3);
}

auto edgeName(const Model& model, const EdgeRef& edge) -> std::string
{
  const Resistor* resistor = findEdge(model, edge);
  if (resistor == nullptr || edge.net_index >= model.nets.size()) {
    return {};
  }
  return model.nets[edge.net_index].name + ":" + std::to_string(resistor->index);
}

auto capEndpointLabel(const Model& model, const VisibilityReader& visibility, const EdgeRef& edge, const std::string& node_name) -> std::string
{
  if (isTargetEdge(visibility, edge)) {
    return node_name;
  }
  const std::string edge_name = edgeName(model, edge);
  return edge_name.empty() ? node_name : edge_name + "(" + node_name + ")";
}

auto formatResistance(const Resistor& resistor) -> std::string
{
  return "RES" + std::to_string(resistor.index) + " R=" + format::significant(resistor.value, 3) + "OHM";
}

auto formatCouplingCap(const Model& model, const VisibilityReader& visibility, const CouplingCapPlot& cap) -> std::string
{
  return "CC=" + formatCapacitance(cap.value, model.cap_unit) + " " + capEndpointLabel(model, visibility, cap.edge1, cap.node1) + " <-> "
         + capEndpointLabel(model, visibility, cap.edge2, cap.node2);
}

auto formatGroundCap(const Model& model, const VisibilityReader& visibility, const GroundCapPlot& cap) -> std::string
{
  return "CG=" + formatCapacitance(cap.value, model.cap_unit) + " " + capEndpointLabel(model, visibility, cap.edge, cap.node);
}

auto collectNetGdsJobs(const Model& model, const VisibilityReader& visibility) -> std::vector<NetGdsJob>
{
  std::unordered_map<std::string, int> struct_name_count;
  struct_name_count.reserve(model.nets.size());

  std::vector<NetGdsJob> jobs;
  jobs.reserve(model.nets.size());
  for (Size net_index = 0; net_index < model.nets.size(); ++net_index) {
    const auto& net = model.nets[net_index];
    if (!visibility.netVisible(net_index)) {
      continue;
    }
    std::string struct_name = string::identifier(net.name, "net");
    const int count = struct_name_count[struct_name]++;
    if (count > 0) {
      struct_name += "_" + std::to_string(count);
    }
    jobs.push_back({.net = &net, .net_index = net_index, .struct_name = std::move(struct_name)});
  }
  return jobs;
}

class NetGdsWriter
{
 public:
  NetGdsWriter(const Model& model, const VisibilityReader& visibility, const CouplingCapOwners& coupling_cap_owners, const Config& config)
      : model_(model), visibility_(visibility), coupling_cap_owners_(coupling_cap_owners), config_(config)
  {
  }

  auto write(idb::GdsStruct& gds_net, Size net_index, const Net& net) const -> void
  {
    writeNodes(gds_net, net_index, net);
    writeResistors(gds_net, net_index, net);
    writeCouplingCaps(gds_net, net_index);
    writeGroundCaps(gds_net, net_index);
  }

 private:
  auto writeNodes(idb::GdsStruct& gds_net, Size net_index, const Net& net) const -> void
  {
    for (Size node_index = 0; node_index < net.nodes.size(); ++node_index) {
      const auto& node = net.nodes[node_index];
      if (!visibility_.nodeVisible(net_index, node_index)) {
        continue;
      }
      addRect(gds_net, node);
      if (node.has_point) {
        addText(gds_net, node.layer, kTextNode, node.x, node.y, node.name);
      }
    }
  }

  auto writeResistors(idb::GdsStruct& gds_net, Size net_index, const Net& net) const -> void
  {
    for (Size resistor_index = 0; resistor_index < net.resistors.size(); ++resistor_index) {
      const auto& resistor = net.resistors[resistor_index];
      if (!visibility_.resistorVisible(net_index, resistor_index)) {
        continue;
      }
      const Node* node1 = findNode(model_, resistor.node1);
      const Node* node2 = findNode(model_, resistor.node2);
      if (node1 == nullptr || node2 == nullptr || !node1->has_point || !node2->has_point) {
        continue;
      }

      const int layer = resistor.has_layer ? resistor.layer : node1->layer;
      if (resistor.has_box) {
        addRect(gds_net, resistor, layer);
      } else {
        addPath(gds_net, layer, kEdge, kMissingBoxEdgeWidth, *node1, *node2);
      }
      if (visibility_.resistorTarget(net_index, resistor_index)) {
        if (resistor.has_box) {
          addRect(gds_net, resistor, layer, kTargetEdge);
        } else {
          addPath(gds_net, layer, kTargetEdge, kTargetEdgePathWidth, *node1, *node2);
        }
      }
      if (config_.plotResistance()) {
        addText(gds_net, layer, kTextRes, (node1->x + node2->x) / 2, (node1->y + node2->y) / 2, formatResistance(resistor), idb::GdsPresentation::kTopRight);
      }
    }
  }

  auto writeCouplingCaps(idb::GdsStruct& gds_net, Size net_index) const -> void
  {
    if ((visibility_.netContextOnly(net_index) && !config_.hasEdgeFilter()) || !config_.plotCouplingCap()) {
      return;
    }

    for (const auto& cap : collectCouplingCapPlots(model_, visibility_, coupling_cap_owners_, net_index)) {
      const PlotPoint point1 = capPoint(model_, cap.edge1, cap.node1);
      const PlotPoint point2 = capPoint(model_, cap.edge2, cap.node2);
      if (!point1.valid || !point2.valid) {
        continue;
      }

      const int layer = point1.layer;
      addPath(gds_net, layer, kCc, kCcWidth, point1, point2);
      addText(gds_net, layer, kTextCc, (point1.x + point2.x) / 2, (point1.y + point2.y) / 2, formatCouplingCap(model_, visibility_, cap),
              idb::GdsPresentation::kTopRight);
    }
  }

  auto writeGroundCaps(idb::GdsStruct& gds_net, Size net_index) const -> void
  {
    if ((visibility_.netContextOnly(net_index) && !config_.hasEdgeFilter()) || !config_.plotGroundCap()) {
      return;
    }

    for (const auto& cap : collectGroundCapPlots(model_, visibility_, net_index)) {
      PlotPoint point = capPoint(model_, cap.edge, cap.node);
      if (!point.valid) {
        continue;
      }
      addText(gds_net, point.layer, kTextCg, point.x, point.y, formatGroundCap(model_, visibility_, cap), idb::GdsPresentation::kTopRight);
    }
  }

  const Model& model_;
  const VisibilityReader& visibility_;
  const CouplingCapOwners& coupling_cap_owners_;
  const Config& config_;
};

auto buildNetGdsStructs(const Model& model, const VisibilityReader& visibility, const CouplingCapOwners& coupling_cap_owners, const Config& config,
                        const std::vector<NetGdsJob>& jobs) -> std::vector<std::unique_ptr<idb::GdsStruct>>
{
  // Threads build private structs only. GdsData and the file writer are updated
  // later on the main thread because their ownership model is not thread-safe.
  std::vector<std::unique_ptr<idb::GdsStruct>> structs(jobs.size());
  const NetGdsWriter net_writer(model, visibility, coupling_cap_owners, config);
  const int threads = parallel::threadCount(jobs.size(), config.cores);
#pragma omp parallel for schedule(dynamic, 16) num_threads(threads)
  for (I64 index = 0; index < static_cast<I64>(jobs.size()); ++index) {
    const auto& job = jobs[index];
    auto gds_net = std::make_unique<idb::GdsStruct>(job.struct_name);
    net_writer.write(*gds_net, job.net_index, *job.net);
    structs[index] = std::move(gds_net);
  }
  return structs;
}

}  // namespace

auto GdsWriter::formatValue(F64 value, const std::string& unit) -> std::string
{
  return formatCapacitance(value, unit);
}

auto GdsWriter::write(const Model& model, const Visibility& visibility, const Config& config) const -> bool
{
  const auto gds_name = string::identifier(model.design_name, "plot_spef");
  const auto gds_file = gdsOutputFile(config, gds_name);
  if (gds_file.empty()) {
    return false;
  }

  idb::GdsData gds_data;
  gds_data.set_lib_name(model.design_name);
  gds_data.set_unit(1.0 / model.dbu, 1e-6 / model.dbu);
  gds_data.set_top_struct(new idb::GdsStruct(gds_name));

  const VisibilityReader visibility_reader(visibility);
  const CouplingCapOwners coupling_cap_owners = collectCouplingCapOwners(model, visibility_reader);
  const auto jobs = collectNetGdsJobs(model, visibility_reader);
  auto net_structs = buildNetGdsStructs(model, visibility_reader, coupling_cap_owners, config, jobs);

  idb::GdsiiTextWriter writer;
  if (!writer.init(gds_file.string(), &gds_data)) {
    RCXLOG.warn(Loc::current(), "plot_spef failed: cannot open GDS output file ", gds_file.string());
    return false;
  }
  if (!writer.begin()) {
    RCXLOG.warn(Loc::current(), "plot_spef failed: cannot begin GDS output file ", gds_file.string());
    return false;
  }

  for (Size index = 0; index < jobs.size(); ++index) {
    addTopReference(gds_data, jobs[index].struct_name);
    gds_data.add_struct(net_structs[index].release());
  }
  if (!writer.finish()) {
    return false;
  }
  if (config.log_gds_file) {
    RCXLOG.info(Loc::current(), "plot_spef wrote GDS to ", gds_file.string());
  }
  return true;
}

}  // namespace ircx::plot_spef
