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
#include "DataManager.hpp"

#include "AdjacentCutSpacingRule.hpp"
#include "IdbEnum.h"
#include "Monitor.hpp"
#include "ParallelRunLengthSpacingRule.hpp"
#include "SameLayerCutSpacingRule.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace idrc {

// public

void DataManager::initInst()
{
  if (_dm_instance == nullptr) {
    _dm_instance = new DataManager();
  }
}

DataManager& DataManager::getInst()
{
  if (_dm_instance == nullptr) {
    DRCLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dm_instance;
}

void DataManager::destroyInst()
{
  if (_dm_instance != nullptr) {
    delete _dm_instance;
    _dm_instance = nullptr;
  }
}

// function

void DataManager::input(std::map<std::string, std::any>& config_map)
{
  auto monitor = Monitor::create();
  DRCLOG.info(Loc::current(), "Starting...");
  wrapConfig(config_map);
  wrapDatabase();
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();
  DRCLOG.info(Loc::current(), "Completed", monitor ? monitor->getStatsInfo() : "");
}

#if 1  // 获得唯一的pitch

int32_t DataManager::getOnlyPitch()
{
  if (_only_pitch <= 0) {
    DRCLOG.error(Loc::current(), "The cluster reference pitch is not initialized!");
  }
  return _only_pitch;
}

#endif

const std::vector<int32_t>& DataManager::getAdjacentCutLayerIdxList(int32_t routing_layer_idx)
{
  const auto& adjacent_map = _database.get_routing_to_adjacent_cut_map();
  auto iter = adjacent_map.find(routing_layer_idx);
  if (iter == adjacent_map.end() || iter->second.empty()) {
    DRCLOG.error(Loc::current(), "The routing layer '", routing_layer_idx, "' has no adjacent cut layer!");
  }
  return iter->second;
}

const std::vector<int32_t>& DataManager::getAdjacentRoutingLayerIdxList(int32_t cut_layer_idx)
{
  const auto& adjacent_map = _database.get_cut_to_adjacent_routing_map();
  auto iter = adjacent_map.find(cut_layer_idx);
  if (iter == adjacent_map.end() || iter->second.empty()) {
    DRCLOG.error(Loc::current(), "The cut layer '", cut_layer_idx, "' has no adjacent routing layer!");
  }
  return iter->second;
}

// private

DataManager* DataManager::_dm_instance = nullptr;

void DataManager::wrapConfig(std::map<std::string, std::any>& config_map)
{
  /////////////////////////////////////////////
  _config.temp_directory_path = DRCUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./drc_temp_directory");
  _config.thread_number = DRCUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  omp_set_num_threads(std::max(_config.thread_number, 1));
  /////////////////////////////////////////////
}

void DataManager::wrapDatabase()
{
  wrapDBInfo();
  wrapMicronDBU();
  wrapManufactureGrid();
  wrapDie();
  wrapDesignRule();
  wrapLayerList();
  wrapLayerInfo();
}

void DataManager::wrapDBInfo()
{
  _database.set_design_name(dmInst->get_idb_def_service()->get_design()->get_design_name());
  _database.set_lef_file_path_list(dmInst->get_idb_lef_service()->get_lef_files());
  _database.set_def_file_path(dmInst->get_idb_def_service()->get_def_file());
}

void DataManager::wrapMicronDBU()
{
  _database.set_micron_dbu(dmInst->get_idb_def_service()->get_design()->get_units()->get_micron_dbu());
}

void DataManager::wrapManufactureGrid()
{
  _database.set_manufacture_grid(dmInst->get_idb_lef_service()->get_layout()->get_munufacture_grid());
}

void DataManager::wrapDie()
{
  idb::IdbDie* idb_die = dmInst->get_idb_lef_service()->get_layout()->get_die();

  Die& die = _database.get_die();
  die.set_ll(idb_die->get_llx(), idb_die->get_lly());
  die.set_ur(idb_die->get_urx(), idb_die->get_ury());
}

void DataManager::wrapDesignRule()
{
  std::set<ViolationType>& exist_rule_set = _database.get_exist_rule_set();

  // default
  {
    exist_rule_set.insert(ViolationType::kOutOfDie);
  }
  // MaxViaStackRule
  {
    MaxViaStackRule& max_via_stack_rule = _database.get_max_via_stack_rule();
    idb::IdbLayers* idb_layer_list = dmInst->get_idb_def_service()->get_layout()->get_layers();
    idb::IdbMaxViaStack* idb_max_via_stack = dmInst->get_idb_lef_service()->get_layout()->get_max_via_stack();
    if (idb_max_via_stack != nullptr) {
      max_via_stack_rule.max_via_stack_num = idb_max_via_stack->get_stacked_via_num();
      max_via_stack_rule.bottom_routing_layer_idx = idb_layer_list->find_layer(idb_max_via_stack->get_layer_bottom())->get_id();
      max_via_stack_rule.top_routing_layer_idx = idb_layer_list->find_layer(idb_max_via_stack->get_layer_top())->get_id();
      exist_rule_set.insert(ViolationType::kMaxViaStack);
    }
  }
  // OffGridOrWrongWayRule
  {
    OffGridOrWrongWayRule& off_grid_or_wrong_way_rule = _database.get_off_grid_or_wrong_way_rule();
    off_grid_or_wrong_way_rule.manufacture_grid = dmInst->get_idb_lef_service()->get_layout()->get_munufacture_grid();
    exist_rule_set.insert(ViolationType::kOffGridOrWrongWay);
  }
}

void DataManager::wrapLayerList()
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();

  std::vector<idb::IdbLayer*>& idb_layers = dmInst->get_idb_lef_service()->get_layout()->get_layers()->get_layers();
  for (idb::IdbLayer* idb_layer : idb_layers) {
    if (idb_layer->is_routing()) {
      idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
      RoutingLayer routing_layer;
      routing_layer.set_layer_idx(idb_routing_layer->get_id());
      routing_layer.set_layer_order(idb_routing_layer->get_order());
      routing_layer.set_layer_name(idb_routing_layer->get_name());
      routing_layer.set_prefer_direction(getDRCDirectionByDB(idb_routing_layer->get_direction()));
      wrapTrackAxis(routing_layer, idb_routing_layer);
      wrapRoutingDesignRule(routing_layer, idb_routing_layer);
      routing_layer_list.push_back(std::move(routing_layer));
    } else if (idb_layer->is_cut()) {
      idb::IdbLayerCut* idb_cut_layer = dynamic_cast<idb::IdbLayerCut*>(idb_layer);
      CutLayer cut_layer;
      cut_layer.set_layer_idx(idb_cut_layer->get_id());
      cut_layer.set_layer_order(idb_cut_layer->get_order());
      cut_layer.set_layer_name(idb_cut_layer->get_name());
      wrapCutDesignRule(cut_layer, idb_cut_layer);
      cut_layer_list.push_back(std::move(cut_layer));
    }
  }
}

