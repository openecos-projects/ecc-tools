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
#include "LayerAssigner.hpp"

#include <algorithm>

#include "GDSPlotter.hpp"
#include "Monitor.hpp"
#include "RTInterface.hpp"
#include "Utility.hpp"

namespace irt {

// public

void LayerAssigner::initInst()
{
  if (_la_instance == nullptr) {
    _la_instance = new LayerAssigner();
  }
}

LayerAssigner& LayerAssigner::getInst()
{
  if (_la_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_la_instance;
}

void LayerAssigner::destroyInst()
{
  if (_la_instance != nullptr) {
    delete _la_instance;
    _la_instance = nullptr;
  }
}

// function

void LayerAssigner::assign()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  clearRoutingEdgeDemand();

  LAModel la_model = initLAModel();
  setLAComParam(la_model);
  initLATaskList(la_model);
  buildPlaneTree(la_model);
  routeLAModel(la_model);
  // debugPlotLAModel(la_model, "after");
  updateSummary(la_model);
  printSummary(la_model);
  outputGuide(la_model);
  outputNetCSV(la_model);
  outputOverflowCSV(la_model);
  RTDM.getDatabase().get_net_global_result_map() = std::move(la_model.get_net_global_result_map());
  RTDM.rebuildGlobalResultRTree();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LayerAssigner::clearRoutingEdgeDemand()
{
  for (std::vector<GridMap<RoutingEdge>>* routing_edge_map_list :
       {&RTDM.getDatabase().get_routing_h_edge_map(), &RTDM.getDatabase().get_routing_v_edge_map()}) {
#pragma omp parallel for
    for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_edge_map_list->size()); layer_idx++) {
      GridMap<RoutingEdge>& routing_edge_map = (*routing_edge_map_list)[layer_idx];
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map.get_y_size(); y++) {
          routing_edge_map[x][y].set_demand(0);
          routing_edge_map[x][y].get_demand_net_idx_list().clear();
        }
      }
    }
  }
}

// private

LayerAssigner* LayerAssigner::_la_instance = nullptr;

// model

LAModel LayerAssigner::initLAModel()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();

  LAModel la_model;
  la_model.set_la_net_list(convertToLANetList(net_list));
  la_model.get_net_global_result_map() = RTDM.getDatabase().get_net_global_result_map();
  return la_model;
}

std::vector<LANet> LayerAssigner::convertToLANetList(std::vector<Net>& net_list)
{
  std::vector<LANet> la_net_list;
  la_net_list.reserve(net_list.size());
  for (Net& net : net_list) {
    la_net_list.emplace_back(convertToLANet(net));
  }
  return la_net_list;
}

LANet LayerAssigner::convertToLANet(Net& net)
{
  LANet la_net;
  la_net.set_origin_net(&net);
  la_net.set_net_idx(net.get_net_idx());
  la_net.set_connect_type(net.get_connect_type());
  for (Pin& pin : net.get_pin_list()) {
    la_net.get_la_pin_list().emplace_back(pin);
  }
  la_net.set_bounding_box(net.get_bounding_box());
  return la_net;
}

void LayerAssigner::setLAComParam(LAModel& la_model)
{
  double prefer_wire_unit = 1;
  double non_prefer_wire_unit = 2.5 * prefer_wire_unit;
  double via_unit = 2 * non_prefer_wire_unit;
  double overflow_unit = 4 * non_prefer_wire_unit;
  LAComParam la_com_param(via_unit, overflow_unit);
  RTLOG.info(Loc::current(), "via_unit: ", la_com_param.get_via_unit());
  RTLOG.info(Loc::current(), "overflow_unit: ", la_com_param.get_overflow_unit());
  la_model.set_la_com_param(la_com_param);
}

void LayerAssigner::initLATaskList(LAModel& la_model)
{
  std::vector<LANet>& la_net_list = la_model.get_la_net_list();
  std::vector<LANet*>& la_task_list = la_model.get_la_task_list();
  la_task_list.reserve(la_net_list.size());
  for (LANet& la_net : la_net_list) {
    la_task_list.push_back(&la_net);
  }
  std::ranges::sort(la_task_list, CmpLANet());
}

double LayerAssigner::getOverflowCost(RoutingEdge& routing_edge, double overflow_unit, int32_t net_idx)
{
  constexpr double blocked_edge_cost = 1e12;

  if (routing_edge.get_ignore_net_set().contains(net_idx)) {
    return 0;
  }
  if (routing_edge.get_supply() <= 0) {
    return blocked_edge_cost;
  }

  int32_t demand = routing_edge.get_demand() + 1;
  int32_t supply = routing_edge.get_supply();
  if (demand == supply) {
    return overflow_unit;
  }
  if (demand > supply) {
    double overflow = demand - supply + 1;
    return overflow_unit * overflow * overflow * overflow * overflow;
  }
  double usage_ratio = demand / 1.0 / supply;
  return overflow_unit * usage_ratio * usage_ratio * usage_ratio * usage_ratio;
}

// route

