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
#include "PDNGenerator.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

// public

void PDNGenerator::initInst()
{
  if (_pg_instance == nullptr) {
    _pg_instance = new PDNGenerator();
  }
}

PDNGenerator& PDNGenerator::getInst()
{
  if (_pg_instance == nullptr) {
    FPLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pg_instance;
}

void PDNGenerator::destroyInst()
{
  if (_pg_instance != nullptr) {
    delete _pg_instance;
    _pg_instance = nullptr;
  }
}

// function

void PDNGenerator::generate()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  PGModel pg_model;
  generatePDN(pg_model);

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::generatePDN(PGModel& pg_model)
{
  buildPGNet(pg_model);
  buildRail(pg_model);
  buildStripe(pg_model);
  alignStripeSegmentList();
  buildLayerConnect(pg_model);
  buildMacroConnect(pg_model);
}

void PDNGenerator::buildPGNet(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  for (PGGlobalConnect& pg_connect : FPDM.getConfig().pg_connect_list) {
    std::map<std::string, int32_t>::iterator pg_net_iter = database.get_pg_net_name_to_idx_map().find(pg_connect.get_net_name());
    if (pg_net_iter == database.get_pg_net_name_to_idx_map().end()) {
      PGNet pg_net;
      pg_net.set_name(pg_connect.get_net_name());
      pg_net.set_type(pg_connect.get_net_type());
      int32_t pg_net_idx = static_cast<int32_t>(database.get_pg_net_list().size());
      database.get_pg_net_list().push_back(pg_net);
      database.get_pg_net_name_to_idx_map()[pg_connect.get_net_name()] = pg_net_idx;
      if (pg_connect.get_net_type() == PGNetType::kPower && pg_model.get_default_power_net_name().empty()) {
        pg_model.set_default_power_net_name(pg_connect.get_net_name());
      } else if (pg_connect.get_net_type() == PGNetType::kGround && pg_model.get_default_ground_net_name().empty()) {
        pg_model.set_default_ground_net_name(pg_connect.get_net_name());
      }
    }

    PGNet& pg_net = getPGNet(pg_connect.get_net_name());
    if (database.get_io_pin_name_to_idx_map().find(pg_connect.get_pin_name()) != database.get_io_pin_name_to_idx_map().end()) {
      pg_net.add_io_pin(pg_connect.get_pin_name(), IOPinDirection::kInOut);
    } else {
      pg_net.add_instance_pin(pg_connect.get_pin_name());
    }
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

PGNet& PDNGenerator::getPGNet(std::string net_name)
{
  Database& database = FPDM.getDatabase();
  return database.get_pg_net_list()[database.get_pg_net_name_to_idx_map()[net_name]];
}

void PDNGenerator::buildRail(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  for (PGRail& pg_rail : FPDM.getConfig().pg_rail_list) {
    RoutingLayer* routing_layer = findRoutingLayer(pg_rail.get_layer_name());
    if (routing_layer == nullptr || routing_layer->get_prefer_direction() != Direction::kHorizontal) {
      continue;
    }

    int32_t width = FPUTIL.transMicronToDBU(pg_rail.get_width_micron(), database.get_micron_dbu());
    if (width <= 0) {
      continue;
    }

    for (Row& row : database.get_row_list()) {
      std::string bottom_net_name = row.get_orient() == PlacementOrientation::kN || row.get_orient() == PlacementOrientation::kFN
                                        ? pg_model.get_default_ground_net_name()
                                        : pg_model.get_default_power_net_name();
      addLineSegment(bottom_net_name, routing_layer->get_name(), PGSegmentType::kFollowPin, width, row.get_ll_x(), row.get_y(),
                     row.get_ur_x(), row.get_y());
    }

    for (Row& row : database.get_row_list()) {
      std::string top_net_name = row.get_orient() == PlacementOrientation::kN || row.get_orient() == PlacementOrientation::kFN
                                     ? pg_model.get_default_power_net_name()
                                     : pg_model.get_default_ground_net_name();
      std::vector<std::pair<int32_t, int32_t>> covered_interval_list;
      for (PGSegment& pg_segment : database.get_pg_segment_list()) {
        if (pg_segment.get_type() != PGSegmentType::kFollowPin || !pg_segment.is_horizontal()
            || pg_segment.get_layer_name() != routing_layer->get_name() || pg_segment.get_net_name() != top_net_name
            || pg_segment.get_start_y() != row.get_ur_y()) {
          continue;
        }
        int32_t start_x = std::max(row.get_ll_x(), std::min(pg_segment.get_start_x(), pg_segment.get_end_x()));
        int32_t end_x = std::min(row.get_ur_x(), std::max(pg_segment.get_start_x(), pg_segment.get_end_x()));
        if (start_x < end_x) {
          covered_interval_list.emplace_back(start_x, end_x);
        }
      }
      std::sort(covered_interval_list.begin(), covered_interval_list.end());

      int32_t current_x = row.get_ll_x();
      for (std::pair<int32_t, int32_t>& covered_interval : covered_interval_list) {
        if (covered_interval.second <= current_x) {
          continue;
        }
        if (current_x < covered_interval.first) {
          addLineSegment(top_net_name, routing_layer->get_name(), PGSegmentType::kFollowPin, width, current_x, row.get_ur_y(),
                         covered_interval.first, row.get_ur_y());
        }
        current_x = std::max(current_x, covered_interval.second);
        if (row.get_ur_x() <= current_x) {
          break;
        }
      }
      if (current_x < row.get_ur_x()) {
        addLineSegment(top_net_name, routing_layer->get_name(), PGSegmentType::kFollowPin, width, current_x, row.get_ur_y(), row.get_ur_x(),
                       row.get_ur_y());
      }
    }
  }
  mergeRailSegmentList();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::mergeRailSegmentList()
{
  Database& database = FPDM.getDatabase();
  std::vector<PGSegment> merged_pg_segment_list;
  for (PGSegment& pg_segment : database.get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kFollowPin || !pg_segment.is_horizontal()) {
      merged_pg_segment_list.push_back(pg_segment);
      continue;
    }

    PGSegment merged_rail_segment = pg_segment;
    for (int32_t segment_idx = 0; segment_idx < static_cast<int32_t>(merged_pg_segment_list.size());) {
      PGSegment& rail_segment = merged_pg_segment_list[segment_idx];
      if (rail_segment.get_type() != PGSegmentType::kFollowPin || !rail_segment.is_horizontal()
          || rail_segment.get_net_name() != merged_rail_segment.get_net_name()
          || rail_segment.get_layer_name() != merged_rail_segment.get_layer_name()
          || rail_segment.get_width() != merged_rail_segment.get_width()
          || rail_segment.get_start_y() != merged_rail_segment.get_start_y()) {
        segment_idx++;
        continue;
      }

      int32_t rail_start_x = std::min(rail_segment.get_start_x(), rail_segment.get_end_x());
      int32_t rail_end_x = std::max(rail_segment.get_start_x(), rail_segment.get_end_x());
      int32_t merged_start_x = std::min(merged_rail_segment.get_start_x(), merged_rail_segment.get_end_x());
      int32_t merged_end_x = std::max(merged_rail_segment.get_start_x(), merged_rail_segment.get_end_x());
      if (rail_segment.get_ur_x() <= merged_rail_segment.get_ll_x()
          || merged_rail_segment.get_ur_x() <= rail_segment.get_ll_x()) {
        segment_idx++;
        continue;
      }

      merged_rail_segment.set_start_coord(std::min(rail_start_x, merged_start_x), merged_rail_segment.get_start_y());
      merged_rail_segment.set_end_coord(std::max(rail_end_x, merged_end_x), merged_rail_segment.get_start_y());
      merged_pg_segment_list.erase(merged_pg_segment_list.begin() + segment_idx);
    }
    merged_pg_segment_list.push_back(merged_rail_segment);
  }
  database.set_pg_segment_list(merged_pg_segment_list);
}

RoutingLayer* PDNGenerator::findRoutingLayer(std::string layer_name)
{
  Database& database = FPDM.getDatabase();
  std::map<std::string, int32_t>::iterator routing_layer_iter = database.get_routing_layer_name_to_idx_map().find(layer_name);
  if (routing_layer_iter == database.get_routing_layer_name_to_idx_map().end()) {
    return nullptr;
  }
  return &database.get_routing_layer_list()[routing_layer_iter->second];
}

void PDNGenerator::addLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width, int32_t start_x,
                                  int32_t start_y, int32_t end_x, int32_t end_y)
{
  if (width <= 0 || (start_x == end_x && start_y == end_y)) {
    return;
  }

  std::vector<std::pair<int32_t, int32_t>> blockage_interval_list
      = getMacroBlockageIntervalList(layer_name, width, start_x, start_y, end_x, end_y);
  if (blockage_interval_list.empty()) {
    addUnblockedLineSegment(net_name, layer_name, segment_type, width, start_x, start_y, end_x, end_y);
    return;
  }

  bool horizontal = start_y == end_y;
  int32_t line_begin = horizontal ? std::min(start_x, end_x) : std::min(start_y, end_y);
  int32_t line_end = horizontal ? std::max(start_x, end_x) : std::max(start_y, end_y);
  int32_t current_coord = line_begin;
  for (std::pair<int32_t, int32_t>& blockage_interval : blockage_interval_list) {
    if (blockage_interval.second <= current_coord) {
      continue;
    }
    if (blockage_interval.first > current_coord) {
      if (horizontal) {
        addUnblockedLineSegment(net_name, layer_name, segment_type, width, current_coord, start_y, blockage_interval.first, start_y);
      } else {
        addUnblockedLineSegment(net_name, layer_name, segment_type, width, start_x, current_coord, start_x, blockage_interval.first);
      }
    }
    current_coord = std::max(current_coord, blockage_interval.second);
    if (current_coord >= line_end) {
      return;
    }
  }
  if (current_coord < line_end) {
    if (horizontal) {
      addUnblockedLineSegment(net_name, layer_name, segment_type, width, current_coord, start_y, line_end, start_y);
    } else {
      addUnblockedLineSegment(net_name, layer_name, segment_type, width, start_x, current_coord, start_x, line_end);
    }
  }
}

void PDNGenerator::addUnblockedLineSegment(std::string net_name, std::string layer_name, PGSegmentType segment_type, int32_t width,
                                           int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y)
{
  if (start_x == end_x && start_y == end_y) {
    return;
  }
  PGSegment pg_segment;
  pg_segment.set_net_name(net_name);
  pg_segment.set_layer_name(layer_name);
  pg_segment.set_type(segment_type);
  pg_segment.set_width(width);
  pg_segment.set_start_coord(start_x, start_y);
  pg_segment.set_end_coord(end_x, end_y);
  pg_segment.set_generated(true);
  FPDM.getDatabase().get_pg_segment_list().push_back(pg_segment);
}

std::vector<std::pair<int32_t, int32_t>> PDNGenerator::getMacroBlockageIntervalList(std::string layer_name, int32_t width,
                                                                                      int32_t start_x, int32_t start_y, int32_t end_x,
                                                                                      int32_t end_y)
{
  RoutingLayer* routing_layer = findRoutingLayer(layer_name);
  std::vector<std::pair<int32_t, int32_t>> blockage_interval_list;
  bool horizontal = start_y == end_y;
  int32_t line_begin = horizontal ? std::min(start_x, end_x) : std::min(start_y, end_y);
  int32_t line_end = horizontal ? std::max(start_x, end_x) : std::max(start_y, end_y);
  int32_t line_coord = horizontal ? start_y : start_x;
  int32_t half_width = width / 2;
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed() || routing_layer->get_order() > getMacroTopLayerOrder(instance)) {
      continue;
    }
    PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
    if (horizontal) {
      if (line_coord + half_width <= routing_halo_rect.get_ll_y() || routing_halo_rect.get_ur_y() <= line_coord - half_width) {
        continue;
      }
      int32_t start_coord = std::max(line_begin, routing_halo_rect.get_ll_x() - half_width);
      int32_t end_coord = std::min(line_end, routing_halo_rect.get_ur_x() + half_width);
      if (start_coord < end_coord) {
        blockage_interval_list.emplace_back(start_coord, end_coord);
      }
    } else {
      if (line_coord + half_width <= routing_halo_rect.get_ll_x() || routing_halo_rect.get_ur_x() <= line_coord - half_width) {
        continue;
      }
      int32_t start_coord = std::max(line_begin, routing_halo_rect.get_ll_y() - half_width);
      int32_t end_coord = std::min(line_end, routing_halo_rect.get_ur_y() + half_width);
      if (start_coord < end_coord) {
        blockage_interval_list.emplace_back(start_coord, end_coord);
      }
    }
  }
  std::sort(blockage_interval_list.begin(), blockage_interval_list.end(),
            [](const std::pair<int32_t, int32_t>& first, const std::pair<int32_t, int32_t>& second) { return first.first < second.first; });
  return blockage_interval_list;
}

