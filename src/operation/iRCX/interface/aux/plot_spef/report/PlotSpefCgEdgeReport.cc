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
 * @file PlotSpefCgEdgeReport.cc
 * @brief Write SPEF *RES indexes for ground caps that can be assigned to edges.
 */
#include "report/PlotSpefCgEdgeReport.hh"

#include "Logger.hpp"
#include "PathUtils.hh"
#include "RCXHeader.hpp"
#include "config/PlotSpefConfig.hh"
#include "model/PlotSpefModel.hh"

namespace ircx::plot_spef {
namespace {

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

auto csvText(const std::string& text) -> std::string
{
  std::string result = "\"";
  for (const char ch : text) {
    if (ch == '"') {
      result += "\"\"";
    } else {
      result.push_back(ch);
    }
  }
  result.push_back('"');
  return result;
}

auto edgeKey(const Model& model, const EdgeRef& ref) -> std::string
{
  const Resistor* edge = findEdge(model, ref);
  if (edge == nullptr || ref.net_index >= model.nets.size()) {
    return {};
  }
  return model.nets[ref.net_index].name + ":" + std::to_string(edge->index);
}

auto addEdgeRow(const Model& model, const EdgeRef& ref, std::unordered_map<std::string, Size>& index_by_key, std::vector<EdgeRow>& rows) -> void
{
  const Resistor* edge = findEdge(model, ref);
  if (edge == nullptr || ref.net_index >= model.nets.size()) {
    return;
  }

  const auto& edge_net = model.nets[ref.net_index];
  const std::string key = edgeKey(model, ref);
  if (key.empty()) {
    return;
  }
  const bool inserted = index_by_key.emplace(key, rows.size()).second;
  if (inserted) {
    rows.push_back(EdgeRow{.net_name = edge_net.name, .res_index = edge->index});
  }
}

auto sortRows(std::vector<EdgeRow>& rows) -> void
{
  std::sort(rows.begin(), rows.end(), [](const EdgeRow& lhs, const EdgeRow& rhs) {
    if (lhs.net_name != rhs.net_name) {
      return lhs.net_name < rhs.net_name;
    }
    return lhs.res_index < rhs.res_index;
  });
}

}  // namespace

auto collectCgEdgeRows(const Model& model) -> std::vector<EdgeRow>
{
  std::unordered_map<std::string, Size> index_by_key;
  std::vector<EdgeRow> rows;
  for (const auto& net : model.nets) {
    for (const auto& cap : net.ground_caps) {
      addEdgeRow(model, cap.edge1, index_by_key, rows);
    }
  }

  sortRows(rows);
  return rows;
}

auto collectCoupledEdgeRows(const Model& model) -> std::vector<EdgeRow>
{
  std::unordered_map<std::string, Size> index_by_key;
  std::vector<EdgeRow> rows;
  for (const auto& net : model.nets) {
    for (const auto& cap : net.coupling_caps) {
      addEdgeRow(model, cap.edge1, index_by_key, rows);
      addEdgeRow(model, cap.edge2, index_by_key, rows);
    }
  }

  sortRows(rows);
  return rows;
}

auto CgEdgeReport::write(const Model& model, const Config& config) const -> bool
{
  const auto report_file = path::fileUnderDir(config.output_dir, "cg_edges", ".csv");
  std::ofstream out(report_file);
  if (!out.is_open()) {
    RCXLOG.warn(Loc::current(), "plot_spef failed: cannot open CG edge report ", report_file.string());
    return false;
  }

  out << "net,index\n";
  for (const auto& row : collectCgEdgeRows(model)) {
    out << csvText(row.net_name) << ',' << row.res_index << '\n';
  }

  RCXLOG.info(Loc::current(), "plot_spef wrote CG edge report to ", report_file.string());
  return true;
}

}  // namespace ircx::plot_spef
