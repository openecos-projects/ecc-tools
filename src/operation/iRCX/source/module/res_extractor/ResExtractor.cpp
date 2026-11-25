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
#include "ResExtractor.hpp"

#include <cmath>

#include "EdgeEtchInterval.hpp"
#include "ProcessBinding.hpp"
#include "ProcessConductor.hpp"
#include "ProcessEffectType.hpp"
#include "ProcessEvaluator.hpp"
#include "ProcessVia.hpp"
#include "TopoEdge.hpp"
#include "Utility.hpp"

namespace ircx {

// public

void ResExtractor::initInst()
{
  if (_re_instance == nullptr) {
    _re_instance = new ResExtractor();
  }
}

ResExtractor& ResExtractor::getInst()
{
  if (_re_instance == nullptr) {
    RCXLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_re_instance;
}

void ResExtractor::destroyInst()
{
  if (_re_instance != nullptr) {
    delete _re_instance;
    _re_instance = nullptr;
  }
}

// function

void ResExtractor::extract()
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");

  extractResistance();

  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

ResExtractor* ResExtractor::_re_instance = nullptr;

void ResExtractor::extractResistance()
{
  Database& database = RCXDM.getDatabase();
  int32_t corner_num = static_cast<int32_t>(database.get_corner_data_list().size());
  int32_t net_num = database.get_layout_data().get_regular_net_num();
  database.get_rc_data().init(corner_num, net_num, database.get_topo_pool());

  for (int32_t corner_idx = 0; corner_idx < corner_num; ++corner_idx) {
    extractCornerResistance(corner_idx);
  }
}

void ResExtractor::extractCornerResistance(int32_t corner_idx)
{
  const double scale = RCXDM.getDatabase().get_corner_data_list()[corner_idx].get_half_node_scale_factor();
  if (!std::isfinite(scale) || scale <= 0.0) {
    RCXLOG.error(Loc::current(), "invalid resistance HALF_NODE_SCALE_FACTOR");
  }
  int32_t net_num = RCXDM.getDatabase().get_layout_data().get_regular_net_num();
  int32_t thread_num = RCXUTIL.getThreadNum(net_num, RCXDM.getConfig().thread_number);
  std::vector<std::string> errors(net_num);
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    errors[net_idx] = extractNetResistance(corner_idx, net_idx);
  }
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    if (!errors[net_idx].empty())
      RCXLOG.error(Loc::current(), "Resistance extraction failed for net ", net_idx, ": ", errors[net_idx]);
  }
}

std::string ResExtractor::extractNetResistance(int32_t corner_idx, int32_t net_idx)
{
  Database& database = RCXDM.getDatabase();
  CornerData& corner_data = database.get_corner_data_list()[corner_idx];
  std::span<TopoEdge> edge_list = database.get_topo_pool().get_net_edge_list(net_idx);
  std::vector<double>& resistance_list = database.get_rc_data().get_corner_net_resistance_list(CornerNetIdx(corner_idx, net_idx));
  NetEtchProfile& net_etch_profile = database.get_corner_net_etch_profile_pool().get_item(CornerNetIdx(corner_idx, net_idx));

  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_list.size()); ++edge_idx) {
    TopoEdge& edge = edge_list[edge_idx];
    if (edge.get_is_via()) {
      const ProcessVia* via = getProcessVia(corner_data, edge.get_layer_idx());
      if (!via)
        return "missing via process binding for design layer " + std::to_string(edge.get_layer_idx());
      auto value = extractViaResistance(corner_data, *via, edge);
      if (!value)
        return value.error;
      resistance_list[edge_idx] = *value;
      continue;
    }

    const ProcessConductor* conductor = getProcessConductor(corner_data, edge.get_layer_idx());
    if (!conductor)
      return "missing conductor process binding for design layer " + std::to_string(edge.get_layer_idx());

    std::span<EdgeEtchInterval> edge_interval_list = net_etch_profile.get_edge_interval_list(edge_idx);
    auto value = extractWireResistance(corner_data, *conductor, edge, edge_interval_list);
    if (!value)
      return value.error;
    resistance_list[edge_idx] = *value;
  }
  return {};
}