int32_t PDNGenerator::getMacroTopLayerOrder(Instance& instance)
{
  int32_t top_layer_order = -1;
  for (InstancePinShape& pin_shape : instance.get_pin_shape_list()) {
    RoutingLayer* routing_layer = findRoutingLayer(pin_shape.get_layer_name());
    if (routing_layer != nullptr) {
      top_layer_order = std::max(top_layer_order, routing_layer->get_order());
    }
  }
  return top_layer_order;
}

void PDNGenerator::buildStripe(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Core& core = FPDM.getDatabase().get_core();
  for (PGStripe& pg_stripe : FPDM.getConfig().pg_stripe_list) {
    RoutingLayer* routing_layer = findRoutingLayer(pg_stripe.get_layer_name());
    if (routing_layer == nullptr) {
      continue;
    }

    int32_t width = FPUTIL.transMicronToDBU(pg_stripe.get_width_micron(), FPDM.getDatabase().get_micron_dbu());
    int32_t pitch = FPUTIL.transMicronToDBU(pg_stripe.get_pitch_micron(), FPDM.getDatabase().get_micron_dbu());
    int32_t offset = FPUTIL.transMicronToDBU(pg_stripe.get_offset_micron(), FPDM.getDatabase().get_micron_dbu());
    if (width <= 0 || pitch <= 0) {
      continue;
    }

    int32_t line_begin = routing_layer->get_prefer_direction() == Direction::kHorizontal ? core.get_ll_y() : core.get_ll_x();
    int32_t line_end = routing_layer->get_prefer_direction() == Direction::kHorizontal ? core.get_ur_y() : core.get_ur_x();
    int32_t start = line_begin + offset + width / 2;
    int32_t half_width = width / 2;
    int32_t half_pitch = pitch / 2;
    int32_t track_pitch = routing_layer->get_prefer_track_pitch();
    int32_t track_offset = routing_layer->get_prefer_track_offset();
    for (int32_t coord = start; coord <= line_end; coord += pitch) {
      int32_t power_coord = coord;
      if (width <= track_pitch && track_pitch > 0) {
        power_coord = (power_coord - track_offset) / track_pitch * track_pitch + track_offset;
      }
      if (power_coord - half_width < line_begin) {
        continue;
      }
      if (power_coord + half_width > line_end) {
        break;
      }
      if (routing_layer->get_prefer_direction() == Direction::kHorizontal) {
        addLineSegment(pg_model.get_default_power_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, core.get_ll_x(),
                       power_coord, core.get_ur_x(), power_coord);
      } else {
        addLineSegment(pg_model.get_default_power_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, power_coord,
                       core.get_ll_y(), power_coord, core.get_ur_y());
      }

      int32_t ground_coord = power_coord + half_pitch;
      if (ground_coord + half_width > line_end) {
        continue;
      }
      if (routing_layer->get_prefer_direction() == Direction::kHorizontal) {
        addLineSegment(pg_model.get_default_ground_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, core.get_ll_x(),
                       ground_coord, core.get_ur_x(), ground_coord);
      } else {
        addLineSegment(pg_model.get_default_ground_net_name(), routing_layer->get_name(), PGSegmentType::kStripe, width, ground_coord,
                       core.get_ll_y(), ground_coord, core.get_ur_y());
      }
    }
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::alignStripeSegmentList()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  for (PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (pg_segment.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    alignStripeSegment(pg_segment);
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::alignStripeSegment(PGSegment& stripe_segment)
{
  RoutingLayer* routing_layer = findRoutingLayer(stripe_segment.get_layer_name());
  if (routing_layer == nullptr) {
    return;
  }

  bool vertical = stripe_segment.is_vertical();
  int32_t half_width = stripe_segment.get_width() / 2;
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed() || routing_layer->get_order() > getMacroTopLayerOrder(instance)) {
      continue;
    }

    PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
    if (vertical) {
      if (stripe_segment.get_ur_x() <= routing_halo_rect.get_ll_x() || routing_halo_rect.get_ur_x() <= stripe_segment.get_ll_x()) {
        continue;
      }
      if (stripe_segment.get_start_y() == routing_halo_rect.get_ur_y() + half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, true);
        if (rail_coord != INT32_MAX && rail_coord <= stripe_segment.get_end_y()) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, true);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_start_y(rail_coord);
        }
      }
      if (stripe_segment.get_end_y() == routing_halo_rect.get_ll_y() - half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, false);
        if (rail_coord != INT32_MAX && stripe_segment.get_start_y() <= rail_coord) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, false);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_end_y(rail_coord);
        }
      }
    } else {
      if (stripe_segment.get_ur_y() <= routing_halo_rect.get_ll_y() || routing_halo_rect.get_ur_y() <= stripe_segment.get_ll_y()) {
        continue;
      }
      if (stripe_segment.get_start_x() == routing_halo_rect.get_ur_x() + half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, true);
        if (rail_coord != INT32_MAX && rail_coord <= stripe_segment.get_end_x()) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, true);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_start_x(rail_coord);
        }
      }
      if (stripe_segment.get_end_x() == routing_halo_rect.get_ll_x() - half_width) {
        int32_t rail_coord = getClosestRailEdgeCoord(stripe_segment, instance, false);
        if (rail_coord != INT32_MAX && stripe_segment.get_start_x() <= rail_coord) {
          int32_t cross_stripe_coord = getClosestCrossStripeEdgeCoord(stripe_segment, instance, rail_coord, false);
          if (cross_stripe_coord != INT32_MAX) {
            rail_coord = cross_stripe_coord;
          }
          stripe_segment.set_end_x(rail_coord);
        }
      }
    }
  }
}

