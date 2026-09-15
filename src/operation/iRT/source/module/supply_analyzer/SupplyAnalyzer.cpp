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
#include "SupplyAnalyzer.hpp"

#include "GDSPlotter.hpp"
#include "GPGDS.hpp"
#include "Monitor.hpp"
#include "RTHeader.hpp"
#include "SAModel.hpp"

namespace irt {

// public

void SupplyAnalyzer::initInst()
{
  if (_sa_instance == nullptr) {
    _sa_instance = new SupplyAnalyzer();
  }
}

SupplyAnalyzer& SupplyAnalyzer::getInst()
{
  if (_sa_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_sa_instance;
}

void SupplyAnalyzer::destroyInst()
{
  if (_sa_instance != nullptr) {
    delete _sa_instance;
    _sa_instance = nullptr;
  }
}

// function

void SupplyAnalyzer::analyze()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  SAModel sa_model = initSAModel();
  initRoutingEdgeMap();
  buildSupplySchedule(sa_model);
  analyzeSupply(sa_model);
  // debugPlotSAModel(sa_model);
  updateSummary(sa_model);
  printSummary(sa_model);
  outputPlanarSupplyCSV(sa_model);
  outputLayerSupplyCSV(sa_model);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

SupplyAnalyzer* SupplyAnalyzer::_sa_instance = nullptr;

SAModel SupplyAnalyzer::initSAModel()
{
  SAModel sa_model;
  return sa_model;
}

void SupplyAnalyzer::initRoutingEdgeMap()
{
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();

  routing_h_edge_map.resize(routing_layer_list.size());
  routing_v_edge_map.resize(routing_layer_list.size());
#pragma omp parallel for
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_layer_list.size()); layer_idx++) {
    routing_h_edge_map[layer_idx].init(std::max(0, gcell_map.get_x_size() - 1), gcell_map.get_y_size());
    routing_v_edge_map[layer_idx].init(gcell_map.get_x_size(), std::max(0, gcell_map.get_y_size() - 1));
  }
}

void SupplyAnalyzer::buildSupplySchedule(SAModel& sa_model)
{
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t bottom_routing_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_routing_layer_idx = RTDM.getConfig().top_routing_layer_idx;
  std::vector<std::vector<std::pair<LayerCoord, LayerCoord>>>& grid_pair_list_list = sa_model.get_grid_pair_list_list();
  grid_pair_list_list.reserve(2 * routing_layer_list.size());

  for (RoutingLayer& routing_layer : routing_layer_list) {
    if (routing_layer.get_layer_idx() < bottom_routing_layer_idx || top_routing_layer_idx < routing_layer.get_layer_idx()) {
      continue;
    }
    if (routing_layer.isPreferH()) {
      for (int32_t begin_x = 1; begin_x <= 2; begin_x++) {
        std::vector<std::pair<LayerCoord, LayerCoord>> grid_pair_list;
        grid_pair_list.reserve(static_cast<size_t>(die.getXSize()) * die.getYSize() / 2);
        for (int32_t y = 0; y < die.getYSize(); y++) {
          for (int32_t x = begin_x; x < die.getXSize(); x += 2) {
            grid_pair_list.emplace_back(LayerCoord(x - 1, y, routing_layer.get_layer_idx()), LayerCoord(x, y, routing_layer.get_layer_idx()));
          }
        }
        grid_pair_list_list.push_back(std::move(grid_pair_list));
      }
    } else {
      for (int32_t begin_y = 1; begin_y <= 2; begin_y++) {
        std::vector<std::pair<LayerCoord, LayerCoord>> grid_pair_list;
        grid_pair_list.reserve(static_cast<size_t>(die.getXSize()) * die.getYSize() / 2);
        for (int32_t x = 0; x < die.getXSize(); x++) {
          for (int32_t y = begin_y; y < die.getYSize(); y += 2) {
            grid_pair_list.emplace_back(LayerCoord(x, y - 1, routing_layer.get_layer_idx()), LayerCoord(x, y, routing_layer.get_layer_idx()));
          }
        }
        grid_pair_list_list.push_back(std::move(grid_pair_list));
      }
    }
  }
}