void DataManager::wrapTrackAxis(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer)
{
  if (idb_layer->get_prefer_track_grid() != nullptr) {
    routing_layer.set_pitch(idb_layer->get_prefer_track_grid()->get_track()->get_pitch());
  }
}

void DataManager::wrapRoutingDesignRule(RoutingLayer& routing_layer, idb::IdbLayerRouting* idb_layer)
{
  std::set<ViolationType>& exist_rule_set = _database.get_exist_rule_set();

  // default
  {
    exist_rule_set.insert(ViolationType::kMetalShort);
  }
  // CornerFillSpacingRule
  {
    CornerFillSpacingRule& corner_fill_spacing_rule = routing_layer.get_corner_fill_spacing_rule();
    idb::routinglayer::Lef58CornerFillSpacing* idb_corner_fill = idb_layer->get_lef58_corner_fill_spacing().get();
    if (idb_corner_fill != nullptr) {
      corner_fill_spacing_rule.has_corner_fill = true;
      corner_fill_spacing_rule.corner_fill_spacing = idb_corner_fill->get_spacing();
      corner_fill_spacing_rule.edge_length_1 = idb_corner_fill->get_edge_length1();
      corner_fill_spacing_rule.edge_length_2 = idb_corner_fill->get_edge_length2();
      corner_fill_spacing_rule.adjacent_eol = idb_corner_fill->get_eol_width();
      exist_rule_set.insert(ViolationType::kCornerFillSpacing);
    }
  }
  // CornerSpacingRule
  {
    std::vector<CornerSpacingRule>& corner_spacing_rule_list = routing_layer.get_corner_spacing_rule_list();
    if (!idb_layer->get_lef58_corner_spacing_list().empty()) {
      for (const std::shared_ptr<idb::routinglayer::Lef58CornerSpacing>& idb_corner_spacing : idb_layer->get_lef58_corner_spacing_list()) {
        CornerSpacingRule corner_spacing_rule;
        corner_spacing_rule.has_convex_corner = idb_corner_spacing->get_corner_type() == idb::routinglayer::Lef58CornerSpacing::CornerType::kConvexCorner;
        corner_spacing_rule.has_concave_corner = idb_corner_spacing->get_corner_type() == idb::routinglayer::Lef58CornerSpacing::CornerType::kConcaveCorner;
        corner_spacing_rule.has_except_eol = idb_corner_spacing->get_except_eol().has_value();
        if (idb_corner_spacing->get_except_eol().has_value()) {
          corner_spacing_rule.except_eol = idb_corner_spacing->get_except_eol().value();
        }
        for (const auto& width_spacing : idb_corner_spacing->get_width_spacing_list()) {
          corner_spacing_rule.width_spacing_list.emplace_back(width_spacing.get_width(), width_spacing.get_spacing());
        }
        corner_spacing_rule_list.push_back(std::move(corner_spacing_rule));
      }
      exist_rule_set.insert(ViolationType::kCornerSpacing);
    }
  }
  // EndOfLineSpacingRule
  {
    std::vector<EndOfLineSpacingRule>& end_of_line_spacing_rule_list = routing_layer.get_end_of_line_spacing_rule_list();
    if (!idb_layer->get_lef58_spacing_eol_list().empty()) {
      for (std::shared_ptr<idb::routinglayer::Lef58SpacingEol> idb_spacing_eol : idb_layer->get_lef58_spacing_eol_list()) {
        EndOfLineSpacingRule end_of_line_spacing_rule;

        end_of_line_spacing_rule.eol_spacing = idb_spacing_eol.get()->get_eol_space();
        end_of_line_spacing_rule.eol_width = idb_spacing_eol.get()->get_eol_width();
        end_of_line_spacing_rule.eol_within = idb_spacing_eol.get()->get_eol_within().value();

        end_of_line_spacing_rule.has_ete = idb_spacing_eol.get()->get_end_to_end().has_value();
        if (idb_spacing_eol.get()->get_end_to_end().has_value()) {
          end_of_line_spacing_rule.ete_spacing = idb_spacing_eol.get()->get_end_to_end().value().get_end_to_end_space();
        }

        end_of_line_spacing_rule.has_par = idb_spacing_eol.get()->get_parallel_edge().has_value();
        if (idb_spacing_eol.get()->get_parallel_edge().has_value()) {
          end_of_line_spacing_rule.has_subtrace_eol_width = idb_spacing_eol.get()->get_parallel_edge().value().is_subtract_eol_width();
          end_of_line_spacing_rule.par_spacing = idb_spacing_eol.get()->get_parallel_edge().value().get_par_space();
          end_of_line_spacing_rule.par_within = idb_spacing_eol.get()->get_parallel_edge().value().get_par_within();
          end_of_line_spacing_rule.has_two_edges = idb_spacing_eol.get()->get_parallel_edge().value().is_two_edges();
          end_of_line_spacing_rule.has_min_length = idb_spacing_eol.get()->get_parallel_edge().value().get_min_length().has_value();
          if (idb_spacing_eol.get()->get_parallel_edge().value().get_min_length().has_value()) {
            end_of_line_spacing_rule.min_length = idb_spacing_eol.get()->get_parallel_edge().value().get_min_length().value();
          }
          end_of_line_spacing_rule.has_same_metal = idb_spacing_eol.get()->get_parallel_edge().value().is_same_metal();
        }

        end_of_line_spacing_rule.has_enclose_cut = idb_spacing_eol.get()->get_enclose_cut().has_value();
        if (idb_spacing_eol.get()->get_enclose_cut().has_value()) {
          end_of_line_spacing_rule.has_below
              = idb_spacing_eol.get()->get_enclose_cut().value().get_direction() == idb::routinglayer::Lef58SpacingEol::Direction::kBelow;
          end_of_line_spacing_rule.has_above
              = idb_spacing_eol.get()->get_enclose_cut().value().get_direction() == idb::routinglayer::Lef58SpacingEol::Direction::kAbove;
          end_of_line_spacing_rule.enclosed_dist = idb_spacing_eol.get()->get_enclose_cut().value().get_enclose_dist();
          end_of_line_spacing_rule.cut_to_metal_spacing = idb_spacing_eol.get()->get_enclose_cut().value().get_cut_to_metal_space();
          end_of_line_spacing_rule.has_all_cuts = idb_spacing_eol.get()->get_enclose_cut().value().is_all_cuts();
        }
        end_of_line_spacing_rule_list.push_back(end_of_line_spacing_rule);
      }
      exist_rule_set.insert(ViolationType::kEndOfLineSpacing);
    }

    if (idb_layer->get_spacing_list() != nullptr) {
      for (auto& spacing_rule : idb_layer->get_spacing_list()->get_spacing_list()) {
        EndOfLineSpacingRule end_of_line_spacing_rule;
        if (spacing_rule->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingEndOfLine) {
          end_of_line_spacing_rule.eol_spacing = spacing_rule->get_min_spacing();
          end_of_line_spacing_rule.eol_width = spacing_rule->get_eol_width();
          end_of_line_spacing_rule.eol_within = spacing_rule->get_eol_within();
          end_of_line_spacing_rule.has_par = spacing_rule->get_has_parallel_edge();
          end_of_line_spacing_rule.par_spacing = spacing_rule->get_par_space();
          end_of_line_spacing_rule.par_within = spacing_rule->get_par_within();
          end_of_line_spacing_rule.has_two_edges = spacing_rule->get_has_parallel_edge() && spacing_rule->get_has_two_edges();
          end_of_line_spacing_rule_list.push_back(end_of_line_spacing_rule);
          exist_rule_set.insert(ViolationType::kEndOfLineSpacing);
        }
      }
    }
  }
  // MaximumWidthRule
  {
    MaximumWidthRule& maximum_width_rule = routing_layer.get_maximum_width_rule();
    int32_t max_width = INT32_MAX;
    if (idb_layer->get_max_width() != -1) {
      max_width = idb_layer->get_max_width();
    }
    maximum_width_rule.max_width = max_width;
    exist_rule_set.insert(ViolationType::kMaximumWidth);
  }
  // MinHoleRule
  {
    MinHoleRule& min_hole_rule = routing_layer.get_min_hole_rule();
    std::vector<IdbMinEncloseArea>& min_area_list = idb_layer->get_min_enclose_area_list()->get_min_area_list();
    if (!min_area_list.empty()) {
      min_hole_rule.min_hole_area = min_area_list.front()._area;
      exist_rule_set.insert(ViolationType::kMinHole);
    }
  }
  // MinimumAreaRule
  {
    MinimumAreaRule& minimum_area_rule = routing_layer.get_minimum_area_rule();
    minimum_area_rule.min_area = idb_layer->get_area();
    exist_rule_set.insert(ViolationType::kMinimumArea);
  }
  // MinimumCutRule
  {
    std::vector<MinimumCutRule>& minimum_cut_rule_list = routing_layer.get_minimum_cut_rule_list();
    if (!idb_layer->get_lef58_minimum_cut().empty()) {
      for (std::shared_ptr<idb::routinglayer::Lef58MinimumCut>& idb_minimum_cut : idb_layer->get_lef58_minimum_cut()) {
        MinimumCutRule minimum_cut_rule;
        if (idb_minimum_cut.get()->get_num_cuts().has_value()) {
          minimum_cut_rule.num_cuts = idb_minimum_cut.get()->get_num_cuts().value();
        } else {
          for (const idb::routinglayer::Lef58MinimumCut::CutClass& idb_cut_class : idb_minimum_cut.get()->get_cut_classes()) {
            if (idb_cut_class.get_class_name() == "VSINGLECUT") {
              minimum_cut_rule.num_cuts = idb_cut_class.get_num_cuts();
              break;
            }
          }
        }
        minimum_cut_rule.width = idb_minimum_cut.get()->get_width();
        minimum_cut_rule.has_within_cut_distance = idb_minimum_cut.get()->get_within_cut_distance().has_value();
        if (idb_minimum_cut.get()->get_within_cut_distance().has_value()) {
          minimum_cut_rule.within_cut_distance = idb_minimum_cut.get()->get_within_cut_distance().value();
        }
        minimum_cut_rule.has_from_above = idb_minimum_cut.get()->get_orient() == idb::routinglayer::Lef58MinimumCut::Orient::kFromAbove ? true : false;
        minimum_cut_rule.has_from_below = idb_minimum_cut.get()->get_orient() == idb::routinglayer::Lef58MinimumCut::Orient::kFromBelow ? true : false;
        minimum_cut_rule.has_length = idb_minimum_cut.get()->get_length().has_value();
        if (idb_minimum_cut.get()->get_length().has_value()) {
          minimum_cut_rule.length = idb_minimum_cut.get()->get_length().value().get_length();
          minimum_cut_rule.distance = idb_minimum_cut.get()->get_length().value().get_distance();
        }
        minimum_cut_rule_list.push_back(minimum_cut_rule);
      }
      exist_rule_set.insert(ViolationType::kMinimumCut);
    }
  }
  // MinimumWidthRule
  {
    MinimumWidthRule& minimum_width_rule = routing_layer.get_minimum_width_rule();
    minimum_width_rule.min_width = idb_layer->get_min_width();
    exist_rule_set.insert(ViolationType::kMinimumWidth);
  }
  // MinStepRule
  {
    MinStepRule& min_step_rule = routing_layer.get_min_step_rule();
    idb::IdbMinStep* idb_min_step = idb_layer->get_min_step().get();
    std::vector<std::shared_ptr<idb::routinglayer::Lef58MinStep>>& idb_lef58_min_step_list = idb_layer->get_lef58_min_step();
    if (idb_min_step != nullptr && !idb_lef58_min_step_list.empty()) {
      min_step_rule.min_step = idb_min_step->get_min_step_length();
      min_step_rule.max_edges = idb_min_step->get_max_edges();
      for (std::shared_ptr<idb::routinglayer::Lef58MinStep>& idb_lef58_min_step : idb_layer->get_lef58_min_step()) {
        min_step_rule.lef58_min_step = idb_lef58_min_step.get()->get_min_step_length();
        min_step_rule.lef58_min_adjacent_length = idb_lef58_min_step.get()->get_min_adjacent_length().value().get_min_adj_length();
        break;
      }
      exist_rule_set.insert(ViolationType::kMinStep);
    }
  }
  // NonsufficientMetalOverlapRule
  {
    NonsufficientMetalOverlapRule& nonsufficient_metal_overlap_rule = routing_layer.get_nonsufficient_metal_overlap_rule();
    nonsufficient_metal_overlap_rule.min_width = idb_layer->get_min_width();
    exist_rule_set.insert(ViolationType::kNonsufficientMetalOverlap);
  }
  // NotchSpacingRule
  {
    NotchSpacingRule& notch_spacing_rule = routing_layer.get_notch_spacing_rule();
    IdbLayerSpacingNotchLength& idb_notch = idb_layer->get_spacing_notchlength();
    idb::routinglayer::Lef58SpacingNotchlength* idb_lef58_notch = idb_layer->get_lef58_spacing_notchlength().get();
    if (idb_notch.exist()) {
      notch_spacing_rule.notch_spacing = idb_notch.get_min_spacing();
      notch_spacing_rule.notch_length = idb_notch.get_notch_length();
      exist_rule_set.insert(ViolationType::kNotchSpacing);
    } else if (idb_lef58_notch != nullptr) {
      notch_spacing_rule.notch_spacing = idb_lef58_notch->get_min_spacing();
      notch_spacing_rule.notch_length = idb_lef58_notch->get_min_notch_length();
      notch_spacing_rule.concave_ends = idb_lef58_notch->get_concave_ends_side_of_notch_width();
      exist_rule_set.insert(ViolationType::kNotchSpacing);
    }
  }
  // ParallelRunLengthSpacingRule
  {
    ParallelRunLengthSpacingRule& parallel_run_length_spacing_rule = routing_layer.get_parallel_run_length_spacing_rule();
    std::shared_ptr<idb::IdbParallelSpacingTable> idb_spacing_table;
    if (idb_layer->get_spacing_table().get()->get_parallel().get() != nullptr && idb_layer->get_spacing_table().get()->is_parallel()) {
      idb_spacing_table = idb_layer->get_spacing_table()->get_parallel();
      std::vector<int32_t>& width_list = parallel_run_length_spacing_rule.width_list;
      std::vector<int32_t>& parallel_length_list = parallel_run_length_spacing_rule.parallel_length_list;
      GridMap<int32_t>& width_parallel_length_map = parallel_run_length_spacing_rule.width_parallel_length_map;

      width_list = idb_spacing_table->get_width_list();
      parallel_length_list = idb_spacing_table->get_parallel_length_list();
      width_parallel_length_map.init(width_list.size(), parallel_length_list.size());
      for (int32_t x = 0; x < width_parallel_length_map.get_x_size(); x++) {
        for (int32_t y = 0; y < width_parallel_length_map.get_y_size(); y++) {
          width_parallel_length_map[x][y] = idb_spacing_table->get_spacing_table()[x][y];
        }
      }
      parallel_run_length_spacing_rule.has_spacing_table = true;
      exist_rule_set.insert(ViolationType::kParallelRunLengthSpacing);
    }

    if (idb_layer->get_spacing_list() != nullptr) {
      auto& spacing_list = parallel_run_length_spacing_rule.spacing_list;
      for (auto& spacing_rule : idb_layer->get_spacing_list()->get_spacing_list()) {
        LayerSpacingType spacing_type;
        if (spacing_rule->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingDefault) {
          spacing_type = LayerSpacingType::kSpacingDefault;
        } else if (spacing_rule->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingRange) {
          spacing_type = LayerSpacingType::kSpacingRange;
        } else if (spacing_rule->get_spacing_type() == idb::IdbLayerSpacingType::kSpacingEndOfLine) {
          continue;
        } else {
          spacing_type = LayerSpacingType::kNone;
        }
        LayerSpacing spacing{spacing_type, spacing_rule->get_min_spacing(), spacing_rule->get_min_width(), spacing_rule->get_max_width()};
        spacing_list.push_back(spacing);
      }

      parallel_run_length_spacing_rule.has_spacing_list = true;
      exist_rule_set.insert(ViolationType::kParallelRunLengthSpacing);
    }
  }
}