int32_t PDNGenerator::getClosestRailEdgeCoord(PGSegment& stripe_segment, Instance& instance, bool high_side)
{
  PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
  bool vertical = stripe_segment.is_vertical();
  int32_t closest_rail_edge_coord = INT32_MAX;
  int32_t closest_distance = INT32_MAX;
  for (PGSegment& rail_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (rail_segment.get_type() != PGSegmentType::kFollowPin || rail_segment.get_net_name() != stripe_segment.get_net_name()) {
      continue;
    }
    if (vertical) {
      if (!rail_segment.is_horizontal() || stripe_segment.get_ur_x() <= rail_segment.get_ll_x()
          || rail_segment.get_ur_x() <= stripe_segment.get_ll_x()) {
        continue;
      }
      if (high_side) {
        if (rail_segment.get_ll_y() < routing_halo_rect.get_ur_y()) {
          continue;
        }
        int32_t distance = rail_segment.get_ll_y() - routing_halo_rect.get_ur_y();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ll_y();
        }
      } else {
        if (routing_halo_rect.get_ll_y() < rail_segment.get_ur_y()) {
          continue;
        }
        int32_t distance = routing_halo_rect.get_ll_y() - rail_segment.get_ur_y();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ur_y();
        }
      }
    } else {
      if (!rail_segment.is_vertical() || stripe_segment.get_ur_y() <= rail_segment.get_ll_y()
          || rail_segment.get_ur_y() <= stripe_segment.get_ll_y()) {
        continue;
      }
      if (high_side) {
        if (rail_segment.get_ll_x() < routing_halo_rect.get_ur_x()) {
          continue;
        }
        int32_t distance = rail_segment.get_ll_x() - routing_halo_rect.get_ur_x();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ll_x();
        }
      } else {
        if (routing_halo_rect.get_ll_x() < rail_segment.get_ur_x()) {
          continue;
        }
        int32_t distance = routing_halo_rect.get_ll_x() - rail_segment.get_ur_x();
        if (distance < closest_distance) {
          closest_distance = distance;
          closest_rail_edge_coord = rail_segment.get_ur_x();
        }
      }
    }
  }
  return closest_rail_edge_coord;
}