void SupplyAnalyzer::analyzeSupply(SAModel& sa_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();

  using DetailedRTree = bgi::rtree<std::pair<BGRectInt, int32_t>, bgi::quadratic<16>>;
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<std::vector<std::pair<int32_t, PlanarRect>>> layer_detailed_shape_list(routing_layer_list.size());
  std::vector<std::vector<std::pair<BGRectInt, int32_t>>> layer_rtree_value_list(routing_layer_list.size());
  Die& die = RTDM.getDatabase().get_die();
  int32_t detection_distance = RTDM.getDatabase().get_detection_distance();
  for (auto& [net_idx, segment_list] : RTDM.getDatabase().get_net_detailed_result_map()) {
    for (Segment<LayerCoord>& segment : segment_list) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, segment)) {
        if (!net_shape.get_is_routing()) {
          continue;
        }
        PlanarRect real_rect = net_shape;
        if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
          continue;
        }
        int32_t layer_idx = net_shape.get_layer_idx();
        std::vector<std::pair<int32_t, PlanarRect>>& detailed_shape_list = layer_detailed_shape_list[layer_idx];
        layer_rtree_value_list[layer_idx].emplace_back(RTUTIL.convertToBGRectInt(real_rect), static_cast<int32_t>(detailed_shape_list.size()));
        detailed_shape_list.emplace_back(net_idx, net_shape);
      }
    }
  }
  for (auto& [net_idx, patch_list] : RTDM.getDatabase().get_net_detailed_patch_map()) {
    for (EXTLayerRect& patch : patch_list) {
      PlanarRect real_rect = patch.get_real_rect();
      if (!RTUTIL.hasRegularRect(real_rect, die.get_real_rect())) {
        continue;
      }
      int32_t layer_idx = patch.get_layer_idx();
      std::vector<std::pair<int32_t, PlanarRect>>& detailed_shape_list = layer_detailed_shape_list[layer_idx];
      layer_rtree_value_list[layer_idx].emplace_back(RTUTIL.convertToBGRectInt(real_rect), static_cast<int32_t>(detailed_shape_list.size()));
      detailed_shape_list.emplace_back(net_idx, patch.get_real_rect());
    }
  }
  std::vector<DetailedRTree> layer_rtree_list(routing_layer_list.size());