void LayerAssigner::buildPlaneTree(LAModel& la_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<LANet>& la_net_list = la_model.get_la_net_list();
  std::map<int32_t, std::vector<Segment<LayerCoord>>>& net_global_result_map = la_model.get_net_global_result_map();

#pragma omp parallel for schedule(dynamic, 1)
  for (int32_t net_idx = 0; net_idx < static_cast<int32_t>(la_net_list.size()); net_idx++) {
    LANet& la_net = la_net_list[net_idx];
    std::vector<Segment<LayerCoord>> routing_segment_list;
    auto result_iter = net_global_result_map.find(la_net.get_net_idx());
    if (result_iter != net_global_result_map.end()) {
      routing_segment_list = std::move(result_iter->second);
    }
    std::vector<LayerCoord> candidate_root_coord_list;
    std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
    std::vector<LAPin>& la_pin_list = la_net.get_la_pin_list();
    candidate_root_coord_list.reserve(la_pin_list.size());
    for (size_t i = 0; i < la_pin_list.size(); i++) {
      LayerCoord coord(la_pin_list[i].get_access_point().get_grid_coord(), 0);
      candidate_root_coord_list.emplace_back(coord);
      key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
    }
    la_net.set_planar_tree(RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map));
  }
  la_model.get_net_global_result_map().clear();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LayerAssigner::routeLAModel(LAModel& la_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<LANet*>& la_task_list = la_model.get_la_task_list();

  int32_t batch_size = RTUTIL.getBatchSize(la_task_list.size());

  Monitor stage_monitor;
  for (size_t i = 0; i < la_task_list.size(); i++) {
    routeLATask(la_model, la_task_list[i]);
    if ((i + 1) % batch_size == 0 || (i + 1) == la_task_list.size()) {
      RTLOG.info(Loc::current(), "Routed ", (i + 1), "/", la_task_list.size(), "(", RTUTIL.getPercentage(i + 1, la_task_list.size()), ") nets",
                 stage_monitor.getStatsInfo());
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LayerAssigner::routeLATask(LAModel& la_model, LANet* la_task)
{
  initSingleTask(la_model, la_task);
  if (needRouting(la_model)) {
    routeSingleTask(la_model);
  }
  resetSingleTask(la_model);
}

void LayerAssigner::initSingleTask(LAModel& la_model, LANet* la_task)
{
  la_model.set_curr_la_task(la_task);
  la_model.get_routing_tree().clear();
}

void LayerAssigner::routeSingleTask(LAModel& la_model)
{
  buildRoutingTree(la_model);
  RoutingSegmentList routing_segment_list = RTUTIL.getSegListByTree(la_model.get_routing_tree());
  updateRoutingTreeToGraph(la_model, routing_segment_list, ChangeType::kAdd);
  std::vector<LAOverflowSegment> overflow_segment_list = getOverflowSegmentList(la_model);
  if (!overflow_segment_list.empty()) {
    updateRoutingTreeToGraph(la_model, routing_segment_list, ChangeType::kDel);
    splitPlaneTreeByOverflow(la_model, overflow_segment_list);
    buildRoutingTree(la_model);
    routing_segment_list = RTUTIL.getSegListByTree(la_model.get_routing_tree());
    updateRoutingTreeToGraph(la_model, routing_segment_list, ChangeType::kAdd);
  }
  uploadNetResult(la_model, routing_segment_list);
}

bool LayerAssigner::needRouting(LAModel& la_model)
{
  return (la_model.get_curr_la_task()->get_planar_tree().get_root() != nullptr);
}

std::vector<LayerAssigner::LAOverflowSegment> LayerAssigner::getOverflowSegmentList(LAModel& la_model)
{
  constexpr int32_t refine_level = 2;  // 0: off, 1: conservative, 2: aggressive, 3: unrestricted
  if (refine_level == 0) {
    return {};
  }
  int32_t curr_net_idx = la_model.get_curr_la_task()->get_net_idx();
  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  int32_t max_refine_segment_num = 3;
  int32_t min_subsegment_length = 4;
  if (refine_level == 2) {
    max_refine_segment_num = 8;
    min_subsegment_length = 2;
  } else if (refine_level == 3) {
    min_subsegment_length = 1;
  }

  std::vector<LAOverflowSegment> overflow_segment_list;
  TNode<LAPillar>* pillar_tree_root = la_model.get_curr_la_task()->get_pillar_tree().get_root();
  std::queue<TNode<LAPillar>*> pillar_node_queue = RTUTIL.initQueue(pillar_tree_root);
  while (!pillar_node_queue.empty()) {
    TNode<LAPillar>* parent_pillar_node = RTUTIL.getFrontAndPop(pillar_node_queue);
    PlanarCoord parent_coord = parent_pillar_node->value().get_planar_coord();
    for (TNode<LAPillar>* child_node : parent_pillar_node->get_child_list()) {
      PlanarCoord child_coord = child_node->value().get_planar_coord();
      int32_t segment_length = RTUTIL.getManhattanDistance(parent_coord, child_coord);
      if (segment_length < 2 * min_subsegment_length) {
        continue;
      }
      if (!RTUTIL.isRightAngled(parent_coord, child_coord)) {
        RTLOG.error(Loc::current(), "The segment is oblique!");
      }

      int32_t layer_idx = child_node->value().get_layer_idx();
      int32_t step_x = parent_coord.get_x() < child_coord.get_x() ? 1 : (child_coord.get_x() < parent_coord.get_x() ? -1 : 0);
      int32_t step_y = parent_coord.get_y() < child_coord.get_y() ? 1 : (child_coord.get_y() < parent_coord.get_y() ? -1 : 0);
      bool is_horizontal = step_x != 0;
      GridMap<RoutingEdge>& routing_edge_map = is_horizontal ? routing_h_edge_map[layer_idx] : routing_v_edge_map[layer_idx];
      int32_t peak_offset = -1;
      std::vector<int32_t> overflow_list;
      overflow_list.reserve(segment_length);
      LAOverflowSegment overflow_segment;
      overflow_segment.first_coord = parent_coord;
      overflow_segment.second_coord = child_coord;
      for (int32_t offset = 0; offset < segment_length; offset++) {
        int32_t edge_x = parent_coord.get_x() + (step_x * offset);
        int32_t edge_y = parent_coord.get_y() + (step_y * offset);
        if (step_x < 0) {
          edge_x--;
        }
        if (step_y < 0) {
          edge_y--;
        }
        RoutingEdge& routing_edge = is_horizontal ? routing_edge_map[edge_x][parent_coord.get_y()] : routing_edge_map[parent_coord.get_x()][edge_y];
        int32_t overflow = routing_edge.get_ignore_net_set().contains(curr_net_idx) ? 0 : routing_edge.get_overflow();
        overflow_list.push_back(overflow);
        overflow_segment.total_overflow += overflow;
        if (overflow > overflow_segment.max_overflow) {
          overflow_segment.max_overflow = overflow;
          peak_offset = offset;
        }
      }
      if (peak_offset == -1) {
        continue;
      }

      int32_t hotspot_threshold = std::max(1, overflow_segment.max_overflow / 2);
      int32_t hotspot_first_offset = peak_offset;
      int32_t hotspot_second_offset = peak_offset;
      while (hotspot_first_offset > 0 && overflow_list[hotspot_first_offset - 1] >= hotspot_threshold) {
        hotspot_first_offset--;
      }
      while (hotspot_second_offset + 1 < segment_length && overflow_list[hotspot_second_offset + 1] >= hotspot_threshold) {
        hotspot_second_offset++;
      }

      if (refine_level == 1 || segment_length < 3 * min_subsegment_length) {
        int32_t split_offset = std::clamp(peak_offset + 1, min_subsegment_length, segment_length - min_subsegment_length);
        overflow_segment.split_coord_list.emplace_back(parent_coord.get_x() + (step_x * split_offset), parent_coord.get_y() + (step_y * split_offset));
      } else if (refine_level == 2) {
        int32_t first_split_offset = std::clamp(hotspot_first_offset, min_subsegment_length, segment_length - (2 * min_subsegment_length));
        int32_t second_split_offset = std::clamp(hotspot_second_offset + 1, first_split_offset + min_subsegment_length, segment_length - min_subsegment_length);
        overflow_segment.split_coord_list.emplace_back(parent_coord.get_x() + (step_x * first_split_offset),
                                                       parent_coord.get_y() + (step_y * first_split_offset));
        overflow_segment.split_coord_list.emplace_back(parent_coord.get_x() + (step_x * second_split_offset),
                                                       parent_coord.get_y() + (step_y * second_split_offset));
      } else {
        for (int32_t offset = 1; offset < segment_length; offset++) {
          overflow_segment.split_coord_list.emplace_back(parent_coord.get_x() + (step_x * offset), parent_coord.get_y() + (step_y * offset));
        }
      }
      overflow_segment_list.push_back(std::move(overflow_segment));
    }
    RTUTIL.addListToQueue(pillar_node_queue, parent_pillar_node->get_child_list());
  }

  std::ranges::sort(overflow_segment_list, [](const LAOverflowSegment& a, const LAOverflowSegment& b) {
    if (a.total_overflow != b.total_overflow) {
      return a.total_overflow > b.total_overflow;
    }
    return a.max_overflow > b.max_overflow;
  });
  if (refine_level != 3 && static_cast<int32_t>(overflow_segment_list.size()) > max_refine_segment_num) {
    overflow_segment_list.resize(max_refine_segment_num);
  }
  return overflow_segment_list;
}

void LayerAssigner::splitPlaneTreeByOverflow(LAModel& la_model, std::vector<LAOverflowSegment>& overflow_segment_list)
{
  TNode<LayerCoord>* planar_tree_root = la_model.get_curr_la_task()->get_planar_tree().get_root();
  std::queue<TNode<LayerCoord>*> planar_node_queue = RTUTIL.initQueue(planar_tree_root);
  while (!planar_node_queue.empty()) {
    TNode<LayerCoord>* planar_node = RTUTIL.getFrontAndPop(planar_node_queue);
    std::vector<TNode<LayerCoord>*> child_list = planar_node->get_child_list();
    for (TNode<LayerCoord>* child_node : child_list) {
      PlanarCoord parent_coord = planar_node->value().get_planar_coord();
      PlanarCoord child_coord = child_node->value().get_planar_coord();
      for (LAOverflowSegment& overflow_segment : overflow_segment_list) {
        if (overflow_segment.is_split || parent_coord != overflow_segment.first_coord || child_coord != overflow_segment.second_coord) {
          continue;
        }
        planar_node->delChild(child_node);
        TNode<LayerCoord>* curr_node = planar_node;
        for (PlanarCoord& split_coord : overflow_segment.split_coord_list) {
          auto* split_node = new TNode<LayerCoord>(LayerCoord(split_coord, 0));
          curr_node->addChild(split_node);
          curr_node = split_node;
        }
        curr_node->addChild(child_node);
        overflow_segment.is_split = true;
        break;
      }
    }
    RTUTIL.addListToQueue(planar_node_queue, child_list);
  }
}

// layer assignment

void LayerAssigner::buildPillarTree(LAModel& la_model)
{
  LANet* curr_la_task = la_model.get_curr_la_task();

  std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC> coord_pin_layer_map;
  for (LAPin& la_pin : curr_la_task->get_la_pin_list()) {
    AccessPoint& access_point = la_pin.get_access_point();
    coord_pin_layer_map[access_point.get_grid_coord()].insert(access_point.get_layer_idx());
  }
  std::function<LAPillar(LayerCoord&, std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC>&)> convert
      = [](LayerCoord& layer_coord, std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC>& coord_pin_layer_map) {
          LAPillar la_pillar;
          la_pillar.set_planar_coord(layer_coord.get_planar_coord());
          auto pin_layer_iter = coord_pin_layer_map.find(layer_coord.get_planar_coord());
          if (pin_layer_iter != coord_pin_layer_map.end()) {
            la_pillar.set_pin_layer_idx_set(pin_layer_iter->second);
          }
          return la_pillar;
        };
  curr_la_task->set_pillar_tree(RTUTIL.convertTree(curr_la_task->get_planar_tree(), convert, coord_pin_layer_map));
}

void LayerAssigner::assignPillarTree(LAModel& la_model)
{
  buildSubtreeCost(la_model);
  assignLayer(la_model);
}

void LayerAssigner::buildSubtreeCost(LAModel& la_model)
{
  TNode<LAPillar>* pillar_tree_root = la_model.get_curr_la_task()->get_pillar_tree().get_root();

  std::map<TNode<LAPillar>*, std::vector<int32_t>> candidate_layer_idx_list_map;
  std::queue<TNode<LAPillar>*> pillar_node_queue = RTUTIL.initQueue(pillar_tree_root);
  while (!pillar_node_queue.empty()) {
    TNode<LAPillar>* parent_pillar_node = RTUTIL.getFrontAndPop(pillar_node_queue);
    for (TNode<LAPillar>* child_node : parent_pillar_node->get_child_list()) {
      LAPackage la_package(parent_pillar_node, child_node);
      candidate_layer_idx_list_map.emplace(child_node, getCandidateLayerList(la_package));
    }
    RTUTIL.addListToQueue(pillar_node_queue, parent_pillar_node->get_child_list());
  }

  std::vector<std::vector<TNode<LAPillar>*>> level_list = RTUTIL.getLevelOrder(la_model.get_curr_la_task()->get_pillar_tree());
  const std::vector<int32_t> root_incoming_layer_idx_list{-1};
  for (int32_t i = static_cast<int32_t>(level_list.size()) - 1; i >= 0; i--) {
    std::vector<TNode<LAPillar>*>& level_node_list = level_list[i];
    for (int32_t node_idx = 0; node_idx < static_cast<int32_t>(level_node_list.size()); node_idx++) {
      TNode<LAPillar>* pillar_node = level_node_list[node_idx];
      LAPillar& pillar = pillar_node->value();
      std::vector<LALayerCost>& layer_cost_list = pillar.get_layer_cost_list();
      layer_cost_list.clear();

      const std::vector<int32_t>& incoming_layer_idx_list
          = pillar_node == pillar_tree_root ? root_incoming_layer_idx_list : candidate_layer_idx_list_map.at(pillar_node);
      layer_cost_list.reserve(incoming_layer_idx_list.size());

      std::vector<std::vector<double>> child_base_cost_list_list;
      std::vector<TNode<LAPillar>*>& child_list = pillar_node->get_child_list();
      child_base_cost_list_list.reserve(child_list.size());
      std::set<int32_t> child_boundary_layer_idx_set;
      for (TNode<LAPillar>* child_node : child_list) {
        LAPackage la_package(pillar_node, child_node);
        const std::vector<int32_t>& child_candidate_layer_idx_list = candidate_layer_idx_list_map.at(child_node);
        std::vector<LALayerCost>& child_layer_cost_list = child_node->value().get_layer_cost_list();
        std::vector<double> child_base_cost_list;
        child_base_cost_list.reserve(child_candidate_layer_idx_list.size());
        for (size_t candidate_idx = 0; candidate_idx < child_candidate_layer_idx_list.size(); candidate_idx++) {
          int32_t layer_idx = child_candidate_layer_idx_list[candidate_idx];
          child_boundary_layer_idx_set.insert(layer_idx);
          double subtree_cost = DBL_MAX;
          if (candidate_idx < child_layer_cost_list.size() && child_layer_cost_list[candidate_idx].get_layer_idx() == layer_idx) {
            subtree_cost = child_layer_cost_list[candidate_idx].get_subtree_cost();
          }
          if (subtree_cost == DBL_MAX) {
            RTLOG.error(Loc::current(), "The child layer cost is not found!");
          }
          child_base_cost_list.push_back(getSegmentCost(la_model, la_package, layer_idx) + subtree_cost);
        }
        child_base_cost_list_list.push_back(std::move(child_base_cost_list));
      }

      for (int32_t incoming_layer_idx : incoming_layer_idx_list) {
        std::set<int32_t> base_layer_idx_set = pillar.get_pin_layer_idx_set();
        std::set<int32_t> boundary_layer_idx_set = child_boundary_layer_idx_set;
        if (incoming_layer_idx != -1) {
          base_layer_idx_set.insert(incoming_layer_idx);
          boundary_layer_idx_set.insert(incoming_layer_idx);
        }

        double min_cost = DBL_MAX;
        std::vector<int32_t> best_child_layer_idx_list;
        if (pillar_node->isLeafNode()) {
          min_cost = getPillarViaCost(la_model, base_layer_idx_set);
        } else {
          // For a fixed via span, each child chooses its best layer independently.
          for (int32_t layer_idx : base_layer_idx_set) {
            boundary_layer_idx_set.insert(layer_idx);
          }
          std::vector<int32_t> boundary_layer_idx_list(boundary_layer_idx_set.begin(), boundary_layer_idx_set.end());
          for (size_t low_idx = 0; low_idx < boundary_layer_idx_list.size(); low_idx++) {
            for (size_t high_idx = low_idx; high_idx < boundary_layer_idx_list.size(); high_idx++) {
              int32_t low_layer_idx = boundary_layer_idx_list[low_idx];
              int32_t high_layer_idx = boundary_layer_idx_list[high_idx];
              if (!base_layer_idx_set.empty() && (low_layer_idx > *base_layer_idx_set.begin() || high_layer_idx < *base_layer_idx_set.rbegin())) {
                continue;
              }

              double curr_cost = la_model.get_la_com_param().get_via_unit() * (high_layer_idx - low_layer_idx);
              std::vector<int32_t> curr_child_layer_idx_list;
              bool valid = true;
              curr_child_layer_idx_list.reserve(child_list.size());
              for (size_t child_idx = 0; child_idx < child_list.size(); child_idx++) {
                double child_min_cost = DBL_MAX;
                int32_t best_child_layer_idx = -1;
                const std::vector<int32_t>& child_candidate_layer_idx_list = candidate_layer_idx_list_map.at(child_list[child_idx]);
                for (size_t candidate_idx = 0; candidate_idx < child_candidate_layer_idx_list.size(); candidate_idx++) {
                  int32_t child_layer_idx = child_candidate_layer_idx_list[candidate_idx];
                  if (child_layer_idx < low_layer_idx || high_layer_idx < child_layer_idx) {
                    continue;
                  }
                  double child_cost = child_base_cost_list_list[child_idx][candidate_idx];
                  if (child_cost < child_min_cost || (child_cost == child_min_cost && child_layer_idx < best_child_layer_idx)) {
                    child_min_cost = child_cost;
                    best_child_layer_idx = child_layer_idx;
                  }
                }
                if (child_min_cost == DBL_MAX) {
                  valid = false;
                  break;
                }
                curr_cost += child_min_cost;
                curr_child_layer_idx_list.push_back(best_child_layer_idx);
              }
              if (!valid) {
                continue;
              }
              if (curr_cost < min_cost || (curr_cost == min_cost && curr_child_layer_idx_list < best_child_layer_idx_list)) {
                min_cost = curr_cost;
                best_child_layer_idx_list = std::move(curr_child_layer_idx_list);
              }
            }
          }
        }
        if (min_cost == DBL_MAX) {
          RTLOG.error(Loc::current(), "The min cost is wrong!");
        }

        LALayerCost layer_cost;
        layer_cost.set_layer_idx(incoming_layer_idx);
        layer_cost.set_subtree_cost(min_cost);
        layer_cost.set_child_layer_idx_list(best_child_layer_idx_list);
        layer_cost_list.push_back(std::move(layer_cost));
      }
    }
  }
}

std::vector<int32_t> LayerAssigner::getCandidateLayerList(LAPackage& la_package)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  int32_t bottom_routing_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  int32_t top_routing_layer_idx = RTDM.getConfig().top_routing_layer_idx;

  Direction direction = RTUTIL.getDirection(la_package.getParentPillar().get_planar_coord(), la_package.getChildPillar().get_planar_coord());

  std::vector<int32_t> candidate_layer_idx_list;
  candidate_layer_idx_list.reserve(routing_layer_list.size());
  for (RoutingLayer& routing_layer : routing_layer_list) {
    if (routing_layer.get_layer_idx() < bottom_routing_layer_idx || top_routing_layer_idx < routing_layer.get_layer_idx()) {
      continue;
    }
    if (direction == Direction::kProximal) {
      candidate_layer_idx_list.push_back(routing_layer.get_layer_idx());
    } else if (direction == routing_layer.get_prefer_direction()) {
      candidate_layer_idx_list.push_back(routing_layer.get_layer_idx());
    }
  }
  return candidate_layer_idx_list;
}

double LayerAssigner::getPillarViaCost(LAModel& la_model, const std::set<int32_t>& layer_idx_set)
{
  if (layer_idx_set.empty()) {
    return 0;
  }
  return la_model.get_la_com_param().get_via_unit() * (*layer_idx_set.rbegin() - *layer_idx_set.begin());
}

double LayerAssigner::getSegmentCost(LAModel& la_model, LAPackage& la_package, int32_t candidate_layer_idx)
{
  double overflow_unit = la_model.get_la_com_param().get_overflow_unit();
  int32_t net_idx = la_model.get_curr_la_task()->get_net_idx();

  PlanarCoord first_coord = la_package.getParentPillar().get_planar_coord();
  PlanarCoord second_coord = la_package.getChildPillar().get_planar_coord();
  if (RTUTIL.isProximal(first_coord, second_coord)) {
    return 0;
  }
  if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
    RTLOG.error(Loc::current(), "The segment is oblique!");
  }
  int32_t first_x = first_coord.get_x();
  int32_t first_y = first_coord.get_y();
  int32_t second_x = second_coord.get_x();
  int32_t second_y = second_coord.get_y();
  RTUTIL.swapByASC(first_x, second_x);
  RTUTIL.swapByASC(first_y, second_y);

  double edge_cost = 0;
  if (RTUTIL.isHorizontal(first_coord, second_coord)) {
    GridMap<RoutingEdge>& routing_edge_map = RTDM.getDatabase().get_routing_h_edge_map()[candidate_layer_idx];
    for (int32_t x = first_x; x < second_x; x++) {
      edge_cost += getOverflowCost(routing_edge_map[x][first_y], overflow_unit, net_idx);
    }
  } else {
    GridMap<RoutingEdge>& routing_edge_map = RTDM.getDatabase().get_routing_v_edge_map()[candidate_layer_idx];
    for (int32_t y = first_y; y < second_y; y++) {
      edge_cost += getOverflowCost(routing_edge_map[first_x][y], overflow_unit, net_idx);
    }
  }
  return edge_cost;
}

void LayerAssigner::assignLayer(LAModel& la_model)
{
  TNode<LAPillar>* pillar_tree_root = la_model.get_curr_la_task()->get_pillar_tree().get_root();
  pillar_tree_root->value().set_layer_idx(-1);

  std::queue<TNode<LAPillar>*> pillar_node_queue = RTUTIL.initQueue(pillar_tree_root);
  while (!pillar_node_queue.empty()) {
    TNode<LAPillar>* pillar_node = RTUTIL.getFrontAndPop(pillar_node_queue);
    LALayerCost* selected_layer_cost = nullptr;
    for (LALayerCost& layer_cost : pillar_node->value().get_layer_cost_list()) {
      if (layer_cost.get_layer_idx() == pillar_node->value().get_layer_idx()) {
        selected_layer_cost = &layer_cost;
        break;
      }
    }
    if (selected_layer_cost == nullptr) {
      RTLOG.error(Loc::current(), "The layer cost is not found!");
    }

    std::vector<TNode<LAPillar>*>& child_list = pillar_node->get_child_list();
    std::vector<int32_t>& child_layer_idx_list = selected_layer_cost->get_child_layer_idx_list();
    if (child_list.size() != child_layer_idx_list.size()) {
      RTLOG.error(Loc::current(), "The child layer count is wrong!");
    }
    for (size_t i = 0; i < child_list.size(); i++) {
      child_list[i]->value().set_layer_idx(child_layer_idx_list[i]);
    }
    RTUTIL.addListToQueue(pillar_node_queue, child_list);
  }
}

// result

void LayerAssigner::buildRoutingTree(LAModel& la_model)
{
  buildPillarTree(la_model);
  assignPillarTree(la_model);
  std::vector<Segment<LayerCoord>> routing_segment_list = getRoutingSegmentList(la_model);
  la_model.set_routing_tree(getCoordTree(la_model, routing_segment_list));
}

std::vector<Segment<LayerCoord>> LayerAssigner::getRoutingSegmentList(LAModel& la_model)
{
  std::vector<Segment<LayerCoord>> routing_segment_list;

  TNode<LAPillar>* pillar_tree_root = la_model.get_curr_la_task()->get_pillar_tree().get_root();
  std::queue<TNode<LAPillar>*> pillar_node_queue = RTUTIL.initQueue(pillar_tree_root);
  while (!pillar_node_queue.empty()) {
    TNode<LAPillar>* parent_pillar_node = RTUTIL.getFrontAndPop(pillar_node_queue);
    std::vector<TNode<LAPillar>*>& child_list = parent_pillar_node->get_child_list();
    {
      int32_t bottom_layer_idx = std::numeric_limits<int32_t>::max();
      int32_t top_layer_idx = std::numeric_limits<int32_t>::min();
      for (int32_t layer_idx : parent_pillar_node->value().get_pin_layer_idx_set()) {
        bottom_layer_idx = std::min(bottom_layer_idx, layer_idx);
        top_layer_idx = std::max(top_layer_idx, layer_idx);
      }
      if (parent_pillar_node != pillar_tree_root) {
        int32_t layer_idx = parent_pillar_node->value().get_layer_idx();
        bottom_layer_idx = std::min(bottom_layer_idx, layer_idx);
        top_layer_idx = std::max(top_layer_idx, layer_idx);
      }
      for (TNode<LAPillar>* child_node : child_list) {
        int32_t layer_idx = child_node->value().get_layer_idx();
        bottom_layer_idx = std::min(bottom_layer_idx, layer_idx);
        top_layer_idx = std::max(top_layer_idx, layer_idx);
      }
      if (bottom_layer_idx <= top_layer_idx) {
        routing_segment_list.emplace_back(LayerCoord(parent_pillar_node->value().get_planar_coord(), bottom_layer_idx),
                                          LayerCoord(parent_pillar_node->value().get_planar_coord(), top_layer_idx));
      }
    }
    for (TNode<LAPillar>* child_node : child_list) {
      routing_segment_list.emplace_back(LayerCoord(parent_pillar_node->value().get_planar_coord(), child_node->value().get_layer_idx()),
                                        LayerCoord(child_node->value().get_planar_coord(), child_node->value().get_layer_idx()));
    }
    RTUTIL.addListToQueue(pillar_node_queue, child_list);
  }
  return routing_segment_list;
}

MTree<LayerCoord> LayerAssigner::getCoordTree(LAModel& la_model, std::vector<Segment<LayerCoord>>& routing_segment_list)
{
  std::vector<LayerCoord> candidate_root_coord_list;
  std::map<LayerCoord, std::set<int32_t>, CmpLayerCoordByXASC> key_coord_pin_map;
  std::vector<LAPin>& la_pin_list = la_model.get_curr_la_task()->get_la_pin_list();
  candidate_root_coord_list.reserve(la_pin_list.size());
  for (size_t i = 0; i < la_pin_list.size(); i++) {
    LayerCoord coord = la_pin_list[i].get_access_point().getGridLayerCoord();
    candidate_root_coord_list.push_back(coord);
    key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
  }
  return RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map);
}