int32_t PDNGenerator::getClosestCrossStripeEdgeCoord(PGSegment& stripe_segment, Instance& instance, int32_t rail_coord, bool high_side)
{
  PlanarRect& routing_halo_rect = instance.get_routing_halo_rect();
  bool vertical = stripe_segment.is_vertical();
  int32_t half_width = stripe_segment.get_width() / 2;
  int32_t closest_full_overlap_coord = INT32_MAX;
  int32_t closest_full_overlap_gap_distance = INT32_MAX;
  int32_t closest_full_overlap_extension_distance = INT32_MAX;
  int32_t closest_contact_coord = INT32_MAX;
  int32_t closest_contact_gap_distance = INT32_MAX;
  int32_t closest_contact_extension_distance = INT32_MAX;
  for (PGSegment& cross_stripe : FPDM.getDatabase().get_pg_segment_list()) {
    if (cross_stripe.get_type() != PGSegmentType::kStripe || cross_stripe.get_net_name() != stripe_segment.get_net_name()
        || (vertical && !cross_stripe.is_horizontal()) || (!vertical && !cross_stripe.is_vertical())) {
      continue;
    }

    bool connect_layers = false;
    for (PGLayerPair& pg_layer_pair : FPDM.getConfig().pg_layer_pair_list) {
      if ((pg_layer_pair.get_first_layer_name() == stripe_segment.get_layer_name()
           && pg_layer_pair.get_second_layer_name() == cross_stripe.get_layer_name())
          || (pg_layer_pair.get_second_layer_name() == stripe_segment.get_layer_name()
              && pg_layer_pair.get_first_layer_name() == cross_stripe.get_layer_name())) {
        connect_layers = true;
        break;
      }
    }
    if (!connect_layers) {
      continue;
    }

    int32_t full_overlap_coord = INT32_MAX;
    int32_t contact_coord = INT32_MAX;
    int32_t gap_distance = INT32_MAX;
    bool full_overlap_valid = false;
    bool contact_valid = false;
    if (vertical) {
      int32_t stripe_ll_x = stripe_segment.get_start_x() - half_width;
      int32_t stripe_ur_x = stripe_segment.get_start_x() + half_width;
      int32_t cross_begin_x = std::min(cross_stripe.get_start_x(), cross_stripe.get_end_x());
      int32_t cross_end_x = std::max(cross_stripe.get_start_x(), cross_stripe.get_end_x());
      bool full_width_overlap = cross_begin_x <= stripe_ll_x && stripe_ur_x <= cross_end_x;
      bool positive_width_overlap
          = std::max(stripe_ll_x, cross_stripe.get_ll_x()) < std::min(stripe_ur_x, cross_stripe.get_ur_x());
      if (!positive_width_overlap) {
        continue;
      }
      if (high_side) {
        full_overlap_coord = cross_stripe.get_ll_y();
        contact_coord = cross_stripe.get_ur_y();
        gap_distance = std::max(rail_coord - cross_stripe.get_ur_y(), 0);
        full_overlap_valid = full_width_overlap && routing_halo_rect.get_ur_y() <= full_overlap_coord && full_overlap_coord <= rail_coord;
        contact_valid = routing_halo_rect.get_ur_y() <= contact_coord && contact_coord <= rail_coord;
      } else {
        full_overlap_coord = cross_stripe.get_ur_y();
        contact_coord = cross_stripe.get_ll_y();
        gap_distance = std::max(cross_stripe.get_ll_y() - rail_coord, 0);
        full_overlap_valid = full_width_overlap && rail_coord <= full_overlap_coord && full_overlap_coord <= routing_halo_rect.get_ll_y();
        contact_valid = rail_coord <= contact_coord && contact_coord <= routing_halo_rect.get_ll_y();
      }
    } else {
      int32_t stripe_ll_y = stripe_segment.get_start_y() - half_width;
      int32_t stripe_ur_y = stripe_segment.get_start_y() + half_width;
      int32_t cross_begin_y = std::min(cross_stripe.get_start_y(), cross_stripe.get_end_y());
      int32_t cross_end_y = std::max(cross_stripe.get_start_y(), cross_stripe.get_end_y());
      bool full_width_overlap = cross_begin_y <= stripe_ll_y && stripe_ur_y <= cross_end_y;
      bool positive_width_overlap
          = std::max(stripe_ll_y, cross_stripe.get_ll_y()) < std::min(stripe_ur_y, cross_stripe.get_ur_y());
      if (!positive_width_overlap) {
        continue;
      }
      if (high_side) {
        full_overlap_coord = cross_stripe.get_ll_x();
        contact_coord = cross_stripe.get_ur_x();
        gap_distance = std::max(rail_coord - cross_stripe.get_ur_x(), 0);
        full_overlap_valid = full_width_overlap && routing_halo_rect.get_ur_x() <= full_overlap_coord && full_overlap_coord <= rail_coord;
        contact_valid = routing_halo_rect.get_ur_x() <= contact_coord && contact_coord <= rail_coord;
      } else {
        full_overlap_coord = cross_stripe.get_ur_x();
        contact_coord = cross_stripe.get_ll_x();
        gap_distance = std::max(cross_stripe.get_ll_x() - rail_coord, 0);
        full_overlap_valid = full_width_overlap && rail_coord <= full_overlap_coord && full_overlap_coord <= routing_halo_rect.get_ll_x();
        contact_valid = rail_coord <= contact_coord && contact_coord <= routing_halo_rect.get_ll_x();
      }
    }

    // Repair only the small gaps that buildLayerConnect already treats as overlaps because it expands line endpoints by half the width.
    contact_valid = contact_valid && 0 < gap_distance && gap_distance < half_width;
    if (full_overlap_valid) {
      int32_t extension_distance = std::abs(full_overlap_coord - rail_coord);
      if (gap_distance < closest_full_overlap_gap_distance
          || (gap_distance == closest_full_overlap_gap_distance
              && extension_distance < closest_full_overlap_extension_distance)) {
        closest_full_overlap_gap_distance = gap_distance;
        closest_full_overlap_extension_distance = extension_distance;
        closest_full_overlap_coord = full_overlap_coord;
      }
    }
    if (contact_valid) {
      int32_t extension_distance = std::abs(contact_coord - rail_coord);
      if (gap_distance < closest_contact_gap_distance
          || (gap_distance == closest_contact_gap_distance && extension_distance < closest_contact_extension_distance)) {
        closest_contact_gap_distance = gap_distance;
        closest_contact_extension_distance = extension_distance;
        closest_contact_coord = contact_coord;
      }
    }
  }
  return closest_full_overlap_coord != INT32_MAX ? closest_full_overlap_coord : closest_contact_coord;
}