#pragma omp parallel for
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_layer_list.size()); layer_idx++) {
    layer_rtree_list[layer_idx] = DetailedRTree(layer_rtree_value_list[layer_idx].begin(), layer_rtree_value_list[layer_idx].end());
  }
  std::vector<std::vector<std::pair<BGRectInt, int32_t>>>().swap(layer_rtree_value_list);

  size_t total_pair_num = 0;
  for (std::vector<std::pair<LayerCoord, LayerCoord>>& grid_pair_list : sa_model.get_grid_pair_list_list()) {
    total_pair_num += grid_pair_list.size();
  }

  size_t analyzed_pair_num = 0;
  for (std::vector<std::pair<LayerCoord, LayerCoord>>& grid_pair_list : sa_model.get_grid_pair_list_list()) {
    Monitor stage_monitor;
#pragma omp parallel for
    for (std::pair<LayerCoord, LayerCoord>& grid_pair : grid_pair_list) {
      LayerCoord first_coord = grid_pair.first;
      LayerCoord second_coord = grid_pair.second;
      EXTLayerRect search_rect = getSearchRect(first_coord, second_coord);

      bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
      RoutingEdge& routing_edge = is_horizontal ? routing_h_edge_map[first_coord.get_layer_idx()][first_coord.get_x()][first_coord.get_y()]
                                                : routing_v_edge_map[first_coord.get_layer_idx()][first_coord.get_x()][first_coord.get_y()];
      std::set<int32_t>& ignore_net_set = routing_edge.get_ignore_net_set();

      std::vector<PlanarRect> obs_rect_list;
      {
        for (auto& [net_idx, fixed_rect_set] : RTDM.getNetFixedRectMap(true, search_rect)) {
          for (EXTLayerRect* fixed_rect : fixed_rect_set) {
            obs_rect_list.push_back(fixed_rect->get_real_rect());
          }
        }

        PlanarRect query_real_rect = RTUTIL.getEnlargedRect(search_rect.get_real_rect(), detection_distance);
        BGRectInt query_rect = RTUTIL.convertToBGRectInt(query_real_rect);
        std::vector<std::pair<BGRectInt, int32_t>> rtree_value_list;
        layer_rtree_list[search_rect.get_layer_idx()].query(bgi::intersects(query_rect), std::back_inserter(rtree_value_list));
        for (auto& [rect, shape_idx] : rtree_value_list) {
          auto& [net_idx, detailed_shape] = layer_detailed_shape_list[search_rect.get_layer_idx()][shape_idx];
          obs_rect_list.push_back(detailed_shape);
          if (RTUTIL.isOpenOverlap(search_rect.get_real_rect(), detailed_shape)) {
            ignore_net_set.insert(net_idx);
          }
        }
      }
      std::vector<LayerRect> wire_list = getCrossingWireList(search_rect);
      int32_t supply = 0;
      for (LayerRect& wire : wire_list) {
        if (isAccess(wire, obs_rect_list)) {
          supply++;
        }
      }
      supply = std::min(supply, static_cast<int32_t>(wire_list.size()) - static_cast<int32_t>(ignore_net_set.size()));
      supply = std::max(0, supply);
      supply = static_cast<int32_t>(supply * 0.9);
      routing_edge.set_supply(supply);
    }
    analyzed_pair_num += grid_pair_list.size();
    RTLOG.info(Loc::current(), "Analyzed ", analyzed_pair_num, "/", total_pair_num, "(", RTUTIL.getPercentage(analyzed_pair_num, total_pair_num),
               ") grid pairs", stage_monitor.getStatsInfo());
    std::vector<std::pair<LayerCoord, LayerCoord>>().swap(grid_pair_list);
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

EXTLayerRect SupplyAnalyzer::getSearchRect(LayerCoord& first_coord, LayerCoord& second_coord)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
    RTLOG.error(Loc::current(), "The grid_pair layer_idx is not equal!");
  }
  PlanarRect search_real_rect;
  {
    PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
    PlanarCoord first_mid_coord = first_real_rect.getMidPoint();
    PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
    PlanarCoord second_mid_coord = second_real_rect.getMidPoint();
    if (RTUTIL.isHorizontal(first_coord, second_coord)) {
      std::vector<PlanarCoord> coord_list;
      coord_list.emplace_back(first_mid_coord.get_x(), first_real_rect.get_ll_y());
      coord_list.emplace_back(first_mid_coord.get_x(), first_real_rect.get_ur_y());
      coord_list.emplace_back(second_mid_coord.get_x(), second_real_rect.get_ll_y());
      coord_list.emplace_back(second_mid_coord.get_x(), second_real_rect.get_ur_y());
      search_real_rect = RTUTIL.getBoundingBox(coord_list);
    } else if (RTUTIL.isVertical(first_coord, second_coord)) {
      std::vector<PlanarCoord> coord_list;
      coord_list.emplace_back(first_real_rect.get_ll_x(), first_mid_coord.get_y());
      coord_list.emplace_back(first_real_rect.get_ur_x(), first_mid_coord.get_y());
      coord_list.emplace_back(second_real_rect.get_ll_x(), second_mid_coord.get_y());
      coord_list.emplace_back(second_real_rect.get_ur_x(), second_mid_coord.get_y());
      search_real_rect = RTUTIL.getBoundingBox(coord_list);
    }
  }
  EXTLayerRect search_rect;
  search_rect.set_real_rect(search_real_rect);
  search_rect.set_grid_rect(RTUTIL.getClosedGCellGridRect(search_rect.get_real_rect(), gcell_axis));
  search_rect.set_layer_idx(first_coord.get_layer_idx());
  return search_rect;
}