void DataManager::wrapCutDesignRule(CutLayer& cut_layer, idb::IdbLayerCut* idb_layer)
{
  std::set<ViolationType>& exist_rule_set = _database.get_exist_rule_set();

  // default
  {
    exist_rule_set.insert(ViolationType::kCutShort);
  }
  // AdjacentCutSpacingRule
  {
    AdjacentCutSpacingRule& adj_cut_spacing_rule = cut_layer.get_adjacent_cut_rule();
    if (!idb_layer->get_spacings().empty()) {
      for (auto& cut_spacing : idb_layer->get_spacings()) {
        if (cut_spacing->get_adjacent_cuts().has_value()) {
          adj_cut_spacing_rule.has_rule = true;
          adj_cut_spacing_rule.cut_spacing = cut_spacing->get_spacing();
          adj_cut_spacing_rule.adjacnet_cuts = cut_spacing->get_adjacent_cuts()->get_adjacent_cuts();
          adj_cut_spacing_rule.cut_within = cut_spacing->get_adjacent_cuts()->get_cut_within();
          exist_rule_set.insert(ViolationType::kAdjacentCutSpacing);
          // only one ADJACENTCUTS statement per cut layer
          continue;
        }
      }
    }
  }
  // CutEOLSpacingRule
  {
    CutEOLSpacingRule& cut_eol_spacing_rule = cut_layer.get_cut_eol_spacing_rule();
    if (idb_layer->get_lef58_eol_spacing().get() != nullptr) {
      idb::cutlayer::Lef58EolSpacing* idb_eol_spacing = idb_layer->get_lef58_eol_spacing().get();
      cut_eol_spacing_rule.eol_spacing = idb_eol_spacing->get_cut_spacing1();
      cut_eol_spacing_rule.eol_prl = idb_eol_spacing->get_prl();
      cut_eol_spacing_rule.eol_prl_spacing = idb_eol_spacing->get_cut_spacing2();
      cut_eol_spacing_rule.eol_width = idb_eol_spacing->get_eol_width();
      cut_eol_spacing_rule.smaller_overhang = idb_eol_spacing->get_smaller_overhang();
      cut_eol_spacing_rule.equal_overhang = idb_eol_spacing->get_equal_overhang();
      cut_eol_spacing_rule.side_ext = idb_eol_spacing->get_side_ext();
      cut_eol_spacing_rule.backward_ext = idb_eol_spacing->get_backward_ext();
      cut_eol_spacing_rule.span_length = idb_eol_spacing->get_span_length();
      exist_rule_set.insert(ViolationType::kCutEOLSpacing);
    }
  }
  // DifferentLayerCutSpacingRule
  {
    DifferentLayerCutSpacingRule& different_layer_cut_spacing_rule = cut_layer.get_different_layer_cut_spacing_rule();
    if (!idb_layer->get_lef58_spacing_table().empty()) {
      idb::cutlayer::Lef58SpacingTable* spacing_table = nullptr;
      for (std::shared_ptr<idb::cutlayer::Lef58SpacingTable>& spacing_table_ptr : idb_layer->get_lef58_spacing_table()) {
        if (!spacing_table_ptr.get()->get_second_layer().has_value()) {
          continue;
        }
        spacing_table = spacing_table_ptr.get();
      }
      if (spacing_table != nullptr) {
        idb::cutlayer::Lef58SpacingTable::CutSpacing cut_spacing = spacing_table->get_cutclass().get_cut_spacing(0, 0);

        int32_t below_spacing = cut_spacing.get_cut_spacing1().value();
        int32_t below_prl = spacing_table->get_prl().value().get_prl();
        int32_t below_prl_spacing = cut_spacing.get_cut_spacing2().value();
        different_layer_cut_spacing_rule.below_spacing = below_spacing;
        different_layer_cut_spacing_rule.below_prl = below_prl;
        different_layer_cut_spacing_rule.below_prl_spacing = below_prl_spacing;
        exist_rule_set.insert(ViolationType::kDifferentLayerCutSpacing);
      }
    }
  }
  // EnclosureEdgeRule
  {
    std::vector<EnclosureEdgeRule>& enclosure_edge_rule_list = cut_layer.get_enclosure_edge_rule_list();
    if (!idb_layer->get_lef58_enclosure_edge_list().empty()) {
      for (std::shared_ptr<idb::cutlayer::Lef58EnclosureEdge>& idb_enclosure_edge : idb_layer->get_lef58_enclosure_edge_list()) {
        EnclosureEdgeRule enclosure_edge_rule;
        if (idb_enclosure_edge.get()->get_convex_corners().has_value()) {
          auto convex_corner = idb_enclosure_edge->get_convex_corners().value();
          enclosure_edge_rule.has_convexcorners = true;
          enclosure_edge_rule.convex_length = convex_corner.get_convex_length();
          enclosure_edge_rule.adjacent_length = convex_corner.get_adjacent_length();
          enclosure_edge_rule.convex_par_within = convex_corner.get_par_within();
          enclosure_edge_rule.length = convex_corner.get_length();
          enclosure_edge_rule.has_above = (idb_enclosure_edge.get()->get_direction() == idb::cutlayer::Lef58EnclosureEdge::Direction::kAbove);
          enclosure_edge_rule.has_below = (idb_enclosure_edge.get()->get_direction() == idb::cutlayer::Lef58EnclosureEdge::Direction::kBelow);
          enclosure_edge_rule.overhang = idb_enclosure_edge.get()->get_overhang();
          enclosure_edge_rule_list.push_back(enclosure_edge_rule);
          continue;
        }
        enclosure_edge_rule.has_above = (idb_enclosure_edge.get()->get_direction() == idb::cutlayer::Lef58EnclosureEdge::Direction::kAbove);
        enclosure_edge_rule.has_below = (idb_enclosure_edge.get()->get_direction() == idb::cutlayer::Lef58EnclosureEdge::Direction::kBelow);
        enclosure_edge_rule.overhang = idb_enclosure_edge.get()->get_overhang();
        enclosure_edge_rule.min_width = idb_enclosure_edge.get()->get_min_width().value();
        enclosure_edge_rule.par_length = idb_enclosure_edge.get()->get_par_length().value();
        enclosure_edge_rule.par_within = idb_enclosure_edge.get()->get_par_within().value();
        enclosure_edge_rule.has_except_two_edges = idb_enclosure_edge.get()->has_except_twoedges();
        enclosure_edge_rule_list.push_back(enclosure_edge_rule);
      }
      exist_rule_set.insert(ViolationType::kEnclosureEdge);
    }
  }
  // EnclosureParallelRule
  {
    EnclosureParallelRule& enclosure_parallel_rule = cut_layer.get_enclosure_parallel_rule();
    if (idb_layer->get_lef58_eol_enclosure().get() != nullptr) {
      idb::cutlayer::Lef58EolEnclosure* idb_eol_enclosure = idb_layer->get_lef58_eol_enclosure().get();
      enclosure_parallel_rule.eol_width = idb_eol_enclosure->get_eol_width();
      enclosure_parallel_rule.has_above = (idb_eol_enclosure->get_direction() == idb::cutlayer::Lef58EolEnclosure::Direction::kAbove);
      enclosure_parallel_rule.has_below = (idb_eol_enclosure->get_direction() == idb::cutlayer::Lef58EolEnclosure::Direction::kBelow);
      enclosure_parallel_rule.overhang = idb_eol_enclosure->get_overhang();
      if (idb_eol_enclosure->get_par_space().has_value()) {
        enclosure_parallel_rule.par_spacing = idb_eol_enclosure->get_par_space().value();
      }
      if (idb_eol_enclosure->get_extension().has_value()) {
        enclosure_parallel_rule.backward_ext = idb_eol_enclosure->get_extension().value().get_backward_ext();
        enclosure_parallel_rule.forward_ext = idb_eol_enclosure->get_extension().value().get_forward_ext();
      }
      enclosure_parallel_rule.has_min_length = idb_eol_enclosure->get_min_length().has_value();
      if (idb_eol_enclosure->get_min_length().has_value()) {
        enclosure_parallel_rule.min_length = idb_eol_enclosure->get_min_length().value();
      }
      exist_rule_set.insert(ViolationType::kEnclosureParallel);
    }
  }
  // SameLayerCutSpacingRule
  {
    SameLayerCutSpacingRule& same_layer_cut_spacing_rule = cut_layer.get_same_layer_cut_spacing_rule();
    if (!idb_layer->get_spacings().empty()) {
      for (auto& cut_spacing : idb_layer->get_spacings()) {
        if (cut_spacing->get_adjacent_cuts().has_value()) {
          continue;
        }
        SameLayerCutSpacing same_layer_cut_spacing;
        same_layer_cut_spacing.curr_spacing = cut_spacing->get_spacing();
        same_layer_cut_spacing.curr_prl = -1;
        same_layer_cut_spacing.curr_prl_spacing = -1;
        same_layer_cut_spacing.has_same_net = cut_spacing->get_has_same_net();
        same_layer_cut_spacing_rule.spacings.push_back(same_layer_cut_spacing);
      }
      exist_rule_set.insert(ViolationType::kSameLayerCutSpacing);
    } else if (!idb_layer->get_lef58_spacing_table().empty()) {
      idb::cutlayer::Lef58SpacingTable* spacing_table = nullptr;
      for (std::shared_ptr<idb::cutlayer::Lef58SpacingTable>& spacing_table_ptr : idb_layer->get_lef58_spacing_table()) {
        if (spacing_table_ptr.get()->get_second_layer().has_value()) {
          continue;
        }
        spacing_table = spacing_table_ptr.get();
      }
      if (spacing_table != nullptr) {
        // NEXT 是否需要支持全部的规则，而不是第一条？
        idb::cutlayer::Lef58SpacingTable::CutSpacing cut_spacing = spacing_table->get_cutclass().get_cut_spacing(0, 0);

        int32_t curr_spacing = cut_spacing.get_cut_spacing1().value();
        int32_t curr_prl = spacing_table->get_prl().value().get_prl();
        int32_t curr_prl_spacing = cut_spacing.get_cut_spacing2().value();
        same_layer_cut_spacing_rule.spacings.push_back({curr_spacing, curr_prl, curr_prl_spacing, false});
        exist_rule_set.insert(ViolationType::kSameLayerCutSpacing);
      }
    }
  }
}