void PDNGenerator::buildLayerConnect(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  for (PGLayerPair& pg_layer_pair : FPDM.getConfig().pg_layer_pair_list) {
    RoutingLayer* first_layer = findRoutingLayer(pg_layer_pair.get_first_layer_name());
    RoutingLayer* second_layer = findRoutingLayer(pg_layer_pair.get_second_layer_name());
    if (first_layer == nullptr || second_layer == nullptr || first_layer->get_prefer_direction() == second_layer->get_prefer_direction()) {
      continue;
    }
    RoutingLayer* bottom_layer = first_layer;
    RoutingLayer* top_layer = second_layer;
    if (top_layer->get_order() < bottom_layer->get_order()) {
      std::swap(bottom_layer, top_layer);
    }

    std::vector<PGSegment> pg_segment_list = database.get_pg_segment_list();
    for (PGNet& pg_net : database.get_pg_net_list()) {
      std::vector<PGSegment> bottom_segment_list;
      std::vector<PGSegment> top_segment_list;
      for (PGSegment& pg_segment : pg_segment_list) {
        if (!pg_segment.is_line() || pg_segment.get_net_name() != pg_net.get_name()) {
          continue;
        }
        if (pg_segment.get_layer_name() == bottom_layer->get_name()) {
          bottom_segment_list.push_back(pg_segment);
        } else if (pg_segment.get_layer_name() == top_layer->get_name()) {
          top_segment_list.push_back(pg_segment);
        }
      }
      for (PGSegment& bottom_segment : bottom_segment_list) {
        PlanarRect bottom_rect(bottom_segment.get_ll_x(), bottom_segment.get_ll_y(), bottom_segment.get_ur_x(), bottom_segment.get_ur_y());
        for (PGSegment& top_segment : top_segment_list) {
          PlanarRect top_rect(top_segment.get_ll_x(), top_segment.get_ll_y(), top_segment.get_ur_x(), top_segment.get_ur_y());
          PlanarRect overlap_rect = getOverlapRect(bottom_rect, top_rect);
          if (overlap_rect.get_width() <= 0 || overlap_rect.get_height() <= 0) {
            continue;
          }
          addViaSegment(pg_model, pg_net.get_name(), bottom_layer->get_name(), top_layer->get_name(), "",
                        (overlap_rect.get_ll_x() + overlap_rect.get_ur_x()) / 2, (overlap_rect.get_ll_y() + overlap_rect.get_ur_y()) / 2,
                        overlap_rect.get_width(), overlap_rect.get_height());
        }
      }
    }
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

PlanarRect PDNGenerator::getOverlapRect(PlanarRect first_rect, PlanarRect second_rect)
{
  PlanarRect overlap_rect;
  overlap_rect.set_rect(std::max(first_rect.get_ll_x(), second_rect.get_ll_x()), std::max(first_rect.get_ll_y(), second_rect.get_ll_y()),
                        std::min(first_rect.get_ur_x(), second_rect.get_ur_x()), std::min(first_rect.get_ur_y(), second_rect.get_ur_y()));
  return overlap_rect;
}

void PDNGenerator::addViaSegment(PGModel& pg_model, std::string net_name, std::string bottom_layer_name, std::string top_layer_name,
                                 std::string cut_layer_name, int32_t x, int32_t y, int32_t width, int32_t height)
{
  std::string via_key = FPUTIL.getString(net_name, "|", bottom_layer_name, "|", top_layer_name, "|", cut_layer_name, "|", x, "|", y, "|",
                                         width, "|", height);
  if (pg_model.get_via_key_set().contains(via_key)) {
    return;
  }
  pg_model.get_via_key_set().insert(via_key);

  PGSegment pg_segment;
  pg_segment.set_net_name(net_name);
  pg_segment.set_type(PGSegmentType::kVia);
  pg_segment.set_bottom_layer_name(bottom_layer_name);
  pg_segment.set_top_layer_name(top_layer_name);
  pg_segment.set_cut_layer_name(cut_layer_name);
  pg_segment.set_start_coord(x, y);
  pg_segment.set_via_width(std::max(width, 1));
  pg_segment.set_via_height(std::max(height, 1));
  pg_segment.set_generated(true);
  FPDM.getDatabase().get_pg_segment_list().push_back(pg_segment);
}

void PDNGenerator::buildMacroConnect(PGModel& pg_model)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  Database& database = FPDM.getDatabase();
  int32_t macro_pin_num = 0;
  for (PGNet& pg_net : database.get_pg_net_list()) {
    for (Instance& instance : database.get_instance_list()) {
      if (!instance.get_macro() || !instance.get_placed()) {
        continue;
      }
      for (InstancePinShape& pin_shape : instance.get_pin_shape_list()) {
        if (std::find(pg_net.get_instance_pin_name_list().begin(), pg_net.get_instance_pin_name_list().end(), pin_shape.get_pin_name())
            != pg_net.get_instance_pin_name_list().end()) {
          macro_pin_num++;
        }
      }
    }
  }

  int32_t batch_size = std::max(macro_pin_num / 10, 1);
  int32_t processed_macro_pin_num = 0;
  Monitor stage_monitor;
  for (PGNet& pg_net : database.get_pg_net_list()) {
    for (Instance& instance : database.get_instance_list()) {
      if (!instance.get_macro() || !instance.get_placed()) {
        continue;
      }
      for (InstancePinShape& pin_shape : instance.get_pin_shape_list()) {
        if (std::find(pg_net.get_instance_pin_name_list().begin(), pg_net.get_instance_pin_name_list().end(), pin_shape.get_pin_name())
            == pg_net.get_instance_pin_name_list().end()) {
          continue;
        }
        connectMacroPin(pg_model, pg_net, pin_shape);
        processed_macro_pin_num++;
        if (processed_macro_pin_num % batch_size == 0 || processed_macro_pin_num == macro_pin_num) {
          FPLOG.info(Loc::current(), "Processed ", processed_macro_pin_num, "/", macro_pin_num, " macro power pins",
                     stage_monitor.getStatsInfo());
        }
      }
    }
  }

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PDNGenerator::connectMacroPin(PGModel& pg_model, PGNet& pg_net, InstancePinShape& pin_shape)
{
  RoutingLayer* pin_layer = findRoutingLayer(pin_shape.get_layer_name());
  if (pin_layer == nullptr) {
    return;
  }

  Database& database = FPDM.getDatabase();
  PlanarRect pin_rect(pin_shape.get_ll_x(), pin_shape.get_ll_y(), pin_shape.get_ur_x(), pin_shape.get_ur_y());
  std::vector<PGSegment> pg_segment_list = database.get_pg_segment_list();
  int32_t connect_layer_order = INT32_MAX;
  for (PGSegment& pg_segment : pg_segment_list) {
    if (!pg_segment.is_line() || pg_segment.get_net_name() != pg_net.get_name()) {
      continue;
    }
    RoutingLayer* routing_layer = findRoutingLayer(pg_segment.get_layer_name());
    if (routing_layer == nullptr || routing_layer->get_order() <= pin_layer->get_order()) {
      continue;
    }
    PlanarRect segment_rect(pg_segment.get_ll_x(), pg_segment.get_ll_y(), pg_segment.get_ur_x(), pg_segment.get_ur_y());
    PlanarRect overlap_rect = getOverlapRect(pin_rect, segment_rect);
    if (overlap_rect.get_width() <= 0 || overlap_rect.get_height() <= 0) {
      continue;
    }
    connect_layer_order = std::min(connect_layer_order, routing_layer->get_order());
  }
  if (connect_layer_order == INT32_MAX) {
    return;
  }

  for (PGSegment& pg_segment : pg_segment_list) {
    if (!pg_segment.is_line() || pg_segment.get_net_name() != pg_net.get_name()) {
      continue;
    }
    RoutingLayer* routing_layer = findRoutingLayer(pg_segment.get_layer_name());
    if (routing_layer == nullptr || routing_layer->get_order() != connect_layer_order) {
      continue;
    }
    PlanarRect segment_rect(pg_segment.get_ll_x(), pg_segment.get_ll_y(), pg_segment.get_ur_x(), pg_segment.get_ur_y());
    PlanarRect overlap_rect = getOverlapRect(pin_rect, segment_rect);
    if (overlap_rect.get_width() <= 0 || overlap_rect.get_height() <= 0) {
      continue;
    }
    addViaSegment(pg_model, pg_net.get_name(), pin_layer->get_name(), routing_layer->get_name(), "",
                  (overlap_rect.get_ll_x() + overlap_rect.get_ur_x()) / 2, (overlap_rect.get_ll_y() + overlap_rect.get_ur_y()) / 2,
                  overlap_rect.get_width(), overlap_rect.get_height());
  }
}

// private

PDNGenerator* PDNGenerator::_pg_instance = nullptr;

}  // namespace ifp