std::vector<LayerRect> SupplyAnalyzer::getCrossingWireList(EXTLayerRect& search_rect)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  RoutingLayer& routing_layer = routing_layer_list[search_rect.get_layer_idx()];
  int32_t half_wire_width = routing_layer.get_min_width() / 2;

  int32_t real_ll_x = search_rect.get_real_ll_x();
  int32_t real_ll_y = search_rect.get_real_ll_y();
  int32_t real_ur_x = search_rect.get_real_ur_x();
  int32_t real_ur_y = search_rect.get_real_ur_y();

  std::vector<LayerRect> wire_list;
  if (routing_layer.isPreferH()) {
    for (int32_t y : RTUTIL.getScaleList(real_ll_y, real_ur_y, routing_layer.getYTrackGridList())) {
      wire_list.emplace_back(real_ll_x, y - half_wire_width, real_ur_x, y + half_wire_width, search_rect.get_layer_idx());
    }
  } else {
    for (int32_t x : RTUTIL.getScaleList(real_ll_x, real_ur_x, routing_layer.getXTrackGridList())) {
      wire_list.emplace_back(x - half_wire_width, real_ll_y, x + half_wire_width, real_ur_y, search_rect.get_layer_idx());
    }
  }
  return wire_list;
}

bool SupplyAnalyzer::isAccess(LayerRect& wire, std::vector<PlanarRect>& obs_rect_list)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  RoutingLayer& routing_layer = routing_layer_list[wire.get_layer_idx()];

  for (PlanarRect& obs_rect : obs_rect_list) {
    int32_t enlarged_size = routing_layer.getPRLSpacing(obs_rect);
    PlanarRect enlarged_rect = RTUTIL.getEnlargedRect(obs_rect, enlarged_size);
    if (RTUTIL.isOpenOverlap(enlarged_rect, wire)) {
      // 阻塞
      return false;
    }
  }
  return true;
}

#if 1  // exhibit

void SupplyAnalyzer::updateSummary(SAModel& sa_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  Summary& summary = RTDM.getDatabase().get_summary();

  std::map<int32_t, int32_t>& routing_supply_map = summary.sa_summary.routing_supply_map;
  int32_t& total_supply = summary.sa_summary.total_supply;

  routing_supply_map.clear();
  total_supply = 0;

  std::vector<int32_t> routing_supply_list(routing_layer_list.size(), 0);
#pragma omp parallel for
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_layer_list.size()); layer_idx++) {
    int32_t routing_supply = 0;
    for (GridMap<RoutingEdge>* routing_edge_map : {&routing_h_edge_map[layer_idx], &routing_v_edge_map[layer_idx]}) {
      for (int32_t x = 0; x < routing_edge_map->get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map->get_y_size(); y++) {
          routing_supply += (*routing_edge_map)[x][y].get_supply();
        }
      }
    }
    routing_supply_list[layer_idx] = routing_supply;
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    int32_t layer_idx = routing_layer.get_layer_idx();
    routing_supply_map[layer_idx] = routing_supply_list[layer_idx];
    total_supply += routing_supply_list[layer_idx];
  }
}

void SupplyAnalyzer::printSummary(SAModel& sa_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  Summary& summary = RTDM.getDatabase().get_summary();

  std::map<int32_t, int32_t>& routing_supply_map = summary.sa_summary.routing_supply_map;
  int32_t& total_supply = summary.sa_summary.total_supply;

  fort::char_table routing_supply_map_table;
  {
    routing_supply_map_table.set_cell_text_align(fort::text_align::right);
    routing_supply_map_table << fort::header << "routing"
                             << "supply"
                             << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_supply_map_table << routing_layer.get_layer_name() << routing_supply_map[routing_layer.get_layer_idx()]
                               << RTUTIL.getPercentage(routing_supply_map[routing_layer.get_layer_idx()], total_supply) << fort::endr;
    }
    routing_supply_map_table << fort::header << "Total" << total_supply << RTUTIL.getPercentage(total_supply, total_supply) << fort::endr;
  }
  RTUTIL.printTableList({routing_supply_map_table});
}

