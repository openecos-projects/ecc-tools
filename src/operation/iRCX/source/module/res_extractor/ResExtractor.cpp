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

#include "EdgeEtchInterval.hpp"
#include "ProcessConductor.hpp"
#include "ProcessEffectType.hpp"
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
  int32_t net_num = RCXDM.getDatabase().get_layout_data().get_regular_net_num();
  int32_t thread_num = RCXUTIL.getThreadNum(net_num, RCXDM.getConfig().thread_number);
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    extractNetResistance(corner_idx, net_idx);
  }
}

void ResExtractor::extractNetResistance(int32_t corner_idx, int32_t net_idx)
{
  Database& database = RCXDM.getDatabase();
  CornerData& corner_data = database.get_corner_data_list()[corner_idx];
  std::span<TopoEdge> edge_list = database.get_topo_pool().get_net_edge_list(net_idx);
  std::vector<double>& resistance_list = database.get_rc_data().get_corner_net_resistance_list(CornerNetIdx(corner_idx, net_idx));
  NetEtchProfile& net_etch_profile = database.get_corner_net_etch_profile_pool().get_item(CornerNetIdx(corner_idx, net_idx));

  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(edge_list.size()); ++edge_idx) {
    TopoEdge& edge = edge_list[edge_idx];
    if (edge.get_is_via()) {
      ProcessVia* via = getProcessVia(corner_data, edge.get_layer_idx());
      if (via != nullptr) {
        resistance_list[edge_idx] = extractViaResistance(corner_data, *via, edge);
      }
      continue;
    }

    ProcessConductor* conductor = getProcessConductor(corner_data, edge.get_layer_idx());
    if (conductor == nullptr) {
      continue;
    }

    std::span<EdgeEtchInterval> edge_interval_list = net_etch_profile.get_edge_interval_list(edge_idx);
    resistance_list[edge_idx] = extractWireResistance(corner_data, *conductor, edge, edge_interval_list);
  }
}

double ResExtractor::extractWireResistance(CornerData& corner_data, ProcessConductor& conductor, TopoEdge& edge, std::span<EdgeEtchInterval> edge_interval_list)
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

  double resistance = 0.0;
  for (EdgeEtchInterval& edge_interval : edge_interval_list) {
    double overlap_start = std::max(edge_interval.get_start_coord(), segment_start);
    double overlap_end = std::min(edge_interval.get_end_coord(), segment_end);
    if (overlap_end <= overlap_start) {
      continue;
    }

    double length = overlap_end - overlap_start;
    double width = edge_interval.get_width();
    double thickness = edge_interval.get_thickness();
    if (width <= 0.0 || thickness <= 0.0) {
      continue;
    }

    std::optional<double> resistivity = conductor.get_resistivity_by_width_thickness_table().query(thickness, width);
    double resistivity_value = resistivity.has_value() ? resistivity.value() : conductor.get_resistivity();
    double sheet_resistance = 0.0;
    if (resistivity_value <= 0.0) {
      std::optional<double> sheet_resistance_by_width = conductor.get_sheet_resistance_by_width_table().query(width);
      sheet_resistance = sheet_resistance_by_width.has_value() ? sheet_resistance_by_width.value() : conductor.get_sheet_resistance();
    }

    double base_resistance = 0.0;
    if (resistivity_value > 0.0) {
      base_resistance = resistivity_value * length / (width * thickness);
    }
    if (sheet_resistance > 0.0) {
      base_resistance += sheet_resistance * length / width;
    }

    double tmpr_coefficient1 = 0.0;
    double tmpr_coefficient2 = 0.0;
    conductor.query_tmpr_coefficient(width, tmpr_coefficient1, tmpr_coefficient2);
    double nominal_tmpr = conductor.get_has_nominal_tmpr() ? conductor.get_nominal_tmpr() : corner_data.get_global_tmpr();
    resistance += base_resistance * getTmprFactor(corner_data.get_tmpr(), nominal_tmpr, tmpr_coefficient1, tmpr_coefficient2);
  }
  return resistance;
}