void DataManager::wrapLayerInfo()
{
  std::map<int32_t, int32_t>& routing_idb_layer_id_to_idx_map = _database.get_routing_idb_layer_id_to_idx_map();
  std::map<int32_t, int32_t>& cut_idb_layer_id_to_idx_map = _database.get_cut_idb_layer_id_to_idx_map();
  std::map<std::string, int32_t>& routing_layer_name_to_idx_map = _database.get_routing_layer_name_to_idx_map();
  std::map<std::string, int32_t>& cut_layer_name_to_idx_map = _database.get_cut_layer_name_to_idx_map();
  routing_idb_layer_id_to_idx_map.clear();
  cut_idb_layer_id_to_idx_map.clear();
  routing_layer_name_to_idx_map.clear();
  cut_layer_name_to_idx_map.clear();

  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  for (size_t i = 0; i < routing_layer_list.size(); i++) {
    if (!routing_idb_layer_id_to_idx_map.emplace(routing_layer_list[i].get_layer_idx(), static_cast<int32_t>(i)).second
        || !routing_layer_name_to_idx_map.emplace(routing_layer_list[i].get_layer_name(), static_cast<int32_t>(i)).second) {
      DRCLOG.error(Loc::current(), "The routing layer id or name is duplicated!");
    }
  }
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();
  for (size_t i = 0; i < cut_layer_list.size(); i++) {
    if (!cut_idb_layer_id_to_idx_map.emplace(cut_layer_list[i].get_layer_idx(), static_cast<int32_t>(i)).second
        || !cut_layer_name_to_idx_map.emplace(cut_layer_list[i].get_layer_name(), static_cast<int32_t>(i)).second) {
      DRCLOG.error(Loc::current(), "The cut layer id or name is duplicated!");
    }
  }
}