void LayerAssigner::uploadNetResult(LAModel& la_model, const RoutingSegmentList& routing_segment_list)
{
  std::vector<Segment<LayerCoord>>& net_result_list = la_model.get_net_global_result_map()[la_model.get_curr_la_task()->get_net_idx()];
  net_result_list.reserve(net_result_list.size() + routing_segment_list.size());
  for (const Segment<TNode<LayerCoord>*>& coord_segment : routing_segment_list) {
    net_result_list.emplace_back(coord_segment.get_first()->value(), coord_segment.get_second()->value());
  }
}

void LayerAssigner::resetSingleTask(LAModel& la_model)
{
  la_model.get_routing_tree().clear();
  la_model.set_curr_la_task(nullptr);
}

// environment

void LayerAssigner::updateRoutingTreeToGraph(LAModel& la_model, const RoutingSegmentList& routing_segment_list, ChangeType change_type)
{
  int32_t curr_net_idx = la_model.get_curr_la_task()->get_net_idx();
  int32_t delta = 0;
  if (change_type == ChangeType::kAdd) {
    delta = 1;
  } else if (change_type == ChangeType::kDel) {
    delta = -1;
  } else {
    RTLOG.error(Loc::current(), "The change type is error!");
  }

  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  for (const Segment<TNode<LayerCoord>*>& coord_segment : routing_segment_list) {
    LayerCoord first_coord = coord_segment.get_first()->value();
    LayerCoord second_coord = coord_segment.get_second()->value();
    if (first_coord.get_layer_idx() != second_coord.get_layer_idx()) {
      continue;
    }
    if (RTUTIL.isProximal(first_coord, second_coord)) {
      continue;
    }
    if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
      RTLOG.error(Loc::current(), "The orientation is error!");
    }

    int32_t layer_idx = first_coord.get_layer_idx();
    int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
    int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
    int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
    bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
    int32_t edge_num = is_horizontal ? second_x - first_x : second_y - first_y;
    for (int32_t i = 0; i < edge_num; i++) {
      RoutingEdge& routing_edge = is_horizontal ? routing_h_edge_map[layer_idx][first_x + i][first_y] : routing_v_edge_map[layer_idx][first_x][first_y + i];
      if (routing_edge.get_ignore_net_set().contains(curr_net_idx)) {
        continue;
      }
      if (change_type == ChangeType::kDel && routing_edge.get_demand() <= 0) {
        RTLOG.error(Loc::current(), "The routing edge demand is error!");
      }
      std::vector<int32_t>& demand_net_idx_list = routing_edge.get_demand_net_idx_list();
      if (change_type == ChangeType::kAdd) {
        demand_net_idx_list.push_back(curr_net_idx);
      } else {
        auto iter = std::find(demand_net_idx_list.begin(), demand_net_idx_list.end(), curr_net_idx);
        if (iter == demand_net_idx_list.end()) {
          RTLOG.error(Loc::current(), "The routing edge demand net is error!");
        }
        demand_net_idx_list.erase(iter);
      }
      routing_edge.set_demand(routing_edge.get_demand() + delta);
    }
  }
}

