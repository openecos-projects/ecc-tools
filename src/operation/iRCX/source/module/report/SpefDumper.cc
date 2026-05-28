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
#include "SpefDumper.hh"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "Geometry.hh"
#include "LayoutData.hh"
#include "ProcessCorner.hpp"
#include "RCTable.hh"
#include "RCXConfig.hh"
#include "SpefContext.hh"
#include "TopoPool.hh"
#include "Types.hh"
#include "log/Log.hh"
#include "usage/usage.hh"

namespace ircx {

// ─────────────────────────────────────────────────────────────────────────────
// buildNameMaps
// ─────────────────────────────────────────────────────────────────────────────

void SpefDumper::buildNameMaps() const
{
  name_maps_ = NameMaps{};

  // 1. Regular net names
  for (const Str& name : spef_context_->net_names) {
    name_maps_.net_id_to_name[name_maps_.next_id] = name;
    name_maps_.net_name_to_id[name] = name_maps_.next_id;
    name_maps_.next_id++;
  }
  // 2. Ports (IO pins)
  for (const Str& name : spef_context_->port_names) {
    name_maps_.port_id_to_name[name_maps_.next_id] = name;
    name_maps_.port_name_to_id[name] = name_maps_.next_id;
    name_maps_.next_id++;
  }
  // 3. Instance names
  for (const Str& name : spef_context_->instance_names) {
    name_maps_.inst_id_to_name[name_maps_.next_id] = name;
    name_maps_.inst_name_to_id[name] = name_maps_.next_id;
    name_maps_.next_id++;
  }
}

void SpefDumper::buildPortIo() const
{
  port_io_.clear();
  port_io_.reserve(spef_context_->port_names.size());

  for (Size port_idx = 0; port_idx < spef_context_->port_names.size(); ++port_idx) {
    port_io_.emplace(spef_context_->port_names[port_idx], spef_context_->port_io[port_idx]);
  }
}

void SpefDumper::buildNodeSpefNames() const
{
  const auto& nodes = topo_pool_->node_pool();

  node_spef_names_.assign(nodes.size(), Str{});
  for (const TopoNode& node : nodes) {
    node_spef_names_[topo_pool_->node_index(node)] = nodeName(node);
  }
}

void SpefDumper::buildNetSpefNames() const
{
  const auto& net_name_to_id = name_maps_.net_name_to_id;
  const Size net_num = layout_data_->regular_net_count();

  net_spef_names_.assign(net_num, Str{});
  for (Size net_idx = 0; net_idx < net_num; ++net_idx) {
    const Str& net_name = layout_data_->net_vec[net_idx].name;
    auto it = net_name_to_id.find(net_name);
    if (it != net_name_to_id.end()) {
      net_spef_names_[net_idx] = "*" + std::to_string(it->second);
    } else {
      net_spef_names_[net_idx] = net_name;
    }
  }
}

void SpefDumper::buildCouplingRefs(Size corner_idx) const
{
  const auto& edge_pool = topo_pool_->edge_pool();
  const Size net_num = layout_data_->regular_net_count();
  std::vector<Size> coupling_counts(net_num, 0);

  auto for_each_coupling = [&](auto&& fn) {
    for (const auto& [key, cap_vec] : rc_table_->merged_ccap()) {
      if (corner_idx >= cap_vec.size())
        continue;

      const double cap_ff = static_cast<double>(cap_vec[corner_idx]);
      if (cap_ff <= 0.0)
        continue;

      const Size edge_a_id = key.first;
      const Size edge_b_id = key.second;
      if (edge_a_id >= edge_pool.size() || edge_b_id >= edge_pool.size())
        continue;

      const TopoEdge& edge_a = edge_pool[edge_a_id];
      const TopoEdge& edge_b = edge_pool[edge_b_id];
      if (edge_a.net_id() >= net_num || edge_b.net_id() >= net_num)
        continue;

      fn(edge_a.net_id(), edge_a_id, edge_b_id, cap_ff);
      if (edge_b.net_id() != edge_a.net_id()) {
        fn(edge_b.net_id(), edge_b_id, edge_a_id, cap_ff);
      }
    }
  };

  for_each_coupling([&](Size net_idx, Size, Size, double) { ++coupling_counts[net_idx]; });

  net_coupling_refs_.assign(net_num, {});
  for (Size net_idx = 0; net_idx < net_num; ++net_idx) {
    net_coupling_refs_[net_idx].reserve(coupling_counts[net_idx]);
  }

  for_each_coupling([&](Size net_idx, Size self_edge_id, Size other_edge_id, double cap_ff) {
    net_coupling_refs_[net_idx].push_back({self_edge_id, other_edge_id, cap_ff});
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// nodeName
// ─────────────────────────────────────────────────────────────────────────────

Str SpefDumper::nodeName(const TopoNode& node) const
{
  if (node.is_pin_node()) {
    const Str& full_name = node.pin_name();
    // Port pin: no ':' separator
    if (full_name.find(':') == Str::npos) {
      auto it = name_maps_.port_name_to_id.find(full_name);
      if (it != name_maps_.port_name_to_id.end())
        return "*" + std::to_string(it->second);
      // Fallback: keep full name
      return full_name;
    } else {
      // Instance pin: "inst_name:pin_name"
      auto colon = full_name.find(':');
      Str inst_name = full_name.substr(0, colon);
      Str pin_name = full_name.substr(colon + 1);
      auto it = name_maps_.inst_name_to_id.find(inst_name);
      if (it != name_maps_.inst_name_to_id.end())
        return "*" + std::to_string(it->second) + ":" + pin_name;
      // Fallback: keep full name
      return full_name;
    }
  }

  // Internal node: *<net_spef_id>:<node_global_id + 1>
  Size net_id = node.net_id();
  Str net_name = layout_data_->net_vec[net_id].name;
  auto it = name_maps_.net_name_to_id.find(net_name);
  if (it != name_maps_.net_name_to_id.end())
    return "*" + std::to_string(it->second) + ":" + std::to_string(node.id() + 1);
  return "*" + net_name + ":" + std::to_string(node.id() + 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// writeHeader
// ─────────────────────────────────────────────────────────────────────────────

void SpefDumper::writeHeader(std::ofstream& ofs) const
{
  auto t = std::time(nullptr);
  auto tm = *std::localtime(&t);
  std::ostringstream date_ss;
  date_ss << std::put_time(&tm, "%a %b %d %H:%M:%S %Y");

  ofs << "*SPEF \"IEEE 1481-1998\"\n";
  ofs << "*DESIGN \"" << layout_data_->design_name << "\"\n";
  ofs << "*DATE \"" << date_ss.str() << "\"\n";
  ofs << "*VENDOR \"ECOS\"\n";
  ofs << "*PROGRAM \"iRCX\"\n";
  ofs << "*VERSION \"1.0\"\n";
  ofs << "*DESIGN_FLOW \"PIN_CAP NONE\"\n";
  ofs << "*DIVIDER /\n";
  ofs << "*DELIMITER :\n";
  ofs << "*BUS_DELIMITER []\n";
  ofs << "*T_UNIT 1.0 NS\n";
  ofs << "*C_UNIT 1.0 FF\n";
  ofs << "*R_UNIT 1.0 OHM\n";
  ofs << "*L_UNIT 1.0 HENRY\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// writeNameMap
// ─────────────────────────────────────────────────────────────────────────────

void SpefDumper::writeNameMap(std::ofstream& ofs) const
{
  ofs << "\n*NAME_MAP\n";
  // 1. Ports (IO pins)
  for (const auto& [id, name] : name_maps_.port_id_to_name) {
    ofs << "*" << id << " " << name << "\n";
  }
  // 2. instance name
  for (const auto& [id, name] : name_maps_.inst_id_to_name) {
    ofs << "*" << id << " " << name << "\n";
  }
  // 3. net name
  for (const auto& [id, name] : name_maps_.net_id_to_name) {
    ofs << "*" << id << " " << name << "\n";
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// writePorts
// ─────────────────────────────────────────────────────────────────────────────

void SpefDumper::writePorts(std::ofstream& ofs) const
{
  ofs << "\n*PORTS\n\n";
  for (Size port_idx = 0; port_idx < spef_context_->port_names.size(); ++port_idx) {
    const Str& name = spef_context_->port_names[port_idx];
    ofs << "*" << name_maps_.port_name_to_id[name] << " " << spef_context_->port_io[port_idx] << "\n";
  }
}

void SpefDumper::writeGeometry(std::ostream& /*os*/, Size /*net_idx*/) const
{
  if (!RCX_CONFIG_INST.get_report_geometry()) {
    return;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// writeDNet
// ─────────────────────────────────────────────────────────────────────────────

void SpefDumper::writeDNet(std::ostream& os, Size corner_idx, Size net_idx) const
{
  const Net& net = layout_data_->net_vec[net_idx];
  const Str& net_spef_name = net_spef_names_[net_idx];
  const auto& node_spef_name = node_spef_names_;
  const auto& port_io = port_io_;
  const auto& coupling_refs = net_coupling_refs_[net_idx];
  const auto& edge_pool = topo_pool_->edge_pool();

  std::unordered_map<Str, char> inst_pin_io;
  inst_pin_io.reserve(net.pins.size());
  for (const Pin& p : net.pins) {
    if (p.is_input && p.is_output)
      inst_pin_io[p.name] = 'B';
    else if (p.is_input)
      inst_pin_io[p.name] = 'I';
    else if (p.is_output)
      inst_pin_io[p.name] = 'O';
  }

  const auto nodes = topo_pool_->net_nodes(net_idx);
  const auto edges = topo_pool_->net_edges(net_idx);
  const Size node_offset = topo_pool_->net_node_range(net_idx).first;

  const Size node_num = nodes.size();
  if (node_num == 0)
    return;

  const Micron dbu_to_micron = dbu2micron(1, layout_data_->micron_to_dbu);

  // Aggregate ground cap to nodes
  // Ground cap per edge is split equally to its two endpoint nodes.
  auto gcap_pool = rc_table_->corner_net_gcap_pool({corner_idx, net_idx});
  std::vector<double> node_gnd(node_num, 0.0);

  for (Size edge_idx = 0; edge_idx < edges.size(); ++edge_idx) {
    const TopoEdge& edge = edges[edge_idx];
    if (edge.is_via())
      continue;
    const double ground_cap = gcap_pool[edge_idx];
    if (ground_cap <= 0.0)
      continue;
    // u()/v() are GLOBAL node indices; subtract node_offset for LOCAL index.
    node_gnd[edge.u() - node_offset] += ground_cap / 2.0;
    node_gnd[edge.v() - node_offset] += ground_cap / 2.0;
  }

  // Aggregate coupling cap to closest node pairs
  // key: (local_node_idx_self, other_node_name_string) → accumulated cap (fF)
  std::map<std::pair<Size, Str>, double> node_cc_map;

  for (const CouplingRef& coupling : coupling_refs) {
    const TopoEdge& e_self = edge_pool[coupling.self_edge_id];
    const TopoEdge& e_other = edge_pool[coupling.other_edge_id];

    // Guard against edges without valid node assignments.
    if (e_self.u() == kMaxSize || e_self.v() == kMaxSize)
      continue;
    if (e_other.u() == kMaxSize || e_other.v() == kMaxSize)
      continue;

    // Find the closest node pair between self and other edges.
    const GtlPointI& pa_u = topo_pool_->node_at(e_self.u()).point();
    const GtlPointI& pa_v = topo_pool_->node_at(e_self.v()).point();
    const GtlPointI& pb_u = topo_pool_->node_at(e_other.u()).point();
    const GtlPointI& pb_v = topo_pool_->node_at(e_other.v()).point();

    struct Cand
    {
      Size self_global;
      Size other_global;
      Dbu dist;
    };
    const Cand cands[4] = {
        {e_self.u(), e_other.u(), geom::manhattan_distance(pa_u, pb_u)},
        {e_self.u(), e_other.v(), geom::manhattan_distance(pa_u, pb_v)},
        {e_self.v(), e_other.u(), geom::manhattan_distance(pa_v, pb_u)},
        {e_self.v(), e_other.v(), geom::manhattan_distance(pa_v, pb_v)},
    };
    const auto& best = *std::min_element(std::begin(cands), std::end(cands), [](const Cand& x, const Cand& y) { return x.dist < y.dist; });

    // Convert GLOBAL ids to LOCAL for array indexing.
    const Size self_local = best.self_global - node_offset;

    // Get the SPEF name of the other net's node.
    const Str& other_node = node_spef_name[best.other_global];

    node_cc_map[{self_local, other_node}] += coupling.cap_ff;
  }

  // Total capacitance
  double tcap = 0.0;
  for (double ground_cap : node_gnd)
    tcap += ground_cap;
  for (const auto& [_, coupling_cap] : node_cc_map)
    tcap += coupling_cap;

  // Write *D_NET
  os << "\n*D_NET " << net_spef_name << " " << std::fixed << std::setprecision(6) << tcap << "\n\n";

  // *CONN section
  os << "*CONN\n";
  for (Size node_idx = 0; node_idx < node_num; ++node_idx) {  // *P
    const TopoNode& node = nodes[node_idx];
    if (!node.is_pin_node())
      continue;

    if (node.pin_name().find(':') == Str::npos) {  // port pin node
      Micron x = geom::x(node.point()) * dbu_to_micron;
      Micron y = geom::y(node.point()) * dbu_to_micron;

      os << "*" << "P" << " " << node_spef_name[topo_pool_->node_index(net_idx, node.id())] << " " << port_io.at(node.pin_name()) << " *C "
         << std::fixed << std::setprecision(3) << x << " " << y << "\n";
    }
  }

  for (Size node_idx = 0; node_idx < node_num; ++node_idx) {  // *I
    const TopoNode& node = nodes[node_idx];
    if (!node.is_pin_node())
      continue;

    if (node.pin_name().find(':') != Str::npos) {  // instance pin node
      Micron x = geom::x(node.point()) * dbu_to_micron;
      Micron y = geom::y(node.point()) * dbu_to_micron;
      const auto io_it = inst_pin_io.find(node.pin_name());
      const char pin_io = (io_it != inst_pin_io.end()) ? io_it->second : 'B';

      os << "*" << "I" << " " << node_spef_name[topo_pool_->node_index(net_idx, node.id())] << " " << pin_io << " *C " << std::fixed
         << std::setprecision(3) << x << " " << y << "\n";
    }
  }

  for (Size node_idx = 0; node_idx < node_num; ++node_idx) {  // *N
    const TopoNode& node = nodes[node_idx];
    if (node.is_pin_node())
      continue;

    Micron x = geom::x(node.point()) * dbu_to_micron;
    Micron y = geom::y(node.point()) * dbu_to_micron;

    os << "*" << "N" << " " << node_spef_name[topo_pool_->node_index(net_idx, node.id())] << " *C " << std::fixed << std::setprecision(3)
       << x << " " << y << "\n";
  }

  // *CAP section
  os << "\n*CAP\n";
  int cap_id = 1;
  // Coupling caps first.
  for (const auto& [key, coupling_cap] : node_cc_map) {
    if (coupling_cap <= 0.0)
      continue;
    os << cap_id++ << " " << node_spef_name[topo_pool_->node_index(net_idx, key.first)] << " " << key.second << " " << std::setprecision(6)
       << coupling_cap << "\n";
  }
  // Ground caps.
  for (Size node_idx = 0; node_idx < node_num; ++node_idx) {
    if (node_gnd[node_idx] <= 0.0)
      continue;
    os << cap_id++ << " " << node_spef_name[topo_pool_->node_index(net_idx, node_idx)] << " " << std::setprecision(6) << node_gnd[node_idx]
       << "\n";
  }

  // *RES section
  os << "\n*RES\n";
  int res_id = 1;
  auto res_pool = rc_table_->corner_net_res_pool({corner_idx, net_idx});
  for (Size edge_idx = 0; edge_idx < edges.size(); ++edge_idx) {
    const TopoEdge& edge = edges[edge_idx];
    if (edge.u() == kMaxSize || edge.v() == kMaxSize)
      continue;

    const double resistance = res_pool[edge_idx];

    os << res_id++ << " " << node_spef_name[edge.u()] << " " << node_spef_name[edge.v()] << " " << std::setprecision(6) << resistance
       << "\n";
  }

  writeGeometry(os, net_idx);

  os << "*END\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// dump
// ─────────────────────────────────────────────────────────────────────────────

auto SpefDumper::dump(const Str& output_dir) const -> bool
{
  if (corner_data_ == nullptr || corner_data_->empty()) {
    LOG_ERROR << "SpefDumper: process corners not set.";
    return false;
  }

  for (Size corner_idx = 0; corner_idx < corner_data_->size(); ++corner_idx) {
    if (!dumpCorner(output_dir, corner_idx)) {
      return false;
    }
  }

  return true;
}

auto SpefDumper::dumpCorner(const Str& output_dir, Size corner_idx) const -> bool
{
  const auto& corner_data = (*corner_data_)[corner_idx];
  if (corner_data.process_corner == nullptr) {
    LOG_ERROR << "SpefDumper: process corner missing for corner " << corner_data.name;
    return false;
  }

  Str corner_name = corner_data.process_corner->get_technology();
  Str filename = output_dir + "/" + layout_data_->design_name + "_" + corner_name + ".spef";
  std::ofstream ofs(filename);
  if (!ofs) {
    LOG_ERROR << "SpefDumper: cannot open output file: " << filename;
    return false;
  }

  const Size net_count = layout_data_->regular_net_count();

  buildNameMaps();
  buildPortIo();
  buildNodeSpefNames();
  buildNetSpefNames();
  buildCouplingRefs(corner_idx);
  net_str_buffer_.assign(net_count, Str{});

  writeHeader(ofs);
  writeNameMap(ofs);
  writePorts(ofs);

#pragma omp parallel for schedule(dynamic)
  for (Size net_idx = 0; net_idx < net_count; ++net_idx) {
    std::ostringstream net_os;
    writeDNet(net_os, corner_idx, net_idx);
    net_str_buffer_[net_idx] = net_os.str();
  }

  for (const Str& net_str : net_str_buffer_) {
    ofs << net_str;
  }

  LOG_INFO << "SpefDumper: wrote " << filename;
  return true;
}

}  // namespace ircx