Direction DataManager::getDRCDirectionByDB(idb::IdbLayerDirection idb_direction)
{
  if (idb_direction == idb::IdbLayerDirection::kHorizontal) {
    return Direction::kHorizontal;
  } else if (idb_direction == idb::IdbLayerDirection::kVertical) {
    return Direction::kVertical;
  } else {
    return Direction::kOblique;
  }
}

namespace {

int32_t getLayerIdx(const std::map<int32_t, int32_t>& idb_layer_id_to_idx_map, int32_t idb_layer_id, const char* layer_type);

}  // namespace

#if 1  // build

void DataManager::buildConfig()
{
  //////////////////////////////////////////////
  // **********        DRC         ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "drc.log";
  // **********     RuleValidator  ********** //
  _config.rv_temp_directory_path = _config.temp_directory_path + "rule_validator/";
  // **********     GDSPlotter     ********** //
  _config.gp_temp_directory_path = _config.temp_directory_path + "gds_plotter/";
  /////////////////////////////////////////////
  // **********        DRC         ********** //
  DRCUTIL.removeDir(_config.temp_directory_path);
  DRCUTIL.createDir(_config.temp_directory_path);
  DRCUTIL.createDirByFile(_config.log_file_path);
  // **********   RuleValidator    ********** //
  DRCUTIL.createDir(_config.rv_temp_directory_path);
  // **********     GDSPlotter     ********** //
  DRCUTIL.createDir(_config.gp_temp_directory_path);
  //////////////////////////////////////////////
  DRCLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  buildDie();
  buildDesignRule();
  buildLayerList();
  buildLayerInfo();
}