// exhibit

void LayerAssigner::updateSummary(LAModel& la_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::vector<std::vector<ViaMaster>>& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
  Summary& summary = RTDM.getDatabase().get_summary();
  int32_t enable_timing = RTDM.getConfig().enable_timing;

  std::map<int32_t, double>& routing_demand_map = summary.la_summary.routing_demand_map;
  double& total_demand = summary.la_summary.total_demand;
  std::map<int32_t, double>& routing_overflow_map = summary.la_summary.routing_overflow_map;
  double& total_overflow = summary.la_summary.total_overflow;
  std::map<int32_t, double>& routing_wire_length_map = summary.la_summary.routing_wire_length_map;
  double& total_wire_length = summary.la_summary.total_wire_length;
  std::map<int32_t, int32_t>& cut_via_num_map = summary.la_summary.cut_via_num_map;
  int32_t& total_via_num = summary.la_summary.total_via_num;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.la_summary.clock_timing_map;

  std::vector<LANet>& la_net_list = la_model.get_la_net_list();

  routing_demand_map.clear();
  total_demand = 0;
  routing_overflow_map.clear();
  total_overflow = 0;
  routing_wire_length_map.clear();
  total_wire_length = 0;
  cut_via_num_map.clear();
  total_via_num = 0;
  clock_timing_map.clear();

  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_h_edge_map.size()); layer_idx++) {
    double routing_demand = 0;
    double routing_overflow = 0;
    for (GridMap<RoutingEdge>* routing_edge_map : {&routing_h_edge_map[layer_idx], &routing_v_edge_map[layer_idx]}) {
#pragma omp parallel for reduction(+ : routing_demand, routing_overflow)
      for (int32_t x = 0; x < routing_edge_map->get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map->get_y_size(); y++) {
          RoutingEdge& routing_edge = (*routing_edge_map)[x][y];
          routing_demand += routing_edge.get_demand();
          routing_overflow += routing_edge.get_overflow();
        }
      }
    }
    routing_demand_map[layer_idx] = routing_demand;
    total_demand += routing_demand;
    routing_overflow_map[layer_idx] = routing_overflow;
    total_overflow += routing_overflow;
  }
  for (auto& [net_idx, segment_set] : la_model.get_net_global_result_map()) {
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      LayerCoord& first_coord = segment->get_first();
      int32_t first_layer_idx = first_coord.get_layer_idx();
      LayerCoord& second_coord = segment->get_second();
      int32_t second_layer_idx = second_coord.get_layer_idx();

      if (first_layer_idx == second_layer_idx) {
        PlanarRect& first_gcell = gcell_map[first_coord.get_x()][first_coord.get_y()];
        PlanarRect& second_gcell = gcell_map[second_coord.get_x()][second_coord.get_y()];
        double wire_length = RTUTIL.getManhattanDistance(first_gcell.getMidPoint(), second_gcell.getMidPoint()) / 1.0 / micron_dbu;
        routing_wire_length_map[first_layer_idx] += wire_length;
        total_wire_length += wire_length;
      } else {
        RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
        for (int32_t layer_idx = first_layer_idx; layer_idx < second_layer_idx; layer_idx++) {
          cut_via_num_map[layer_via_master_list[layer_idx].front().get_cut_layer_idx()]++;
          total_via_num++;
        }
      }
    }
  }
  if (enable_timing) {
    std::vector<std::map<std::string, std::vector<LayerCoord>>> real_pin_coord_map_list;
    real_pin_coord_map_list.resize(la_net_list.size());
    std::vector<std::vector<Segment<LayerCoord>>> routing_segment_list_list;
    routing_segment_list_list.resize(la_net_list.size());
    for (LANet& la_net : la_net_list) {
      for (LAPin& la_pin : la_net.get_la_pin_list()) {
        LayerCoord layer_coord = la_pin.get_access_point().getGridLayerCoord();
        real_pin_coord_map_list[la_net.get_net_idx()][la_pin.get_pin_name()].emplace_back(RTUTIL.getRealRectByGCell(layer_coord, gcell_axis).getMidPoint(),
                                                                                          layer_coord.get_layer_idx());
      }
    }
    for (auto& [net_idx, segment_set] : la_model.get_net_global_result_map()) {
      for (Segment<LayerCoord>& segment_value : segment_set) {
        Segment<LayerCoord>* segment = &segment_value;
        LayerCoord first_layer_coord = segment->get_first();
        LayerCoord first_real_coord(RTUTIL.getRealRectByGCell(first_layer_coord, gcell_axis).getMidPoint(), first_layer_coord.get_layer_idx());
        LayerCoord second_layer_coord = segment->get_second();
        LayerCoord second_real_coord(RTUTIL.getRealRectByGCell(second_layer_coord, gcell_axis).getMidPoint(), second_layer_coord.get_layer_idx());

        routing_segment_list_list[net_idx].emplace_back(first_real_coord, second_real_coord);
      }
    }
    RTI.updateTiming(real_pin_coord_map_list, routing_segment_list_list, clock_timing_map);
  }
}