double ResExtractor::extractViaResistance(CornerData& corner_data, ProcessVia& via, TopoEdge& edge)
{
  Database& database = RCXDM.getDatabase();
  double micron_per_dbu = 1 / 1.0 / database.get_layout_data().get_dbu_per_micron();
  GTLRectInt& via_shape = edge.get_shape();
  double x_span = (RCXUTIL.maxX(via_shape) - RCXUTIL.minX(via_shape)) * micron_per_dbu;
  double y_span = (RCXUTIL.maxY(via_shape) - RCXUTIL.minY(via_shape)) * micron_per_dbu;
  double length = std::max(x_span, y_span) * corner_data.get_half_node_scale_factor();
  double width = std::min(x_span, y_span) * corner_data.get_half_node_scale_factor();
  ViaEtch etch = via.query_etch(ProcessEffectType::kResistance, width, length);
  length = std::max<double>(0.0, length - 2.0 * etch.get_length());
  width = std::max<double>(0.0, width - 2.0 * etch.get_width());
  double area = length * width;
  std::optional<double> base_resistance = via.query_resistance(area);
  if (!base_resistance.has_value()) {
    return 0.0;
  }

  double tmpr_coefficient1 = 0.0;
  double tmpr_coefficient2 = 0.0;
  via.query_tmpr_coefficient(area, tmpr_coefficient1, tmpr_coefficient2);
  double nominal_tmpr = via.get_has_nominal_tmpr() ? via.get_nominal_tmpr() : corner_data.get_global_tmpr();
  return base_resistance.value() * getTmprFactor(corner_data.get_tmpr(), nominal_tmpr, tmpr_coefficient1, tmpr_coefficient2);
}

double ResExtractor::getTmprFactor(double tmpr, double nominal_tmpr, double tmpr_coefficient1, double tmpr_coefficient2)
{
  double tmpr_delta = tmpr - nominal_tmpr;
  return 1.0 + tmpr_coefficient1 * tmpr_delta + tmpr_coefficient2 * tmpr_delta * tmpr_delta;
}

ProcessVia* ResExtractor::getProcessVia(CornerData& corner_data, int32_t design_layer_idx)
{
  LayerTable& layer_table = RCXDM.getDatabase().get_layer_table();
  std::unordered_map<int32_t, std::string>& design_idx_to_name_map = layer_table.get_design_idx_to_name_map();
  if (design_idx_to_name_map.count(design_layer_idx) == 0) {
    return nullptr;
  }

  std::string& design_layer_name = design_idx_to_name_map[design_layer_idx];
  std::unordered_map<std::string, std::string>& design_name_to_process_name_map = layer_table.get_design_name_to_process_name_map();
  if (design_name_to_process_name_map.count(design_layer_name) == 0) {
    return nullptr;
  }

  std::string& process_layer_name = design_name_to_process_name_map[design_layer_name];
  for (ProcessVia& via : corner_data.get_process_via_list()) {
    if (via.get_layer_name() == process_layer_name) {
      return &via;
    }
  }
  return nullptr;
}

ProcessConductor* ResExtractor::getProcessConductor(CornerData& corner_data, int32_t design_layer_idx)
{
  LayerTable& layer_table = RCXDM.getDatabase().get_layer_table();
  std::unordered_map<int32_t, std::string>& design_idx_to_name_map = layer_table.get_design_idx_to_name_map();
  if (design_idx_to_name_map.count(design_layer_idx) == 0) {
    return nullptr;
  }

  std::string& design_layer_name = design_idx_to_name_map[design_layer_idx];
  std::unordered_map<std::string, std::string>& design_name_to_process_name_map = layer_table.get_design_name_to_process_name_map();
  if (design_name_to_process_name_map.count(design_layer_name) == 0) {
    return nullptr;
  }

  std::string& process_layer_name = design_name_to_process_name_map[design_layer_name];
  for (ProcessConductor& conductor : corner_data.get_process_conductor_list()) {
    if (conductor.get_layer_name() == process_layer_name) {
      return &conductor;
    }
  }
  return nullptr;
}

}  // namespace ircx