ProcessResult<double> ResExtractor::extractWireResistance(CornerData& corner_data, const ProcessConductor& conductor, TopoEdge& edge,
                                                          std::span<EdgeEtchInterval> edge_interval_list)
{
  Database& database = RCXDM.getDatabase();
  TopoNode& start_node = database.get_topo_pool().get_node(edge.get_start_node_idx());
  TopoNode& end_node = database.get_topo_pool().get_node(edge.get_end_node_idx());
  double micron_per_dbu = 1 / 1.0 / database.get_layout_data().get_dbu_per_micron();
  double segment_start
      = edge.get_line_segment().get_is_horizontal() ? RCXUTIL.x(start_node.get_point()) * micron_per_dbu : RCXUTIL.y(start_node.get_point()) * micron_per_dbu;
  double segment_end
      = edge.get_line_segment().get_is_horizontal() ? RCXUTIL.x(end_node.get_point()) * micron_per_dbu : RCXUTIL.y(end_node.get_point()) * micron_per_dbu;
  if (segment_end < segment_start) {
    std::swap(segment_start, segment_end);
  }

  const double scale = corner_data.get_half_node_scale_factor();
  segment_start *= scale;
  segment_end *= scale;
  double resistance = 0.0;
  for (const EdgeEtchInterval& edge_interval : edge_interval_list) {
    const double width = edge.get_width() * micron_per_dbu * scale;
    const double lower_spacing = edge_interval.get_lower_spacing() < 0 ? -1 : edge_interval.get_lower_spacing() * scale;
    const double upper_spacing = edge_interval.get_upper_spacing() < 0 ? -1 : edge_interval.get_upper_spacing() * scale;
    if (edge_interval.get_resistance_segments().empty())
      return std::string("missing finalized resistance geometry");
    for (const auto& segment : edge_interval.get_resistance_segments()) {
      const double start = std::max(segment.start, segment_start);
      const double end = std::min(segment.end, segment_end);
      if (end <= start) continue;
      const auto value = evaluateWireResistanceFromGeometry(conductor, {width, lower_spacing, upper_spacing}, segment.geometry,
          end - start, corner_data.get_tmpr(), corner_data.get_global_tmpr());
      if (!value) return value.error;
      resistance += *value;
    }
  }
  return resistance;
}

ProcessResult<double> ResExtractor::extractViaResistance(CornerData& corner_data, const ProcessVia& via, TopoEdge& edge)
{
  Database& database = RCXDM.getDatabase();
  double micron_per_dbu = 1 / 1.0 / database.get_layout_data().get_dbu_per_micron();
  GTLRectInt& via_shape = edge.get_shape();
  double x_span = (RCXUTIL.maxX(via_shape) - RCXUTIL.minX(via_shape)) * micron_per_dbu;
  double y_span = (RCXUTIL.maxY(via_shape) - RCXUTIL.minY(via_shape)) * micron_per_dbu;
  double length = std::max(x_span, y_span) * corner_data.get_half_node_scale_factor();
  double width = std::min(x_span, y_span) * corner_data.get_half_node_scale_factor();
  return evaluateViaResistance(via, width, length, corner_data.get_tmpr(), corner_data.get_global_tmpr());
}

const ProcessVia* ResExtractor::getProcessVia(CornerData& corner_data, int32_t design_layer_idx)
{
  return findProcessVia(corner_data, RCXDM.getDatabase().get_layer_table(), design_layer_idx);
}

const ProcessConductor* ResExtractor::getProcessConductor(CornerData& corner_data, int32_t design_layer_idx)
{
  return findProcessConductor(corner_data, RCXDM.getDatabase().get_layer_table(), design_layer_idx);
}

}  // namespace ircx