void LayerAssigner::printSummary(LAModel& la_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = RTDM.getDatabase().get_cut_layer_list();
  Summary& summary = RTDM.getDatabase().get_summary();
  int32_t enable_timing = RTDM.getConfig().enable_timing;

  std::map<int32_t, double>& routing_demand_map = summary.la_summary.routing_demand_map;
  double& total_demand = summary.la_summary.total_demand;
  std::map<int32_t, double>& routing_overflow_map = summary.la_summary.routing_overflow_map;
  double& total_overflow = summary.la_summary.total_overflow;
  std::map<int32_t, double>& routing_wire_length_map = summary.la_summary.routing_wire_length_map;
  double& total_wire_length = summary.la_summary.total_wire_length;
  std::map<int32_t, int32_t>& cut_via_num_map = summary.la_summary.cut_via_num_map;
  int32_t& total_via_num = summary.la_summary.total_via_num;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.la_summary.clock_timing_map;

  fort::char_table routing_demand_map_table;
  {
    routing_demand_map_table.set_cell_text_align(fort::text_align::right);
    routing_demand_map_table << fort::header << "routing"
                             << "demand"
                             << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_demand_map_table << routing_layer.get_layer_name() << routing_demand_map[routing_layer.get_layer_idx()]
                               << RTUTIL.getPercentage(routing_demand_map[routing_layer.get_layer_idx()], total_demand) << fort::endr;
    }
    routing_demand_map_table << fort::header << "Total" << total_demand << RTUTIL.getPercentage(total_demand, total_demand) << fort::endr;
  }
  fort::char_table routing_overflow_map_table;
  {
    routing_overflow_map_table.set_cell_text_align(fort::text_align::right);
    routing_overflow_map_table << fort::header << "routing"
                               << "overflow"
                               << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_overflow_map_table << routing_layer.get_layer_name() << routing_overflow_map[routing_layer.get_layer_idx()]
                                 << RTUTIL.getPercentage(routing_overflow_map[routing_layer.get_layer_idx()], total_overflow) << fort::endr;
    }
    routing_overflow_map_table << fort::header << "Total" << total_overflow << RTUTIL.getPercentage(total_overflow, total_overflow) << fort::endr;
  }
  fort::char_table routing_wire_length_map_table;
  {
    routing_wire_length_map_table.set_cell_text_align(fort::text_align::right);
    routing_wire_length_map_table << fort::header << "routing"
                                  << "wire_length"
                                  << "prop" << fort::endr;
    for (RoutingLayer& routing_layer : routing_layer_list) {
      routing_wire_length_map_table << routing_layer.get_layer_name() << routing_wire_length_map[routing_layer.get_layer_idx()]
                                    << RTUTIL.getPercentage(routing_wire_length_map[routing_layer.get_layer_idx()], total_wire_length) << fort::endr;
    }
    routing_wire_length_map_table << fort::header << "Total" << total_wire_length << RTUTIL.getPercentage(total_wire_length, total_wire_length) << fort::endr;
  }
  fort::char_table cut_via_num_map_table;
  {
    cut_via_num_map_table.set_cell_text_align(fort::text_align::right);
    cut_via_num_map_table << fort::header << "cut"
                          << "#via"
                          << "prop" << fort::endr;
    for (CutLayer& cut_layer : cut_layer_list) {
      cut_via_num_map_table << cut_layer.get_layer_name() << cut_via_num_map[cut_layer.get_layer_idx()]
                            << RTUTIL.getPercentage(cut_via_num_map[cut_layer.get_layer_idx()], total_via_num) << fort::endr;
    }
    cut_via_num_map_table << fort::header << "Total" << total_via_num << RTUTIL.getPercentage(total_via_num, total_via_num) << fort::endr;
  }
  fort::char_table timing_table;
  timing_table.set_cell_text_align(fort::text_align::right);
  if (enable_timing) {
    timing_table << fort::header << "clock_name"
                 << "tns"
                 << "wns"
                 << "freq" << fort::endr;
    for (auto& [clock_name, timing_map] : clock_timing_map) {
      timing_table << clock_name << timing_map["TNS"] << timing_map["WNS"] << timing_map["Freq(MHz)"] << fort::endr;
    }
  }
  RTUTIL.printTableList({routing_demand_map_table, routing_overflow_map_table, routing_wire_length_map_table, cut_via_num_map_table});
  RTUTIL.printTableList({timing_table});
}