void DataManager::buildDie()
{
  checkDie();
}

void DataManager::checkDie()
{
  Die& die = _database.get_die();

  if (die.get_ll_x() < 0 || die.get_ll_y() < 0 || die.get_ur_x() < 0 || die.get_ur_y() < 0) {
    DRCLOG.error(Loc::current(), "The die '(", die.get_ll_x(), " , ", die.get_ll_y(), ") - (", die.get_ur_x(), " , ", die.get_ur_y(), ")' is wrong!");
  }
  if ((die.get_ur_x() <= die.get_ll_x()) || (die.get_ur_y() <= die.get_ll_y())) {
    DRCLOG.error(Loc::current(), "The die '(", die.get_ll_x(), " , ", die.get_ll_y(), ") - (", die.get_ur_x(), " , ", die.get_ur_y(), ")' is wrong!");
  }
}

void DataManager::buildDesignRule()
{
  if (!DRCUTIL.exist(_database.get_exist_rule_set(), ViolationType::kMaxViaStack)) {
    return;
  }
  std::map<int32_t, int32_t>& routing_idb_layer_id_to_idx_map = _database.get_routing_idb_layer_id_to_idx_map();
  MaxViaStackRule& max_via_stack_rule = _database.get_max_via_stack_rule();
  max_via_stack_rule.bottom_routing_layer_idx
      = getLayerIdx(routing_idb_layer_id_to_idx_map, max_via_stack_rule.bottom_routing_layer_idx, "routing");
  max_via_stack_rule.top_routing_layer_idx
      = getLayerIdx(routing_idb_layer_id_to_idx_map, max_via_stack_rule.top_routing_layer_idx, "routing");
}

