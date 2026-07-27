// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "EnvBuilder.hpp"

#include "EnvTrackOverlapMerger.hpp"
#include "Utility.hpp"

namespace ircx {

// public

void EnvBuilder::initInst()
{
  if (_eb_instance == nullptr) {
    _eb_instance = new EnvBuilder();
  }
}

EnvBuilder& EnvBuilder::getInst()
{
  if (_eb_instance == nullptr) {
    RCXLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_eb_instance;
}

void EnvBuilder::destroyInst()
{
  if (_eb_instance != nullptr) {
    delete _eb_instance;
    _eb_instance = nullptr;
  }
}

void EnvBuilder::build()
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");

  EBModel eb_model;
  buildEBModel(eb_model);

  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

EnvBuilder* EnvBuilder::_eb_instance = nullptr;

void EnvBuilder::buildEBModel(EBModel& eb_model)
{
  eb_model.set_bucket_size_um(5.0);
  eb_model.set_cross_layer_num(3);

  if (!buildNetEnvList(eb_model)) {
    RCXLOG.error(Loc::current(), "Build net env failed!");
  }
}

bool EnvBuilder::buildNetEnvList(EBModel& eb_model)
{
  Database& database = RCXDM.getDatabase();
  LayoutData& layout_data = database.get_layout_data();
  TopoPool& topo_pool = database.get_topo_pool();

  if (!buildTrackIdxMap(eb_model) || !buildPixelGridMap(eb_model)) {
    return false;
  }
  buildSearchTrackNumMap(eb_model);

  int32_t net_num = layout_data.get_regular_net_num();
  std::vector<NetEnv>& net_env_list = RCXDM.getDatabase().get_net_env_list();
  net_env_list.clear();
  net_env_list.resize(net_num);

  std::map<int32_t, RoutingLayer>& routing_layers = layout_data.get_routing_layer_map();
  std::map<int32_t, EnvTrackIdx>& layer_to_prefer_track_idx_map = eb_model.get_layer_to_prefer_track_idx_map();
  std::map<int32_t, EnvTrackIdx>& layer_to_nonprefer_track_idx_map = eb_model.get_layer_to_nonprefer_track_idx_map();
  std::map<int32_t, int32_t>& layer_to_search_track_num_map = eb_model.get_layer_to_search_track_num_map();

  int32_t thread_num = RCXUTIL.getThreadNum(net_num, omp_get_max_threads());
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    EnvTrackOverlapMerger track_merger;
    EnvPixelOverlapMerger pixel_merger;
    NetEnv& net_env = net_env_list[net_idx];

    for (TopoEdge& edge : topo_pool.get_net_edge_list(net_idx)) {
      if (edge.get_is_via()) {
        net_env.append_edge_interval_list({});  // placeholder to keep idx aligned with TopoPool
        continue;
      }

      int32_t layer_idx = edge.get_layer_idx();
      LineSegment& query_line_segment = edge.get_line_segment();

      std::vector<EnvTrackOverlap> upper_track_overlap_list;
      std::vector<EnvTrackOverlap> lower_track_overlap_list;
      bool is_prefer_horizontal = routing_layers[layer_idx].get_is_prefer_horizontal();
      std::map<int32_t, EnvTrackIdx>& track_idx_map = edge.get_line_segment().get_is_horizontal() == is_prefer_horizontal
                                                          ? layer_to_prefer_track_idx_map
                                                          : layer_to_nonprefer_track_idx_map;
      std::map<int32_t, EnvTrackIdx>::iterator track_iter = track_idx_map.find(layer_idx);
      if (track_iter != track_idx_map.end()) {
        upper_track_overlap_list = track_iter->second.getOverlapList(query_line_segment, layer_to_search_track_num_map[layer_idx]);
        lower_track_overlap_list = track_iter->second.getOverlapList(query_line_segment, -layer_to_search_track_num_map[layer_idx]);
      }

      std::vector<EdgeEnvInterval> edge_env_interval_list;
      track_merger.merge(query_line_segment.get_lower(), query_line_segment.get_upper(), lower_track_overlap_list,
                         upper_track_overlap_list, edge_env_interval_list);

      std::vector<EnvLayerPixelOverlapList> lower_layer_pixel_overlap_list
          = getCrossLayerPixelOverlapList(eb_model, query_line_segment, layer_idx, false);
      std::vector<EnvLayerPixelOverlapList> upper_layer_pixel_overlap_list
          = getCrossLayerPixelOverlapList(eb_model, query_line_segment, layer_idx, true);

      std::vector<CrossLayerOverlap> cross_layer_overlap_list;
      pixel_merger.merge(query_line_segment.get_lower(), query_line_segment.get_upper(), lower_layer_pixel_overlap_list,
                         upper_layer_pixel_overlap_list, cross_layer_overlap_list);

      for (EdgeEnvInterval& edge_env_interval : edge_env_interval_list) {
        edge_env_interval.set_cross_layer_overlap_list(
            getClippedCrossLayerOverlapList(cross_layer_overlap_list, edge_env_interval.get_start_coord(), edge_env_interval.get_end_coord()));
      }

      net_env.append_edge_interval_list(std::move(edge_env_interval_list));
    }
  }

  return true;
}

std::vector<CrossLayerOverlap> EnvBuilder::getClippedCrossLayerOverlapList(const std::vector<CrossLayerOverlap>& cross_layer_overlap_list,
                                                                            int32_t start_coord, int32_t end_coord)
{
  std::vector<CrossLayerOverlap> clipped_cross_layer_overlap_list;
  for (const CrossLayerOverlap& cross_layer_overlap : cross_layer_overlap_list) {
    int32_t clipped_start_coord = std::max(start_coord, cross_layer_overlap.get_start_coord());
    int32_t clipped_end_coord = std::min(end_coord, cross_layer_overlap.get_end_coord());
    if (!(clipped_start_coord < clipped_end_coord)) {
      continue;
    }
    if (!clipped_cross_layer_overlap_list.empty()
        && clipped_cross_layer_overlap_list.back().get_end_coord() == clipped_start_coord
        && clipped_cross_layer_overlap_list.back().get_below_layer_idx() == cross_layer_overlap.get_below_layer_idx()
        && clipped_cross_layer_overlap_list.back().get_above_layer_idx() == cross_layer_overlap.get_above_layer_idx()) {
      clipped_cross_layer_overlap_list.back().set_end_coord(clipped_end_coord);
      continue;
    }
    CrossLayerOverlap clipped_cross_layer_overlap;
    clipped_cross_layer_overlap.set_start_coord(clipped_start_coord);
    clipped_cross_layer_overlap.set_end_coord(clipped_end_coord);
    clipped_cross_layer_overlap.set_below_layer_idx(cross_layer_overlap.get_below_layer_idx());
    clipped_cross_layer_overlap.set_above_layer_idx(cross_layer_overlap.get_above_layer_idx());
    clipped_cross_layer_overlap_list.push_back(std::move(clipped_cross_layer_overlap));
  }
  return clipped_cross_layer_overlap_list;
}

std::vector<EnvLayerPixelOverlapList> EnvBuilder::getCrossLayerPixelOverlapList(EBModel& eb_model, const LineSegment& line_segment,
                                                                                  int32_t base_layer_idx, bool is_upper_layer)
{
  LayoutData& layout_data = RCXDM.getDatabase().get_layout_data();
  std::map<int32_t, RoutingLayer>& routing_layers = layout_data.get_routing_layer_map();
  int32_t min_layer_idx = routing_layers.empty() ? 0 : routing_layers.begin()->first;
  int32_t max_layer_idx = routing_layers.empty() ? 0 : routing_layers.rbegin()->first;
  std::map<int32_t, EnvPixelGrid>& layer_to_prefer_pixel_grid_map = eb_model.get_layer_to_prefer_pixel_grid_map();
  std::map<int32_t, EnvPixelGrid>& layer_to_nonprefer_pixel_grid_map = eb_model.get_layer_to_nonprefer_pixel_grid_map();
  std::vector<EnvLayerPixelOverlapList> layer_pixel_overlap_list;

  for (int32_t delta = 1; delta <= eb_model.get_cross_layer_num(); ++delta) {
    int32_t candidate_layer_idx = 0;
    if (is_upper_layer) {
      if (base_layer_idx > max_layer_idx || max_layer_idx - base_layer_idx < delta) {
        break;
      }
      candidate_layer_idx = base_layer_idx + delta;
    } else {
      if (base_layer_idx < min_layer_idx || base_layer_idx - min_layer_idx < delta) {
        break;
      }
      candidate_layer_idx = base_layer_idx - delta;
    }

    std::map<int32_t, RoutingLayer>::iterator layer_iter = routing_layers.find(candidate_layer_idx);
    if (layer_iter == routing_layers.end()) {
      continue;
    }

    std::map<int32_t, EnvPixelGrid>& pixel_grid_map = (layer_iter->second.get_is_prefer_horizontal() != line_segment.get_is_horizontal())
                                                           ? layer_to_prefer_pixel_grid_map
                                                           : layer_to_nonprefer_pixel_grid_map;
    std::map<int32_t, EnvPixelGrid>::iterator pixel_iter = pixel_grid_map.find(candidate_layer_idx);
    if (pixel_iter == pixel_grid_map.end()) {
      continue;
    }

    std::vector<EnvPixelOverlap> pixel_overlap_list = pixel_iter->second.getOverlapList(line_segment);
    if (pixel_overlap_list.empty()) {
      continue;
    }

    EnvLayerPixelOverlapList layer_pixel_overlap;
    layer_pixel_overlap.set_layer_idx(candidate_layer_idx);
    layer_pixel_overlap.set_pixel_overlap_list(std::move(pixel_overlap_list));
    layer_pixel_overlap_list.push_back(std::move(layer_pixel_overlap));
  }
  return layer_pixel_overlap_list;
}

bool EnvBuilder::buildTrackIdxMap(EBModel& eb_model)
{
  Database& database = RCXDM.getDatabase();
  LayoutData& layout_data = database.get_layout_data();
  TopoPool& topo_pool = database.get_topo_pool();

  std::map<int32_t, RoutingLayer>& routing_layers = layout_data.get_routing_layer_map();
  GTLRectInt& die_shape = layout_data.get_die_shape();
  int32_t bucket_step = static_cast<int32_t>(eb_model.get_bucket_size_um() * layout_data.get_dbu_per_micron());
  std::map<int32_t, EnvTrackIdx>& layer_to_prefer_track_idx_map = eb_model.get_layer_to_prefer_track_idx_map();
  std::map<int32_t, EnvTrackIdx>& layer_to_nonprefer_track_idx_map = eb_model.get_layer_to_nonprefer_track_idx_map();

  layer_to_prefer_track_idx_map.clear();
  layer_to_nonprefer_track_idx_map.clear();

  for (auto& [layer_idx, layer] : routing_layers) {
    TrackInfo& track_info = layer.get_track_info();

    EnvTrackIdx prefer_track_idx;
    if (!initTrackIdx(prefer_track_idx, track_info, die_shape, bucket_step, layer.get_is_prefer_horizontal())) {
      return false;
    }
    layer_to_prefer_track_idx_map[layer_idx] = std::move(prefer_track_idx);

    EnvTrackIdx nonprefer_track_idx;
    if (!initTrackIdx(nonprefer_track_idx, track_info, die_shape, bucket_step, !layer.get_is_prefer_horizontal())) {
      return false;
    }
    layer_to_nonprefer_track_idx_map[layer_idx] = std::move(nonprefer_track_idx);
  }

  for (TopoEdge& edge : topo_pool.get_edge_pool()) {
    addTopoEdgeToTrackIdx(eb_model, edge);
  }
  for (TopoEdge& edge : topo_pool.get_special_edge_pool()) {
    addTopoEdgeToTrackIdx(eb_model, edge);
  }

  return true;
}

void EnvBuilder::addTopoEdgeToTrackIdx(EBModel& eb_model, TopoEdge& edge)
{
  if (edge.get_is_via()) {
    return;
  }

  LayoutData& layout_data = RCXDM.getDatabase().get_layout_data();
  std::map<int32_t, RoutingLayer>& routing_layers = layout_data.get_routing_layer_map();
  std::map<int32_t, EnvTrackIdx>& layer_to_prefer_track_idx_map = eb_model.get_layer_to_prefer_track_idx_map();
  std::map<int32_t, EnvTrackIdx>& layer_to_nonprefer_track_idx_map = eb_model.get_layer_to_nonprefer_track_idx_map();
  int32_t layer_idx = edge.get_layer_idx();
  bool is_prefer_horizontal = routing_layers[layer_idx].get_is_prefer_horizontal();
  std::map<int32_t, EnvTrackIdx>& track_idx_map = edge.get_line_segment().get_is_horizontal() == is_prefer_horizontal
                                                      ? layer_to_prefer_track_idx_map
                                                      : layer_to_nonprefer_track_idx_map;
  track_idx_map[layer_idx].addTopoEdge(edge);
}

bool EnvBuilder::initTrackIdx(EnvTrackIdx& track_idx, TrackInfo& track_info, GTLRectInt& die_shape, int32_t bucket_step,
                                       bool is_horizontal)
{
  int32_t die_x_min = RCXUTIL.minX(die_shape);
  int32_t die_y_min = RCXUTIL.minY(die_shape);
  int32_t die_x_max = RCXUTIL.maxX(die_shape);
  int32_t die_y_max = RCXUTIL.maxY(die_shape);
  int32_t die_x_span = RCXUTIL.deltaX(die_shape);
  int32_t die_y_span = RCXUTIL.deltaY(die_shape);

  int32_t track_origin = is_horizontal ? track_info.get_y_origin() : track_info.get_x_origin();
  int32_t track_count = is_horizontal ? track_info.get_y_count() : track_info.get_x_count();
  int32_t track_step = is_horizontal ? track_info.get_y_step() : track_info.get_x_step();
  int32_t axis_lower_coord = is_horizontal ? die_y_min : die_x_min;
  int32_t axis_upper_coord = is_horizontal ? die_y_max : die_x_max;
  EnvAxis track_axis = getCoveredAxis(track_origin, track_count, track_step, axis_lower_coord, axis_upper_coord);

  track_idx.set_track_origin(track_axis.get_origin());
  track_idx.set_track_count(track_axis.get_count());
  track_idx.set_track_step(track_axis.get_step());
  track_idx.set_bucket_origin(is_horizontal ? die_x_min : die_y_min);
  track_idx.set_bucket_count(RCXUTIL.ceilDivPositive(is_horizontal ? die_x_span : die_y_span, bucket_step));
  track_idx.set_bucket_step(bucket_step);
  return track_idx.initTrackBucketList();
}

EnvAxis EnvBuilder::getCoveredAxis(int32_t origin, int32_t count, int32_t step, int32_t lower_coord, int32_t upper_coord)
{
  if (step <= 0) {
    return EnvAxis(origin, count, step);
  }

  int32_t axis_origin = origin;
  int32_t axis_count = count;
  int32_t axis_step = step;

  if (axis_origin > lower_coord) {
    int32_t shift = (axis_origin - lower_coord + axis_step - 1) / axis_step;
    axis_origin -= shift * axis_step;
    axis_count += shift;
  }

  int32_t covered_upper_coord = axis_origin + axis_step * axis_count;
  if (covered_upper_coord <= upper_coord) {
    axis_count += (upper_coord - covered_upper_coord) / axis_step + 1;
  }

  return EnvAxis(axis_origin, axis_count, step);
}

bool EnvBuilder::buildPixelGridMap(EBModel& eb_model)
{
  Database& database = RCXDM.getDatabase();
  LayoutData& layout_data = database.get_layout_data();
  TopoPool& topo_pool = database.get_topo_pool();

  std::map<int32_t, RoutingLayer>& routing_layers = layout_data.get_routing_layer_map();
  GTLRectInt& die_shape = layout_data.get_die_shape();
  int32_t die_x_min = RCXUTIL.minX(die_shape);
  int32_t die_y_min = RCXUTIL.minY(die_shape);
  int32_t die_x_max = RCXUTIL.maxX(die_shape);
  int32_t die_y_max = RCXUTIL.maxY(die_shape);
  std::map<int32_t, EnvPixelGrid>& layer_to_prefer_pixel_grid_map = eb_model.get_layer_to_prefer_pixel_grid_map();
  std::map<int32_t, EnvPixelGrid>& layer_to_nonprefer_pixel_grid_map = eb_model.get_layer_to_nonprefer_pixel_grid_map();

  layer_to_prefer_pixel_grid_map.clear();
  layer_to_nonprefer_pixel_grid_map.clear();

  for (auto& [layer_idx, layer] : routing_layers) {
    TrackInfo& track_info = layer.get_track_info();
    EnvPixelGrid pixel_grid;

    EnvAxis x_axis = getCoveredAxis(track_info.get_x_origin(), track_info.get_x_count(), track_info.get_x_step(), die_x_min, die_x_max);
    EnvAxis y_axis = getCoveredAxis(track_info.get_y_origin(), track_info.get_y_count(), track_info.get_y_step(), die_y_min, die_y_max);

    pixel_grid.set_x_origin(x_axis.get_origin());
    pixel_grid.set_x_count(x_axis.get_count());
    pixel_grid.set_x_step(x_axis.get_step());
    pixel_grid.set_y_origin(y_axis.get_origin());
    pixel_grid.set_y_count(y_axis.get_count());
    pixel_grid.set_y_step(y_axis.get_step());

    if (!pixel_grid.initPixelMap()) {
      return false;
    }
    layer_to_prefer_pixel_grid_map[layer_idx] = pixel_grid;
    layer_to_nonprefer_pixel_grid_map[layer_idx] = std::move(pixel_grid);
  }

  for (TopoEdge& edge : topo_pool.get_edge_pool()) {
    addTopoEdgeToPixelGrid(eb_model, edge);
  }
  for (TopoEdge& edge : topo_pool.get_special_edge_pool()) {
    addTopoEdgeToPixelGrid(eb_model, edge);
  }

  return true;
}

void EnvBuilder::addTopoEdgeToPixelGrid(EBModel& eb_model, TopoEdge& edge)
{
  if (edge.get_is_via()) {
    return;
  }

  LayoutData& layout_data = RCXDM.getDatabase().get_layout_data();
  std::map<int32_t, RoutingLayer>& routing_layers = layout_data.get_routing_layer_map();
  std::map<int32_t, EnvPixelGrid>& layer_to_prefer_pixel_grid_map = eb_model.get_layer_to_prefer_pixel_grid_map();
  std::map<int32_t, EnvPixelGrid>& layer_to_nonprefer_pixel_grid_map = eb_model.get_layer_to_nonprefer_pixel_grid_map();
  int32_t layer_idx = edge.get_layer_idx();
  bool is_prefer_horizontal = routing_layers[layer_idx].get_is_prefer_horizontal();

  if (edge.get_line_segment().get_is_horizontal() == is_prefer_horizontal) {
    layer_to_prefer_pixel_grid_map[layer_idx].addTopoEdge(edge);
  } else {
    layer_to_nonprefer_pixel_grid_map[layer_idx].addTopoEdge(edge);
  }
}

void EnvBuilder::buildSearchTrackNumMap(EBModel& eb_model)
{
  LayoutData& layout_data = RCXDM.getDatabase().get_layout_data();
  std::map<int32_t, RoutingLayer>& routing_layers = layout_data.get_routing_layer_map();
  std::map<int32_t, int32_t>& layer_to_search_track_num_map = eb_model.get_layer_to_search_track_num_map();

  layer_to_search_track_num_map.clear();

  for (auto& [layer_idx, layer] : routing_layers) {
    layer_to_search_track_num_map[layer_idx] = 10;
  }
}

}  // namespace ircx