void LayerAssigner::outputGuide(LAModel& la_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& la_temp_directory_path = RTDM.getConfig().la_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<LANet>& la_net_list = la_model.get_la_net_list();

  std::ofstream* guide_file_stream = RTUTIL.getOutputFileStream(RTUTIL.getString(la_temp_directory_path, "route.guide"));
  if (guide_file_stream == nullptr) {
    return;
  }
  RTUTIL.pushStream(guide_file_stream, "guide net_name\n");
  RTUTIL.pushStream(guide_file_stream, "pin grid_x grid_y real_x real_y layer energy name\n");
  RTUTIL.pushStream(guide_file_stream, "wire grid1_x grid1_y grid2_x grid2_y real1_x real1_y real2_x real2_y layer\n");
  RTUTIL.pushStream(guide_file_stream, "via grid_x grid_y real_x real_y layer1 layer2\n");

  for (auto& [net_idx, segment_set] : la_model.get_net_global_result_map()) {
    LANet& la_net = la_net_list[net_idx];
    RTUTIL.pushStream(guide_file_stream, "guide ", la_net.get_origin_net()->get_net_name(), "\n");

    for (LAPin& la_pin : la_net.get_la_pin_list()) {
      AccessPoint& access_point = la_pin.get_access_point();
      double grid_x = access_point.get_grid_x();
      double grid_y = access_point.get_grid_y();
      double real_x = access_point.get_real_x() / 1.0 / micron_dbu;
      double real_y = access_point.get_real_y() / 1.0 / micron_dbu;
      std::string layer = routing_layer_list[access_point.get_layer_idx()].get_layer_name();
      std::string connnect;
      if (la_pin.get_is_driven()) {
        connnect = "driven";
      } else {
        connnect = "load";
      }
      RTUTIL.pushStream(guide_file_stream, "pin ", grid_x, " ", grid_y, " ", real_x, " ", real_y, " ", layer, " ", connnect, " ", la_pin.get_pin_name(), "\n");
    }
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      LayerCoord first_layer_coord = segment->get_first();
      double grid1_x = first_layer_coord.get_x();
      double grid1_y = first_layer_coord.get_y();
      int32_t first_layer_idx = first_layer_coord.get_layer_idx();

      PlanarCoord first_mid_coord = RTUTIL.getRealRectByGCell(first_layer_coord, gcell_axis).getMidPoint();
      double real1_x = first_mid_coord.get_x() / 1.0 / micron_dbu;
      double real1_y = first_mid_coord.get_y() / 1.0 / micron_dbu;

      LayerCoord second_layer_coord = segment->get_second();
      double grid2_x = second_layer_coord.get_x();
      double grid2_y = second_layer_coord.get_y();
      int32_t second_layer_idx = second_layer_coord.get_layer_idx();

      PlanarCoord second_mid_coord = RTUTIL.getRealRectByGCell(second_layer_coord, gcell_axis).getMidPoint();
      double real2_x = second_mid_coord.get_x() / 1.0 / micron_dbu;
      double real2_y = second_mid_coord.get_y() / 1.0 / micron_dbu;

      if (first_layer_idx != second_layer_idx) {
        RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
        std::string layer1 = routing_layer_list[first_layer_idx].get_layer_name();
        std::string layer2 = routing_layer_list[second_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "via ", grid1_x, " ", grid1_y, " ", real1_x, " ", real1_y, " ", layer1, " ", layer2, "\n");
      } else {
        std::string layer = routing_layer_list[first_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "wire ", grid1_x, " ", grid1_y, " ", grid2_x, " ", grid2_y, " ", real1_x, " ", real1_y, " ", real2_x, " ", real2_y,
                          " ", layer, "\n");
      }
    }
  }
  RTUTIL.closeFileStream(guide_file_stream);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LayerAssigner::outputNetCSV(LAModel& la_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& la_temp_directory_path = RTDM.getConfig().la_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::ofstream* net_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(la_temp_directory_path, "net_map_", routing_layer.get_layer_name(), ".csv"));
    GridMap<RoutingEdge>& routing_edge_map
        = routing_layer.isPreferH() ? routing_h_edge_map[routing_layer.get_layer_idx()] : routing_v_edge_map[routing_layer.get_layer_idx()];
    for (int32_t y = routing_edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(net_csv_file, routing_edge_map[x][y].get_demand(), ",");
      }
      RTUTIL.pushStream(net_csv_file, "\n");
    }
    RTUTIL.closeFileStream(net_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void LayerAssigner::outputOverflowCSV(LAModel& la_model)
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& la_temp_directory_path = RTDM.getConfig().la_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::ofstream* overflow_csv_file
        = RTUTIL.getOutputFileStream(RTUTIL.getString(la_temp_directory_path, "overflow_map_", routing_layer.get_layer_name(), ".csv"));

    GridMap<RoutingEdge>& routing_edge_map
        = routing_layer.isPreferH() ? routing_h_edge_map[routing_layer.get_layer_idx()] : routing_v_edge_map[routing_layer.get_layer_idx()];
    for (int32_t y = routing_edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(overflow_csv_file, routing_edge_map[x][y].get_overflow(), ",");
      }
      RTUTIL.pushStream(overflow_csv_file, "\n");
    }
    RTUTIL.closeFileStream(overflow_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// debug

void LayerAssigner::debugPlotLAModel(LAModel& la_model, std::string flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& la_temp_directory_path = RTDM.getConfig().la_temp_directory_path;

  int32_t point_size = 5;

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

  // access_point
  for (auto& [net_idx, access_point_set] : RTDM.getNetAccessPointMap(die)) {
    GPStruct access_point_struct(RTUTIL.getString("access_point(net_", net_idx, ")"));
    for (AccessPoint* access_point : access_point_set) {
      int32_t x = access_point->get_real_x();
      int32_t y = access_point->get_real_y();

      GPBoundary access_point_boundary;
      access_point_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(access_point->get_layer_idx()));
      access_point_boundary.set_data_type(static_cast<int32_t>(GPDataType::kAccessPoint));
      access_point_boundary.set_rect(x - point_size, y - point_size, x + point_size, y + point_size);
      access_point_struct.push(access_point_boundary);
    }
    gp_gds.addStruct(access_point_struct);
  }

  // routing result
  for (auto& [net_idx, segment_set] : la_model.get_net_global_result_map()) {
    GPStruct global_result_struct(RTUTIL.getString("global_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      for (NetShape& net_shape : RTDM.getNetGlobalShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kGlobalPath));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        global_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(global_result_struct);
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

  // routing_edge
  {
    GPStruct routing_edge_struct("routing_edge");
    std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
    std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
    for (RoutingLayer& routing_layer : routing_layer_list) {
      int32_t layer_idx = routing_layer.get_layer_idx();
      bool is_horizontal = routing_layer.isPreferH();
      GridMap<RoutingEdge>& routing_edge_map = is_horizontal ? routing_h_edge_map[layer_idx] : routing_v_edge_map[layer_idx];
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map.get_y_size(); y++) {
          RoutingEdge& routing_edge = routing_edge_map[x][y];
          PlanarCoord first_grid_coord(x, y);
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarRect edge_rect
              = RTUTIL.getBoundingBox({RTUTIL.getRealRectByGCell(first_grid_coord, gcell_axis), RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis)});
          int32_t y_reduced_span = std::max(1, edge_rect.getYSpan() / 12);
          int32_t text_y = edge_rect.get_ur_y();

          std::vector<std::string> message_list;
          message_list.push_back(RTUTIL.getString("grid: (", first_grid_coord.get_x(), " , ", first_grid_coord.get_y(), ")-(", second_grid_coord.get_x(), " , ",
                                                  second_grid_coord.get_y(), ")"));
          message_list.push_back(
              RTUTIL.getString("supply: ", routing_edge.get_supply(), ", demand: ", routing_edge.get_demand(), ", overflow: ", routing_edge.get_overflow()));

          std::string ignore_net_message = "ignore_net:";
          for (int32_t net_idx : routing_edge.get_ignore_net_set()) {
            ignore_net_message += RTUTIL.getString(" ", net_idx);
          }
          message_list.push_back(ignore_net_message);

          for (std::string& message : message_list) {
            text_y -= y_reduced_span;
            GPText gp_text;
            gp_text.set_coord(edge_rect.get_ll_x(), text_y);
            gp_text.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            gp_text.set_message(message);
            gp_text.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
            gp_text.set_presentation(GPTextPresentation::kLeftMiddle);
            routing_edge_struct.push(gp_text);
          }
        }
      }
    }
    gp_gds.addStruct(routing_edge_struct);
  }

  // overflow
  {
    GPStruct overflow_struct("overflow");
    std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
    std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();
    for (RoutingLayer& routing_layer : routing_layer_list) {
      int32_t layer_idx = routing_layer.get_layer_idx();
      bool is_horizontal = routing_layer.isPreferH();
      GridMap<RoutingEdge>& routing_edge_map = is_horizontal ? routing_h_edge_map[layer_idx] : routing_v_edge_map[layer_idx];
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map.get_y_size(); y++) {
          RoutingEdge& routing_edge = routing_edge_map[x][y];
          if (routing_edge.get_overflow() <= 0) {
            continue;
          }
          PlanarCoord first_coord = RTUTIL.getRealRectByGCell(PlanarCoord(x, y), gcell_axis).getMidPoint();
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarCoord second_coord = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis).getMidPoint();
          GPPath gp_path;
          gp_path.set_data_type(static_cast<int32_t>(GPDataType::kOverflow));
          gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          gp_path.set_width(point_size);
          gp_path.set_segment(first_coord, second_coord);
          overflow_struct.push(gp_path);
        }
      }
    }
    gp_gds.addStruct(overflow_struct);
  }

  std::string gds_file_path = RTUTIL.getString(la_temp_directory_path, flag, "_la_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

}  // namespace irt