void DataManager::buildLayerList()
{
  transLayerList();
  checkLayerList();
  makeLayerList();
}

void DataManager::transLayerList()
{
  std::map<int32_t, int32_t>& routing_idb_layer_id_to_idx_map = _database.get_routing_idb_layer_id_to_idx_map();
  std::map<int32_t, int32_t>& cut_idb_layer_id_to_idx_map = _database.get_cut_idb_layer_id_to_idx_map();

  for (RoutingLayer& routing_layer : _database.get_routing_layer_list()) {
    routing_layer.set_layer_idx(getLayerIdx(routing_idb_layer_id_to_idx_map, routing_layer.get_layer_idx(), "routing"));
  }
  for (CutLayer& cut_layer : _database.get_cut_layer_list()) {
    cut_layer.set_layer_idx(getLayerIdx(cut_idb_layer_id_to_idx_map, cut_layer.get_layer_idx(), "cut"));
  }
}

void DataManager::makeLayerList()
{
  makeRoutingLayerList();
  makeCutLayerList();
}

void DataManager::makeRoutingLayerList()
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  // Cluster sizing uses the most common pitch without changing per-layer technology data.
  std::map<int32_t, int32_t> pitch_count_map;
  for (RoutingLayer& routing_layer : routing_layer_list) {
    pitch_count_map[routing_layer.get_pitch()]++;
  }
  int32_t max_count = -1;
  for (const auto& [pitch, count] : pitch_count_map) {
    if (count > max_count) {
      _only_pitch = pitch;
      max_count = count;
    }
  }
}

void DataManager::makeCutLayerList()
{
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();

  for (size_t i = 1; i < cut_layer_list.size(); i++) {
    DifferentLayerCutSpacingRule& pre_different_layer_cut_spacing_rule = cut_layer_list[i - 1].get_different_layer_cut_spacing_rule();
    DifferentLayerCutSpacingRule& curr_different_layer_cut_spacing_rule = cut_layer_list[i].get_different_layer_cut_spacing_rule();
    pre_different_layer_cut_spacing_rule.above_spacing = curr_different_layer_cut_spacing_rule.below_spacing;
    pre_different_layer_cut_spacing_rule.above_prl = curr_different_layer_cut_spacing_rule.below_prl;
    pre_different_layer_cut_spacing_rule.above_prl_spacing = curr_different_layer_cut_spacing_rule.below_prl_spacing;
  }
  cut_layer_list.back().get_different_layer_cut_spacing_rule().above_spacing = 0;
  cut_layer_list.back().get_different_layer_cut_spacing_rule().above_prl = 0;
  cut_layer_list.back().get_different_layer_cut_spacing_rule().above_prl_spacing = 0;
}

void DataManager::checkLayerList()
{
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();

  if (routing_layer_list.empty()) {
    DRCLOG.error(Loc::current(), "The routing_layer_list is empty!");
  }
  if (cut_layer_list.empty()) {
    DRCLOG.error(Loc::current(), "The cut_layer_list is empty!");
  }
  for (RoutingLayer& routing_layer : routing_layer_list) {
    std::string& layer_name = routing_layer.get_layer_name();
    if (routing_layer.get_prefer_direction() == Direction::kNone) {
      DRCLOG.error(Loc::current(), "The layer '", layer_name, "' prefer_direction is none!");
    }
    if (routing_layer.get_pitch() <= 0) {
      DRCLOG.error(Loc::current(), "The layer '", layer_name, "' pitch '", routing_layer.get_pitch(), "' is wrong!");
    }
  }
}