void SupplyAnalyzer::outputPlanarSupplyCSV(SAModel& sa_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  std::string& sa_temp_directory_path = RTDM.getConfig().sa_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  for (std::pair<std::string, std::vector<GridMap<RoutingEdge>>*> edge_map_pair :
       {std::make_pair("h_supply_map.csv", &routing_h_edge_map), std::make_pair("v_supply_map.csv", &routing_v_edge_map)}) {
    GridMap<RoutingEdge>& routing_edge_map = edge_map_pair.second->front();
    std::ofstream* supply_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(sa_temp_directory_path, edge_map_pair.first));
    for (int32_t y = routing_edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        int32_t total_supply = 0;
        for (RoutingLayer& routing_layer : routing_layer_list) {
          total_supply += (*edge_map_pair.second)[routing_layer.get_layer_idx()][x][y].get_supply();
        }
        RTUTIL.pushStream(supply_csv_file, total_supply, ",");
      }
      RTUTIL.pushStream(supply_csv_file, "\n");
    }
    RTUTIL.closeFileStream(supply_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void SupplyAnalyzer::outputLayerSupplyCSV(SAModel& sa_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  std::string& sa_temp_directory_path = RTDM.getConfig().sa_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  for (RoutingLayer& routing_layer : routing_layer_list) {
    GridMap<RoutingEdge>& routing_edge_map
        = routing_layer.isPreferH() ? routing_h_edge_map[routing_layer.get_layer_idx()] : routing_v_edge_map[routing_layer.get_layer_idx()];
    std::ofstream* supply_csv_file
        = RTUTIL.getOutputFileStream(RTUTIL.getString(sa_temp_directory_path, "supply_map_", routing_layer.get_layer_name(), ".csv"));
    for (int32_t y = routing_edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(supply_csv_file, routing_edge_map[x][y].get_supply(), ",");
      }
      RTUTIL.pushStream(supply_csv_file, "\n");
    }
    RTUTIL.closeFileStream(supply_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#endif

#if 1  // debug

void SupplyAnalyzer::debugPlotSAModel(SAModel& sa_model)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  std::string& sa_temp_directory_path = RTDM.getConfig().sa_temp_directory_path;

  GPGDS gp_gds;

  // base_region
  {
    GPStruct base_region_struct("base_region");
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(die.get_real_rect());
    base_region_struct.push(gp_boundary);
    gp_gds.addStruct(base_region_struct);
  }

  // gcell_axis
  {
    GPStruct gcell_axis_struct("gcell_axis");
    std::vector<int32_t> gcell_x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), gcell_axis.get_x_grid_list());
    std::vector<int32_t> gcell_y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), gcell_axis.get_y_grid_list());
    for (int32_t x : gcell_x_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
      gcell_axis_struct.push(gp_path);
    }
    for (int32_t y : gcell_y_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
      gcell_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(gcell_axis_struct);
  }

  // track_axis_struct
  {
    GPStruct track_axis_struct("track_axis_struct");
    for (RoutingLayer& routing_layer : routing_layer_list) {
      std::vector<int32_t> x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), routing_layer.getXTrackGridList());
      std::vector<int32_t> y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), routing_layer.getYTrackGridList());
      for (int32_t x : x_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
      for (int32_t y : y_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
    }
    gp_gds.addStruct(track_axis_struct);
  }

  // fixed_rect
  auto& type_layer_fixed_rect_rtree_map = RTDM.getDatabase().get_type_layer_fixed_rect_rtree_map();
  for (bool is_routing : {false, true}) {
    for (auto& [layer_idx, fixed_rect_rtree] : type_layer_fixed_rect_rtree_map[is_routing]) {
      std::map<int32_t, GPStruct> net_fixed_rect_struct_map;
      for (const auto& [rect, net_fixed_rect] : fixed_rect_rtree) {
        auto [net_idx, fixed_rect] = net_fixed_rect;
        auto struct_iter = net_fixed_rect_struct_map.try_emplace(net_idx, RTUTIL.getString("fixed_rect(net_", net_idx, ")")).first;
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(fixed_rect->get_real_rect());
        gp_boundary.set_layer_idx(is_routing ? RTGP.getGDSIdxByRouting(layer_idx) : RTGP.getGDSIdxByCut(layer_idx));
        struct_iter->second.push(gp_boundary);
      }
      for (auto& [net_idx, fixed_rect_struct] : net_fixed_rect_struct_map) {
        gp_gds.addStruct(fixed_rect_struct);
      }
    }
  }

  // routing result
  for (auto& [net_idx, segment_set] : RTDM.getNetDetailedResultMap(die)) {
    GPStruct detailed_result_struct(RTUTIL.getString("detailed_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>* segment : segment_set) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        detailed_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(detailed_result_struct);
  }

  // routing patch
  for (auto& [net_idx, patch_set] : RTDM.getNetDetailedPatchMap(die)) {
    GPStruct detailed_patch_struct(RTUTIL.getString("detailed_patch(net_", net_idx, ")"));
    for (EXTLayerRect* patch : patch_set) {
      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
      gp_boundary.set_rect(patch->get_real_rect());
      gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(patch->get_layer_idx()));
      detailed_patch_struct.push(gp_boundary);
    }
    gp_gds.addStruct(detailed_patch_struct);
  }

  // gcell_map
  {
    GPStruct gcell_map_struct("gcell_map");
    for (RoutingLayer& routing_layer : routing_layer_list) {
      for (int32_t grid_x = 0; grid_x < gcell_map.get_x_size(); grid_x++) {
        for (int32_t grid_y = 0; grid_y < gcell_map.get_y_size(); grid_y++) {
          PlanarRect real_rect = RTUTIL.getRealRectByGCell(grid_x, grid_y, gcell_axis);
          int32_t y_reduced_span = std::max(1, real_rect.getYSpan() / 12);
          int32_t y = real_rect.get_ur_y();

          y -= y_reduced_span;
          GPText gp_text_node_grid_coord;
          gp_text_node_grid_coord.set_coord(real_rect.get_ll_x(), y);
          gp_text_node_grid_coord.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_node_grid_coord.set_message(RTUTIL.getString("(", grid_x, " , ", y, " , ", routing_layer.get_layer_idx(), ")"));
          gp_text_node_grid_coord.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
          gp_text_node_grid_coord.set_presentation(GPTextPresentation::kLeftMiddle);
          gcell_map_struct.push(gp_text_node_grid_coord);

          int32_t layer_idx = routing_layer.get_layer_idx();
          std::map<Orientation, int32_t> orient_supply_map;
          std::map<int32_t, std::set<Orientation>> ignore_net_orient_map;
          if (routing_h_edge_map[layer_idx].isInside(grid_x - 1, grid_y)) {
            RoutingEdge& routing_edge = routing_h_edge_map[layer_idx][grid_x - 1][grid_y];
            if (routing_edge.get_supply() > 0) {
              orient_supply_map[Orientation::kWest] = routing_edge.get_supply();
            }
            for (int32_t net_idx : routing_edge.get_ignore_net_set()) {
              ignore_net_orient_map[net_idx].insert(Orientation::kWest);
            }
          }
          if (routing_h_edge_map[layer_idx].isInside(grid_x, grid_y)) {
            RoutingEdge& routing_edge = routing_h_edge_map[layer_idx][grid_x][grid_y];
            if (routing_edge.get_supply() > 0) {
              orient_supply_map[Orientation::kEast] = routing_edge.get_supply();
            }
            for (int32_t net_idx : routing_edge.get_ignore_net_set()) {
              ignore_net_orient_map[net_idx].insert(Orientation::kEast);
            }
          }
          if (routing_v_edge_map[layer_idx].isInside(grid_x, grid_y - 1)) {
            RoutingEdge& routing_edge = routing_v_edge_map[layer_idx][grid_x][grid_y - 1];
            if (routing_edge.get_supply() > 0) {
              orient_supply_map[Orientation::kSouth] = routing_edge.get_supply();
            }
            for (int32_t net_idx : routing_edge.get_ignore_net_set()) {
              ignore_net_orient_map[net_idx].insert(Orientation::kSouth);
            }
          }
          if (routing_v_edge_map[layer_idx].isInside(grid_x, grid_y)) {
            RoutingEdge& routing_edge = routing_v_edge_map[layer_idx][grid_x][grid_y];
            if (routing_edge.get_supply() > 0) {
              orient_supply_map[Orientation::kNorth] = routing_edge.get_supply();
            }
            for (int32_t net_idx : routing_edge.get_ignore_net_set()) {
              ignore_net_orient_map[net_idx].insert(Orientation::kNorth);
            }
          }

          if (!orient_supply_map.empty()) {
            y -= y_reduced_span;
            GPText gp_text_orient_supply_map;
            gp_text_orient_supply_map.set_coord(real_rect.get_ll_x(), y);
            gp_text_orient_supply_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_orient_supply_map.set_message("orient_supply_map: ");
            gp_text_orient_supply_map.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
            gp_text_orient_supply_map.set_presentation(GPTextPresentation::kLeftMiddle);
            gcell_map_struct.push(gp_text_orient_supply_map);

            y -= y_reduced_span;
            GPText gp_text_orient_supply_map_info;
            gp_text_orient_supply_map_info.set_coord(real_rect.get_ll_x(), y);
            gp_text_orient_supply_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            std::string orient_supply_map_info_message = "--";
            for (auto& [orient, supply] : orient_supply_map) {
              orient_supply_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient), ",", supply, ")");
            }
            gp_text_orient_supply_map_info.set_message(orient_supply_map_info_message);
            gp_text_orient_supply_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
            gp_text_orient_supply_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
            gcell_map_struct.push(gp_text_orient_supply_map_info);
          }

          if (!ignore_net_orient_map.empty()) {
            y -= y_reduced_span;
            GPText gp_text_ignore_net_orient_map;
            gp_text_ignore_net_orient_map.set_coord(real_rect.get_ll_x(), y);
            gp_text_ignore_net_orient_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text_ignore_net_orient_map.set_message("ignore_net_orient_map: ");
            gp_text_ignore_net_orient_map.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
            gp_text_ignore_net_orient_map.set_presentation(GPTextPresentation::kLeftMiddle);
            gcell_map_struct.push(gp_text_ignore_net_orient_map);

            y -= y_reduced_span;
            GPText gp_text_ignore_net_orient_map_info;
            gp_text_ignore_net_orient_map_info.set_coord(real_rect.get_ll_x(), y);
            gp_text_ignore_net_orient_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            std::string ignore_net_orient_map_info_message = "--";
            for (auto& [net_idx, orient_set] : ignore_net_orient_map) {
              ignore_net_orient_map_info_message += RTUTIL.getString("(", net_idx);
              for (Orientation orient : orient_set) {
                ignore_net_orient_map_info_message += RTUTIL.getString(",", GetOrientationName()(orient));
              }
              ignore_net_orient_map_info_message += RTUTIL.getString(")");
            }
            gp_text_ignore_net_orient_map_info.set_message(ignore_net_orient_map_info_message);
            gp_text_ignore_net_orient_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
            gp_text_ignore_net_orient_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
            gcell_map_struct.push(gp_text_ignore_net_orient_map_info);
          }
        }
      }
    }
    gp_gds.addStruct(gcell_map_struct);
  }

  std::string gds_file_path = RTUTIL.getString(sa_temp_directory_path, "supply.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

#endif

}  // namespace irt