void DataManager::buildLayerInfo()
{
  std::map<int32_t, std::vector<int32_t>>& routing_to_adjacent_cut_map = _database.get_routing_to_adjacent_cut_map();
  std::map<int32_t, std::vector<int32_t>>& cut_to_adjacent_routing_map = _database.get_cut_to_adjacent_routing_map();

  routing_to_adjacent_cut_map.clear();
  cut_to_adjacent_routing_map.clear();

  std::vector<std::tuple<int32_t, bool, int32_t>> ordered_layer_list;
  for (RoutingLayer& routing_layer : _database.get_routing_layer_list()) {
    ordered_layer_list.emplace_back(routing_layer.get_layer_order(), true, routing_layer.get_layer_idx());
  }
  for (CutLayer& cut_layer : _database.get_cut_layer_list()) {
    ordered_layer_list.emplace_back(cut_layer.get_layer_order(), false, cut_layer.get_layer_idx());
  }
  std::sort(ordered_layer_list.begin(), ordered_layer_list.end(),
            [](const std::tuple<int32_t, bool, int32_t>& a, const std::tuple<int32_t, bool, int32_t>& b) {
              return std::get<0>(a) < std::get<0>(b);
            });
  for (int32_t i = 0; i < static_cast<int32_t>(ordered_layer_list.size()); i++) {
    bool is_routing = std::get<1>(ordered_layer_list[i]);
    int32_t layer_idx = std::get<2>(ordered_layer_list[i]);
    std::vector<int32_t>& adjacent_layer_idx_list
        = is_routing ? routing_to_adjacent_cut_map[layer_idx] : cut_to_adjacent_routing_map[layer_idx];
    if (i > 0 && std::get<1>(ordered_layer_list[i - 1]) != is_routing) {
      adjacent_layer_idx_list.push_back(std::get<2>(ordered_layer_list[i - 1]));
    }
    if (i + 1 < static_cast<int32_t>(ordered_layer_list.size()) && std::get<1>(ordered_layer_list[i + 1]) != is_routing) {
      adjacent_layer_idx_list.push_back(std::get<2>(ordered_layer_list[i + 1]));
    }
    if (adjacent_layer_idx_list.empty()) {
      if (is_routing) {
        DRCLOG.error(Loc::current(), "The routing layer '", layer_idx, "' has no adjacent cut layer!");
      } else {
        DRCLOG.error(Loc::current(), "The cut layer '", layer_idx, "' has no adjacent routing layer!");
      }
    }
  }
}

void DataManager::printConfig()
{
  /////////////////////////////////////////////
  // **********        DRC         ********** //
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(0), "DRC_CONFIG_INPUT");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "temp_directory_path");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "thread_number");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), _config.thread_number);
  // **********        DRC         ********** //
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(0), "DRC_CONFIG_BUILD");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "log_file_path");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), _config.log_file_path);
  // **********     DRCEngine     ********** //
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "RuleValidator");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), "rv_temp_directory_path");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(3), _config.rv_temp_directory_path);
  // **********     GDSPlotter     ********** //
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "GDSPlotter");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), "gp_temp_directory_path");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(3), _config.gp_temp_directory_path);
  /////////////////////////////////////////////
}

void DataManager::printDatabase()
{
  ////////////////////////////////////////////////
  // ********** DRC ********** //
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(0), "DRC_DATABASE");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "design_name");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), _database.get_design_name());
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "lef_file_path_list");
  for (std::string& lef_file_path : _database.get_lef_file_path_list()) {
    DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), lef_file_path);
  }
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "def_file_path");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), _database.get_def_file_path());
  // **********     MicronDBU     ********** //
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "micron_dbu");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), _database.get_micron_dbu());
  // **********  ManufactureGrid  ********** //
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "manufacture_grid");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), _database.get_manufacture_grid());
  // **********        Die        ********** //
  Die& die = _database.get_die();
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "die");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), "(", die.get_ll_x(), ",", die.get_ll_y(), ")-(", die.get_ur_x(), ",", die.get_ur_y(), ")");
  // ********** RoutingLayer ********** //
  std::vector<RoutingLayer>& routing_layer_list = _database.get_routing_layer_list();
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "routing_layer_num");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), routing_layer_list.size());
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "routing_layer");
  for (RoutingLayer& routing_layer : routing_layer_list) {
    DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), "idx:", routing_layer.get_layer_idx(), " order:", routing_layer.get_layer_order(),
                " name:", routing_layer.get_layer_name(), " prefer_direction:", GetDirectionName()(routing_layer.get_prefer_direction()),
                " pitch:", routing_layer.get_pitch());
  }
  // ********** CutLayer ********** //
  std::vector<CutLayer>& cut_layer_list = _database.get_cut_layer_list();
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "cut_layer_num");
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), cut_layer_list.size());
  DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(1), "cut_layer");
  for (CutLayer& cut_layer : cut_layer_list) {
    DRCLOG.info(Loc::current(), DRCUTIL.getSpaceByTabNum(2), "idx:", cut_layer.get_layer_idx(), " order:", cut_layer.get_layer_order(),
                " name:", cut_layer.get_layer_name());
  }
  ////////////////////////////////////////////////
}

#endif

namespace {

int32_t getLayerIdx(const std::map<int32_t, int32_t>& idb_layer_id_to_idx_map, int32_t idb_layer_id, const char* layer_type)
{
  auto iter = idb_layer_id_to_idx_map.find(idb_layer_id);
  if (iter == idb_layer_id_to_idx_map.end()) {
    DRCLOG.error(Loc::current(), "The ", layer_type, " layer id '", idb_layer_id, "' is not mapped!");
  }
  return iter->second;
}

}  // namespace

}  // namespace idrc
