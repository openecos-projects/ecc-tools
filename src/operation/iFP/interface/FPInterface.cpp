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
#include "FPInterface.hpp"

#include "DataManager.hpp"
#include "DieBuilder.hpp"
#include "IOPlacer.hpp"
#include "IdbTerm.h"
#include "IdbViaMaster.h"
#include "IdbVias.h"
#include "Logger.hpp"
#include "MacroPlacer.hpp"
#include "Monitor.hpp"
#include "PDNGenerator.hpp"
#include "PhyPlacer.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace ifp {

FPInterface* FPInterface::_fp_interface_instance = nullptr;

// public

FPInterface& FPInterface::getInst()
{
  if (_fp_interface_instance == nullptr) {
    _fp_interface_instance = new FPInterface();
  }
  return *_fp_interface_instance;
}

void FPInterface::destroyInst()
{
  if (_fp_interface_instance != nullptr) {
    delete _fp_interface_instance;
    _fp_interface_instance = nullptr;
  }
}

#if 1  // 外部调用FP的API

#if 1  // iFP

void FPInterface::initFP(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  FPLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  FPLOG.info(Loc::current(), "______________________     _____________________________________  ");
  FPLOG.info(Loc::current(), "___(_)__  ____/__  __ \\    __  ___/__  __/__    |__  __ \\__  __/");
  FPLOG.info(Loc::current(), "__  /__  /_   __  /_/ /    _____ \\__  /  __  /| |_  /_/ /_  /    ");
  FPLOG.info(Loc::current(), "_  / _  __/   _  ____/     ____/ /_  /   _  ___ |  _, _/_  /      ");
  FPLOG.info(Loc::current(), "/_/  /_/      /_/          /____/ /_/    /_/  |_/_/ |_| /_/       ");
  FPLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  FPLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  FPDM.input(config_map);

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void FPInterface::runFP()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  DieBuilder::initInst();
  FPDB.build();
  DieBuilder::destroyInst();

  IOPlacer::initInst();
  FPIP.place();
  IOPlacer::destroyInst();

  MacroPlacer::initInst();
  FPMP.place();
  MacroPlacer::destroyInst();

  PDNGenerator::initInst();
  FPPG.generate();
  PDNGenerator::destroyInst();

  PhyPlacer::initInst();
  FPPP.place();
  PhyPlacer::destroyInst();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void FPInterface::destroyFP()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  FPDM.output();
  DataManager::destroyInst();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  FPLOG.printLogFilePath();
  // clang-format off
  FPLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  FPLOG.info(Loc::current(), "______________________     _____________________   _____________________  __ ");
  FPLOG.info(Loc::current(), "___(_)__  ____/__  __ \\    ___  ____/___  _/__  | / /___  _/_  ___/__  / / /");
  FPLOG.info(Loc::current(), "__  /__  /_   __  /_/ /    __  /_    __  / __   |/ / __  / _____ \\__  /_/ / ");
  FPLOG.info(Loc::current(), "_  / _  __/   _  ____/     _  __/   __/ /  _  /|  / __/ /  ____/ /_  __  /   ");
  FPLOG.info(Loc::current(), "/_/  /_/      /_/          /_/      /___/  /_/ |_/  /___/  /____/ /_/ /_/    ");
  FPLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#if 1  // debug iFP

void FPInterface::debugInputMacro(std::map<std::string, std::any> config_map)
{
  std::string macro_place_file_path = FPUTIL.getConfigValue<std::string>(config_map, "-path", "");
  if (macro_place_file_path.empty()) {
    FPLOG.error(Loc::current(), "The macro placement file path is empty!");
    return;
  }

  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_design == nullptr || idb_layout == nullptr) {
    FPLOG.error(Loc::current(), "Failed to get the IDB design or layout!");
    return;
  }

  int32_t micron_dbu = idb_design->get_units() == nullptr ? 0 : idb_design->get_units()->get_micron_dbu();
  if (micron_dbu <= 0 && idb_layout->get_units() != nullptr) {
    micron_dbu = idb_layout->get_units()->get_micron_dbu();
  }
  if (micron_dbu <= 0) {
    FPLOG.error(Loc::current(), "Failed to get a valid micron DBU from IDB!");
    return;
  }

  std::ifstream macro_place_file(macro_place_file_path);
  if (!macro_place_file.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open macro placement file '", macro_place_file_path, "'!");
    return;
  }

  int32_t placed_macro_num = 0;
  int32_t skipped_macro_num = 0;
  int32_t line_num = 0;
  std::string line;
  while (std::getline(macro_place_file, line)) {
    ++line_num;
    if (line.empty() || line[0] == '#') {
      continue;
    }

    std::istringstream line_stream(line);
    std::string instance_name;
    double x_micron = 0.0;
    double y_micron = 0.0;
    std::string orient_name;
    if (!(line_stream >> instance_name >> x_micron >> y_micron >> orient_name)) {
      FPLOG.warn(Loc::current(), "Skip malformed macro placement at line ", line_num, " in '", macro_place_file_path, "'.");
      ++skipped_macro_num;
      continue;
    }

    idb::IdbInstance* idb_instance = idb_design->get_instance_list()->find_instance(instance_name);
    if (idb_instance == nullptr) {
      FPLOG.warn(Loc::current(), "Skip unknown macro '", instance_name, "' from line ", line_num, " in '", macro_place_file_path, "'.");
      ++skipped_macro_num;
      continue;
    }
    if (idb_instance->get_cell_master() == nullptr || !idb_instance->get_cell_master()->is_block()) {
      FPLOG.warn(Loc::current(), "Skip non-block macro '", instance_name, "' from line ", line_num, " in '", macro_place_file_path,
                 "'.");
      ++skipped_macro_num;
      continue;
    }

    int32_t x = FPUTIL.transMicronToDBU(x_micron, micron_dbu);
    int32_t y = FPUTIL.transMicronToDBU(y_micron, micron_dbu);
    if (!dmInst->placeInst(instance_name, x, y, orient_name, "", "", "fixed", false)) {
      FPLOG.warn(Loc::current(), "Failed to place macro '", instance_name, "' from line ", line_num, " in '", macro_place_file_path,
                 "'.");
      ++skipped_macro_num;
      continue;
    }
    ++placed_macro_num;
  }

  FPLOG.info(Loc::current(), "Placed ", placed_macro_num, " macros from '", macro_place_file_path, "'; skipped ", skipped_macro_num, ".");
}

#endif

#endif

#if 1  // FP调用外部的API

#if 1  // TopData

#if 1  // input

void FPInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
  wrapDatabase();
}

void FPInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  Config& config = FPDM.getConfig();
  config.temp_directory_path = "./fp_temp_directory";
  config.thread_number = 128;
  config.macro_placement_halo = -1.0;
  config.macro_routing_halo = -1.0;
  config.die_mode = DieMode::kNone;
  config.die_site_name = "";
  config.die_aspect_ratio = -1.0;
  config.die_utilization = -1.0;
  config.die_width_micron = -1.0;
  config.die_height_micron = -1.0;
  config.die_margin_left_micron = -1.0;
  config.die_margin_right_micron = -1.0;
  config.die_margin_top_micron = -1.0;
  config.die_margin_bottom_micron = -1.0;
  config.io_pin_layer_name_list.clear();
  config.pg_connect_list.clear();
  config.pg_rail_list.clear();
  config.pg_stripe_list.clear();
  config.pg_layer_pair_list.clear();
  config.tapcell_name = "";
  config.tap_distance_micron = -1.0;
  config.left_endcap_name = "";
  config.right_endcap_name = "";
  config.top_endcap_name_list.clear();
  config.bottom_endcap_name_list.clear();
  config.top_boundary_tap_name_list.clear();
  config.bottom_boundary_tap_name_list.clear();
  config.boundary_tap_rule_micron = -1.0;

  std::filesystem::path config_file_path = std::filesystem::absolute(FPUTIL.getConfigValue<std::string>(config_map, "-config", ""));
  std::ifstream config_file_stream(config_file_path);
  if (!config_file_stream.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open config file '", config_file_path.string(), "'!");
  }

  nlohmann::json config_json;
  config_file_stream >> config_json;
  std::filesystem::path config_directory_path = config_file_path.parent_path();

  nlohmann::json& ifp_json = config_json["ifp"];
  config.temp_directory_path = FPUTIL.getAbsolutePath(config_directory_path, ifp_json["temp_directory_path"].get<std::string>());
  config.thread_number = std::max(ifp_json["thread_number"].get<int32_t>(), 1);

  nlohmann::json& macro_placer_json = config_json["macro_placer"];
  config.macro_placement_halo = macro_placer_json["macro_placement_halo"].get<double>();
  config.macro_routing_halo = macro_placer_json["macro_routing_halo"].get<double>();

  nlohmann::json& die_builder_json = config_json["die_builder"];
  config.die_mode = GetDieModeByName()(die_builder_json["mode"].get<std::string>());
  config.die_site_name = die_builder_json["site_name"].get<std::string>();
  nlohmann::json& die_margin_json = die_builder_json["margin"];
  config.die_margin_left_micron = die_margin_json["left_micron"].get<double>();
  config.die_margin_right_micron = die_margin_json["right_micron"].get<double>();
  config.die_margin_top_micron = die_margin_json["top_micron"].get<double>();
  config.die_margin_bottom_micron = die_margin_json["bottom_micron"].get<double>();
  if (config.die_mode == DieMode::kDieUtil) {
    nlohmann::json& die_util_json = die_builder_json["die_util"];
    config.die_aspect_ratio = die_util_json["aspect_ratio"].get<double>();
    config.die_utilization = die_util_json["utilization"].get<double>();
  } else if (config.die_mode == DieMode::kDieSize) {
    nlohmann::json& die_size_json = die_builder_json["die_size"];
    config.die_width_micron = die_size_json["width_micron"].get<double>();
    config.die_height_micron = die_size_json["height_micron"].get<double>();
  }

  nlohmann::json& io_placer_json = config_json["io_placer"];
  for (nlohmann::json& layer_name_json : io_placer_json["io_layer_list"]) {
    config.io_pin_layer_name_list.push_back(layer_name_json.get<std::string>());
  }

  nlohmann::json& phy_placer_json = config_json["phy_placer"];
  nlohmann::json& well_tap_json = phy_placer_json["well_tap"];
  config.tapcell_name = well_tap_json["cell_name"].get<std::string>();
  config.tap_distance_micron = well_tap_json["distance_micron"].get<double>();
  nlohmann::json& side_endcap_json = phy_placer_json["side_endcap"];
  config.left_endcap_name = side_endcap_json["left_cell_name"].get<std::string>();
  config.right_endcap_name = side_endcap_json["right_cell_name"].get<std::string>();
  nlohmann::json& edge_endcap_json = phy_placer_json["edge_endcap"];
  for (nlohmann::json& cell_name_json : edge_endcap_json["top_cell_name_list"]) {
    config.top_endcap_name_list.push_back(cell_name_json.get<std::string>());
  }
  for (nlohmann::json& cell_name_json : edge_endcap_json["bottom_cell_name_list"]) {
    config.bottom_endcap_name_list.push_back(cell_name_json.get<std::string>());
  }
  nlohmann::json& boundary_tap_json = phy_placer_json["boundary_tap"];
  for (nlohmann::json& cell_name_json : boundary_tap_json["top_cell_name_list"]) {
    config.top_boundary_tap_name_list.push_back(cell_name_json.get<std::string>());
  }
  for (nlohmann::json& cell_name_json : boundary_tap_json["bottom_cell_name_list"]) {
    config.bottom_boundary_tap_name_list.push_back(cell_name_json.get<std::string>());
  }
  config.boundary_tap_rule_micron = boundary_tap_json["rule_micron"].get<double>();

  nlohmann::json& pdn_generator_json = config_json["pdn_generator"];
  for (nlohmann::json& pg_connect_json : pdn_generator_json["global_connect"]) {
    PGGlobalConnect pg_connect;
    pg_connect.set_net_name(pg_connect_json["net_name"].get<std::string>());
    pg_connect.set_pin_name(pg_connect_json["instance_pin_name"].get<std::string>());
    pg_connect.set_net_type(pg_connect_json["is_power"].get<bool>() ? PGNetType::kPower : PGNetType::kGround);
    config.pg_connect_list.push_back(pg_connect);
  }
  for (nlohmann::json& pg_rail_json : pdn_generator_json["rail"]) {
    PGRail pg_rail;
    pg_rail.set_layer_name(pg_rail_json["routing_layer_name"].get<std::string>());
    pg_rail.set_width_micron(pg_rail_json["width_micron"].get<double>());
    config.pg_rail_list.push_back(pg_rail);
  }
  for (nlohmann::json& pg_stripe_json : pdn_generator_json["stripe"]) {
    PGStripe pg_stripe;
    pg_stripe.set_layer_name(pg_stripe_json["routing_layer_name"].get<std::string>());
    pg_stripe.set_width_micron(pg_stripe_json["width_micron"].get<double>());
    pg_stripe.set_pitch_micron(pg_stripe_json["pitch_micron"].get<double>());
    pg_stripe.set_offset_micron(pg_stripe_json["offset_micron"].get<double>());
    config.pg_stripe_list.push_back(pg_stripe);
  }
  for (nlohmann::json& layer_connect_json : pdn_generator_json["connect_layers"]) {
    PGLayerPair pg_layer_pair;
    pg_layer_pair.set_first_layer_name(layer_connect_json["bottom_routing_layer_name"].get<std::string>());
    pg_layer_pair.set_second_layer_name(layer_connect_json["top_routing_layer_name"].get<std::string>());
    config.pg_layer_pair_list.push_back(pg_layer_pair);
  }

  omp_set_num_threads(config.thread_number);
}

void FPInterface::wrapDatabase()
{
  wrapDBInfo();
  wrapMicronDBU();
  wrapManufactureGrid();
  wrapCellArea();
  wrapSiteMap();
  wrapCellMasterMap();
  wrapRoutingLayerList();
  wrapInstanceList();
  wrapNetList();
  wrapIOPinList();
}

void FPInterface::wrapDBInfo()
{
  FPDM.getDatabase().set_design_name(dmInst->get_idb_design()->get_design_name());
}

void FPInterface::wrapMicronDBU()
{
  FPDM.getDatabase().set_micron_dbu(dmInst->get_idb_design()->get_units()->get_micron_dbu());
}

void FPInterface::wrapManufactureGrid()
{
  FPDM.getDatabase().set_manufacture_grid(dmInst->get_idb_layout()->get_munufacture_grid());
}

void FPInterface::wrapCellArea()
{
  double cell_area = 0.0;
  int32_t micron_dbu = FPDM.getDatabase().get_micron_dbu();
  for (idb::IdbInstance* idb_instance : dmInst->get_idb_design()->get_instance_list()->get_instance_list()) {
    idb::IdbCellMaster* idb_cell_master = idb_instance->get_cell_master();
    double width_micron = idb_cell_master->get_width() / 1.0 / micron_dbu;
    double height_micron = idb_cell_master->get_height() / 1.0 / micron_dbu;
    cell_area += width_micron * height_micron;
  }
  FPDM.getDatabase().set_cell_area(cell_area);
}

void FPInterface::wrapSiteMap()
{
  std::map<std::string, Site>& site_map = FPDM.getDatabase().get_site_map();
  for (idb::IdbSite* idb_site : dmInst->get_idb_layout()->get_sites()->get_site_list()) {
    Site site;
    site.set_name(idb_site->get_name());
    site.set_width(idb_site->get_width());
    site.set_height(idb_site->get_height());
    site_map[site.get_name()] = site;
  }
}

void FPInterface::wrapCellMasterMap()
{
  std::map<std::string, CellMaster>& cell_master_map = FPDM.getDatabase().get_cell_master_map();
  cell_master_map.clear();
  for (idb::IdbCellMaster* idb_cell_master : dmInst->get_idb_layout()->get_cell_master_list()->get_cell_master()) {
    CellMaster cell_master;
    cell_master.set_name(idb_cell_master->get_name());
    cell_master.set_width(idb_cell_master->get_width());
    cell_master.set_height(idb_cell_master->get_height());
    cell_master.set_pad(idb_cell_master->is_pad());
    cell_master.set_pad_filler(idb_cell_master->is_pad_filler());
    cell_master.set_spacer(idb_cell_master->is_spacer());
    cell_master.set_corner(idb_cell_master->get_site() != nullptr && idb_cell_master->get_site()->is_corner_site());
    cell_master_map[cell_master.get_name()] = cell_master;
  }
}

void FPInterface::wrapRoutingLayerList()
{
  std::vector<RoutingLayer>& routing_layer_list = FPDM.getDatabase().get_routing_layer_list();
  routing_layer_list.clear();
  for (idb::IdbLayer* idb_layer : dmInst->get_idb_layout()->get_layers()->get_routing_layers()) {
    idb::IdbLayerRouting* idb_routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layer);
    RoutingLayer routing_layer;
    routing_layer.set_name(idb_routing_layer->get_name());
    routing_layer.set_layer_idx(idb_routing_layer->get_id());
    routing_layer.set_order(idb_routing_layer->get_order());
    routing_layer.set_pitch_x(idb_routing_layer->get_pitch_x());
    routing_layer.set_pitch_y(idb_routing_layer->get_pitch_y());
    routing_layer.set_min_width(idb_routing_layer->get_min_width());
    routing_layer.set_prefer_track_offset(idb_routing_layer->get_offset_prefer());
    routing_layer.set_spacing(idb_routing_layer->get_spacing(0));
    routing_layer.set_prefer_direction(idb_routing_layer->is_horizontal() ? Direction::kHorizontal : Direction::kVertical);

    idb::IdbTrackGrid* prefer_track_grid = idb_routing_layer->get_prefer_track_grid();
    if (prefer_track_grid != nullptr && prefer_track_grid->get_track() != nullptr) {
      routing_layer.set_prefer_track_pitch(prefer_track_grid->get_track()->get_pitch());
    }
    idb::IdbTrackGrid* nonprefer_track_grid = idb_routing_layer->get_nonprefer_track_grid();
    if (nonprefer_track_grid != nullptr && nonprefer_track_grid->get_track() != nullptr) {
      routing_layer.set_nonprefer_track_pitch(nonprefer_track_grid->get_track()->get_pitch());
    }
    routing_layer_list.push_back(routing_layer);
  }
}

void FPInterface::wrapInstanceList()
{
  std::vector<Instance>& instance_list = FPDM.getDatabase().get_instance_list();
  instance_list.clear();
  for (idb::IdbInstance* idb_instance : dmInst->get_idb_design()->get_instance_list()->get_instance_list()) {
    Instance instance;
    instance.set_name(idb_instance->get_name());
    instance.set_master_name(idb_instance->get_cell_master()->get_name());
    instance.set_orient(wrapPlacementOrientation(idb_instance->get_orient()));
    instance.set_width(idb_instance->get_cell_master()->get_width());
    instance.set_height(idb_instance->get_cell_master()->get_height());
    instance.set_macro(idb_instance->get_cell_master()->is_block());
    instance.set_fixed(idb_instance->is_fixed());
    instance.set_cover(idb_instance->is_cover());
    instance.set_placed(idb_instance->has_placed());
    if (idb_instance->has_placed()) {
      instance.set_coord(idb_instance->get_coordinate()->get_x(), idb_instance->get_coordinate()->get_y());
      idb_instance->set_bounding_box();
      instance.set_bounding_rect(idb_instance->get_bounding_box()->get_low_x(), idb_instance->get_bounding_box()->get_low_y(),
                                 idb_instance->get_bounding_box()->get_high_x(), idb_instance->get_bounding_box()->get_high_y());
    }
    if (instance.get_macro()) {
      wrapMacroPinShapeList(idb_instance, instance);
    }
    instance_list.push_back(instance);
  }
}

void FPInterface::wrapMacroPinShapeList(idb::IdbInstance* idb_instance, Instance& instance)
{
  if (idb_instance->has_placed()) {
    wrapPlacedMacroPinShapeList(idb_instance, instance);
  } else {
    wrapUnplacedMacroPinShapeList(idb_instance, instance);
  }
}

void FPInterface::wrapPlacedMacroPinShapeList(idb::IdbInstance* idb_instance, Instance& instance)
{
  for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
    idb_pin->set_bounding_box();
    for (idb::IdbLayerShape* idb_layer_shape : idb_pin->get_port_box_list()) {
      for (idb::IdbRect* idb_rect : idb_layer_shape->get_rect_list()) {
        InstancePinShape pin_shape;
        pin_shape.set_pin_name(idb_pin->get_pin_name());
        pin_shape.set_layer_name(idb_layer_shape->get_layer()->get_name());
        pin_shape.set_rect(idb_rect->get_low_x(), idb_rect->get_low_y(), idb_rect->get_high_x(), idb_rect->get_high_y());
        instance.get_pin_shape_list().push_back(pin_shape);
      }
    }
  }
}

void FPInterface::wrapUnplacedMacroPinShapeList(idb::IdbInstance* idb_instance, Instance& instance)
{
  for (idb::IdbTerm* idb_term : idb_instance->get_cell_master()->get_term_list()) {
    for (idb::IdbPort* idb_port : idb_term->get_port_list()) {
      for (idb::IdbLayerShape* idb_layer_shape : idb_port->get_layer_shape()) {
        for (idb::IdbRect* idb_rect : idb_layer_shape->get_rect_list()) {
          InstancePinShape pin_shape;
          pin_shape.set_pin_name(idb_term->get_name());
          pin_shape.set_layer_name(idb_layer_shape->get_layer()->get_name());
          pin_shape.set_rect(idb_rect->get_low_x(), idb_rect->get_low_y(), idb_rect->get_high_x(), idb_rect->get_high_y());
          instance.get_pin_shape_list().push_back(pin_shape);
        }
      }
    }
  }
}

PlacementOrientation FPInterface::wrapPlacementOrientation(idb::IdbOrient idb_orient)
{
  switch (idb_orient) {
    case idb::IdbOrient::kN_R0:
      return PlacementOrientation::kN;
    case idb::IdbOrient::kW_R90:
      return PlacementOrientation::kW;
    case idb::IdbOrient::kS_R180:
      return PlacementOrientation::kS;
    case idb::IdbOrient::kE_R270:
      return PlacementOrientation::kE;
    case idb::IdbOrient::kFN_MY:
      return PlacementOrientation::kFN;
    case idb::IdbOrient::kFE_MY90:
      return PlacementOrientation::kFE;
    case idb::IdbOrient::kFS_MX:
      return PlacementOrientation::kFS;
    case idb::IdbOrient::kFW_MX90:
      return PlacementOrientation::kFW;
    default:
      return PlacementOrientation::kNone;
  }
}

void FPInterface::wrapNetList()
{
  std::vector<Net>& net_list = FPDM.getDatabase().get_net_list();
  net_list.clear();
  for (idb::IdbNet* idb_net : dmInst->get_idb_design()->get_net_list()->get_net_list()) {
    Net net;
    net.set_name(idb_net->get_net_name());
    net.set_pin_num(idb_net->get_pin_number());
    net.set_clock(idb_net->is_clock());
    net.set_pdn(idb_net->is_pdn());
    net.set_power(idb_net->is_power());
    net.set_ground(idb_net->is_ground());

    for (idb::IdbPin* idb_pin : idb_net->get_instance_pin_list()->get_pin_list()) {
      NetPin net_pin;
      net_pin.set_instance_name(idb_pin->get_instance()->get_name());
      net_pin.set_pin_name(idb_pin->get_pin_name());
      net_pin.set_coord(idb_pin->get_average_coordinate()->get_x(), idb_pin->get_average_coordinate()->get_y());
      net_pin.set_offset_x(idb_pin->get_term()->get_average_position().get_x());
      net_pin.set_offset_y(idb_pin->get_term()->get_average_position().get_y());
      net_pin.set_placed(idb_pin->get_instance()->has_placed());
      net.get_net_pin_list().push_back(net_pin);
    }
    for (idb::IdbPin* idb_pin : idb_net->get_io_pins()->get_pin_list()) {
      NetPin net_pin;
      net_pin.set_pin_name(idb_pin->get_pin_name());
      net_pin.set_coord(idb_pin->get_average_coordinate()->get_x(), idb_pin->get_average_coordinate()->get_y());
      net_pin.set_placed(idb_pin->get_term()->is_placed());
      net_pin.set_io(true);
      net.get_net_pin_list().push_back(net_pin);
    }
    net_list.push_back(net);
  }
}

void FPInterface::wrapIOPinList()
{
  std::vector<IOPin>& io_pin_list = FPDM.getDatabase().get_io_pin_list();
  io_pin_list.clear();
  for (idb::IdbPin* idb_pin : dmInst->get_idb_design()->get_io_pin_list()->get_pin_list()) {
    idb::IdbTerm* idb_term = idb_pin->get_term();
    IOPin io_pin;
    io_pin.set_name(idb_pin->get_pin_name());
    io_pin.set_orient(wrapPlacementOrientation(idb_pin->get_orient()));
    io_pin.set_coord(idb_pin->get_average_coordinate()->get_x(), idb_pin->get_average_coordinate()->get_y());
    io_pin.set_offset(idb_term->get_average_position().get_x(), idb_term->get_average_position().get_y());
    io_pin.set_port_exist(idb_term->is_port_exist());
    io_pin.set_special_net(idb_pin->is_special_net_pin());
    io_pin.set_placed(idb_term->is_placed());
    io_pin.set_fixed(idb_term->get_placement_status() == idb::IdbPlacementStatus::kFixed);
    idb_pin->set_bounding_box();
    for (idb::IdbLayerShape* idb_layer_shape : idb_pin->get_port_box_list()) {
      for (idb::IdbRect* idb_rect : idb_layer_shape->get_rect_list()) {
        IOPort io_port;
        io_port.set_layer_name(idb_layer_shape->get_layer()->get_name());
        io_port.set_rect(idb_rect->get_low_x(), idb_rect->get_low_y(), idb_rect->get_high_x(), idb_rect->get_high_y());
        io_pin.get_port_list().push_back(io_port);
      }
    }

    if (idb_pin->get_net() != nullptr) {
      idb::IdbInstance* idb_instance = dmInst->getIoCellByIoPin(idb_pin);
      if (idb_instance != nullptr) {
        io_pin.set_instance_name(idb_instance->get_name());
      }
    }
    io_pin_list.push_back(io_pin);
  }
}

#endif

#if 1  // output

void FPInterface::output()
{
  outputFloorplan();
  outputPGNetList();
  outputIOPinList();
  outputIOInstancePlacement();
  outputMacroPlacement();
  outputNewInstanceList();
  outputPGSegmentList();
}

void FPInterface::outputFloorplan()
{
  Database& database = FPDM.getDatabase();
  if (database.is_die_updated()) {
    outputDie();
    database.set_die_updated(false);
  }
  if (database.is_core_updated()) {
    outputCore();
    outputRowList();
    database.set_core_updated(false);
  }
  if (database.is_track_updated()) {
    outputTrackList();
    database.set_track_updated(false);
  }
}

void FPInterface::outputDie()
{
  Die& die = FPDM.getDatabase().get_die();
  idb::IdbDie* idb_die = dmInst->get_idb_layout()->get_die();
  idb_die->reset();
  idb_die->add_point(die.get_ll_x(), die.get_ll_y());
  idb_die->add_point(die.get_ur_x(), die.get_ur_y());
}

void FPInterface::outputCore()
{
  Core& core = FPDM.getDatabase().get_core();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  idb::IdbSites* idb_site_list = idb_layout->get_sites();
  idb::IdbSite* core_site = idb_site_list->find_site(core.get_core_site_name());
  idb::IdbSite* io_site = idb_site_list->find_site(core.get_io_site_name());
  idb::IdbSite* corner_site = idb_site_list->find_site(core.get_corner_site_name());

  idb_site_list->set_core_site(core_site);
  idb_site_list->set_io_site(io_site);
  idb_site_list->set_corener_site(corner_site);

  idb_layout->get_core()->set_bounding_box(core.get_ll_x(), core.get_ll_y(), core.get_ur_x(), core.get_ur_y());
}

void FPInterface::outputRowList()
{
  Database& database = FPDM.getDatabase();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  idb_layout->get_rows()->reset();
  for (Row& row : database.get_new_row_list()) {
    Site& site = database.get_site_map()[row.get_site_name()];
    int32_t site_width = site.get_width();
    int32_t row_width = std::abs(row.get_width());
    if (site_width <= 0) {
      FPLOG.error(Loc::current(), "The site '", row.get_site_name(), "' has invalid width ", site_width, "!");
    }
    if (row_width % site_width != 0) {
      int32_t aligned_row_width = FPUTIL.alignDown(row_width, site_width);
      FPLOG.warn(Loc::current(), "The row '", row.get_name(), "' width ", row_width, " is aligned down to ", aligned_row_width, " by site '",
                 row.get_site_name(), "' width ", site_width, "!");
      row.set_rect(row.get_ll_x(), row.get_ll_y(), row.get_ll_x() + aligned_row_width, row.get_ur_y());
      row_width = aligned_row_width;
    }
    int32_t site_num = row_width / site_width;
    idb::IdbOrient orient = unwrapPlacementOrientation(row.get_orient());
    dmInst->createRow(row.get_name(), row.get_site_name(), row.get_ll_x(), row.get_y(), orient, site_num, 1, site.get_width(), 0);
  }
}

idb::IdbOrient FPInterface::unwrapPlacementOrientation(PlacementOrientation orient)
{
  switch (orient) {
    case PlacementOrientation::kN:
      return idb::IdbOrient::kN_R0;
    case PlacementOrientation::kW:
      return idb::IdbOrient::kW_R90;
    case PlacementOrientation::kS:
      return idb::IdbOrient::kS_R180;
    case PlacementOrientation::kE:
      return idb::IdbOrient::kE_R270;
    case PlacementOrientation::kFN:
      return idb::IdbOrient::kFN_MY;
    case PlacementOrientation::kFE:
      return idb::IdbOrient::kFE_MY90;
    case PlacementOrientation::kFS:
      return idb::IdbOrient::kFS_MX;
    case PlacementOrientation::kFW:
      return idb::IdbOrient::kFW_MX90;
    default:
      return idb::IdbOrient::kNone;
  }
}

void FPInterface::outputTrackList()
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  for (Track& track : FPDM.getDatabase().get_new_track_list()) {
    idb::IdbLayerRouting* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(idb_layout->get_layers()->find_layer(track.get_layer_name()));

    idb::IdbTrackGrid* x_track_grid = idb_layout->get_track_grid_list()->add_track_grid();
    x_track_grid->add_layer_list(routing_layer);
    routing_layer->add_track_grid(x_track_grid);
    x_track_grid->get_track()->set_direction(idb::IdbTrackDirection::kDirectionX);
    x_track_grid->get_track()->set_pitch(track.get_x_pitch());
    x_track_grid->get_track()->set_start(track.get_x_offset());
    x_track_grid->set_track_number((idb_layout->get_die()->get_width() - track.get_x_offset()) / track.get_x_pitch());

    idb::IdbTrackGrid* y_track_grid = idb_layout->get_track_grid_list()->add_track_grid();
    y_track_grid->add_layer_list(routing_layer);
    routing_layer->add_track_grid(y_track_grid);
    y_track_grid->get_track()->set_direction(idb::IdbTrackDirection::kDirectionY);
    y_track_grid->get_track()->set_pitch(track.get_y_pitch());
    y_track_grid->get_track()->set_start(track.get_y_offset());
    y_track_grid->set_track_number((idb_layout->get_die()->get_height() - track.get_y_offset()) / track.get_y_pitch());
  }
}

void FPInterface::outputPGNetList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (PGNet& pg_net : FPDM.getDatabase().get_pg_net_list()) {
    idb::IdbConnectType connect_type = idb::IdbConnectType::kNone;
    if (pg_net.get_type() == PGNetType::kPower) {
      connect_type = idb::IdbConnectType::kPower;
    } else if (pg_net.get_type() == PGNetType::kGround) {
      connect_type = idb::IdbConnectType::kGround;
    }
    idb::IdbSpecialNet* idb_special_net = idb_design->createOrFindSpecialNet(pg_net.get_name(), connect_type);
    for (std::string& instance_pin_name : pg_net.get_instance_pin_name_list()) {
      if (std::find(idb_special_net->get_pin_string_list().begin(), idb_special_net->get_pin_string_list().end(), instance_pin_name)
          == idb_special_net->get_pin_string_list().end()) {
        idb_special_net->add_pin_string(instance_pin_name);
      }
    }
    for (std::pair<const std::string, IOPinDirection>& pair : pg_net.get_io_pin_name_to_direction_map()) {
      idb::IdbPin* idb_pin = idb_design->get_io_pin_list()->find_pin(pair.first);
      if (idb_pin == nullptr) {
        idb_pin = idb_design->createOrFindIoPin(pair.first);
      }
      idb_pin->set_as_io();
      idb::IdbTerm* idb_term = idb_pin->get_term();
      if (idb_term == nullptr) {
        idb_term = idb_pin->set_term();
      }
      if (pair.second != IOPinDirection::kNone) {
        idb_term->set_direction(unwrapIOPinDirection(pair.second));
      }
      idb_term->set_type(connect_type);
      idb_design->connectPinToSpecialNet(idb_pin, idb_special_net);
    }
  }
}

idb::IdbConnectDirection FPInterface::unwrapIOPinDirection(IOPinDirection io_pin_direction)
{
  switch (io_pin_direction) {
    case IOPinDirection::kInput:
      return idb::IdbConnectDirection::kInput;
    case IOPinDirection::kOutput:
      return idb::IdbConnectDirection::kOutput;
    case IOPinDirection::kOutputTriState:
      return idb::IdbConnectDirection::kOutputTriState;
    case IOPinDirection::kInOut:
      return idb::IdbConnectDirection::kInOut;
    case IOPinDirection::kFeedThru:
      return idb::IdbConnectDirection::kFeedThru;
    default:
      return idb::IdbConnectDirection::kNone;
  }
}

void FPInterface::outputIOPinList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  for (IOPin& io_pin : FPDM.getDatabase().get_io_pin_list()) {
    if (!io_pin.is_updated()) {
      continue;
    }

    idb::IdbPin* idb_pin = idb_design->get_io_pin_list()->find_pin(io_pin.get_name());
    idb::IdbTerm* idb_term = idb_pin->get_term();
    if (io_pin.get_fixed()) {
      idb_term->set_placement_status_fix();
    } else if (io_pin.get_placed()) {
      idb_term->set_placement_status_place();
    }
    if (io_pin.is_offset_updated()) {
      idb_term->set_average_position(io_pin.get_offset_x(), io_pin.get_offset_y());
    }
    if (io_pin.is_port_exist_updated()) {
      idb_term->set_has_port(io_pin.get_port_exist());
    }
    if (io_pin.get_direct_location()) {
      idb_pin->set_location(io_pin.get_x(), io_pin.get_y());
    }
    idb_pin->set_average_coordinate(io_pin.get_x(), io_pin.get_y());
    idb_pin->set_orient(unwrapPlacementOrientation(io_pin.get_orient()));

    for (IOPort& io_port : io_pin.get_new_port_list()) {
      idb::IdbPort* idb_port = idb_term->add_port();
      idb_port->set_coordinate(io_port.get_x(), io_port.get_y());
      if (io_port.get_placed()) {
        idb_port->set_placement_status_place();
      }
      idb::IdbLayerShape* idb_layer_shape = idb_port->add_layer_shape();
      idb_layer_shape->set_type_rect();
      idb_layer_shape->set_layer(idb_layout->get_layers()->find_layer(io_port.get_layer_name()));
      idb_layer_shape->add_rect(io_port.get_ll_x(), io_port.get_ll_y(), io_port.get_ur_x(), io_port.get_ur_y());
    }
    idb_pin->set_bounding_box();
    io_pin.get_new_port_list().clear();
    io_pin.set_updated(false);
  }
}

void FPInterface::outputIOInstancePlacement()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.is_placement_updated() || instance.get_macro()) {
      continue;
    }
    idb::IdbOrient orient = unwrapPlacementOrientation(instance.get_orient());
    if (orient == idb::IdbOrient::kNone) {
      orient = idb::IdbOrient::kN_R0;
    }
    idb_design->placeInstance(instance.get_name(), instance.get_x(), instance.get_y(), orient, idb::IdbPlacementStatus::kPlaced);
    instance.set_placement_updated(false);
  }
}

void FPInterface::outputMacroPlacement()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.is_placement_updated()) {
      continue;
    }
    idb::IdbOrient orient = unwrapPlacementOrientation(instance.get_orient());
    if (orient == idb::IdbOrient::kNone) {
      orient = idb::IdbOrient::kN_R0;
    }
    idb_design->placeInstance(instance.get_name(), instance.get_x(), instance.get_y(), orient, idb::IdbPlacementStatus::kFixed);
    instance.set_placement_updated(false);
  }
}

void FPInterface::outputNewInstanceList()
{
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.is_new_instance()) {
      continue;
    }
    idb::IdbOrient orient = unwrapPlacementOrientation(instance.get_orient());
    if (orient == idb::IdbOrient::kNone) {
      orient = idb::IdbOrient::kN_R0;
    }
    dmInst->createInstance(instance.get_name(), instance.get_master_name(), instance.get_x(), instance.get_y(), orient, idb::IdbInstanceType::kDist,
                           idb::IdbPlacementStatus::kFixed);
    instance.set_new_instance(false);
  }
}

void FPInterface::outputPGSegmentList()
{
  adjustPGLineSegmentListByViaEnclosure();

  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  std::map<std::string, idb::IdbSpecialWire*> pg_net_name_to_wire_map;
  for (PGSegment& pg_segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (!pg_segment.get_generated()) {
      continue;
    }
    idb::IdbSpecialNet* idb_special_net = idb_design->get_special_net_list()->find_net(pg_segment.get_net_name());
    if (idb_special_net == nullptr) {
      continue;
    }
    idb::IdbSpecialWire* idb_special_wire = nullptr;
    std::map<std::string, idb::IdbSpecialWire*>::iterator wire_iter = pg_net_name_to_wire_map.find(pg_segment.get_net_name());
    if (wire_iter == pg_net_name_to_wire_map.end()) {
      idb_special_wire = idb_design->get_special_net_list()->generateWire(pg_segment.get_net_name());
      pg_net_name_to_wire_map[pg_segment.get_net_name()] = idb_special_wire;
    } else {
      idb_special_wire = wire_iter->second;
    }
    if (pg_segment.get_type() == PGSegmentType::kVia) {
      outputPGVia(idb_special_wire, pg_segment);
      pg_segment.set_generated(false);
      continue;
    }
    idb::IdbLayer* idb_layer = idb_layout->get_layers()->find_layer(pg_segment.get_layer_name());
    if (idb_layer == nullptr) {
      continue;
    }
    idb::IdbSpecialWireSegment* idb_segment = idb_special_wire->add_segment();
    idb_segment->set_layer_as_new();
    idb_segment->set_layer(idb_layer);
    idb_segment->set_route_width(pg_segment.get_width());
    idb_segment->set_shape_type(pg_segment.get_type() == PGSegmentType::kFollowPin ? idb::IdbWireShapeType::kFollowPin : idb::IdbWireShapeType::kStripe);
    idb_segment->add_point(pg_segment.get_start_x(), pg_segment.get_start_y());
    idb_segment->add_point(pg_segment.get_end_x(), pg_segment.get_end_y());
    idb_segment->set_bounding_box();
    pg_segment.set_generated(false);
  }
}

void FPInterface::adjustPGLineSegmentListByViaEnclosure()
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  std::vector<PGSegment>& pg_segment_list = FPDM.getDatabase().get_pg_segment_list();
  std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>> pg_net_layer_coord_to_stripe_segment_list_map;
  for (PGSegment& pg_segment : pg_segment_list) {
    if (pg_segment.get_type() != PGSegmentType::kStripe) {
      continue;
    }
    std::string pg_net_layer_key = FPUTIL.getString(pg_segment.get_net_name(), "|", pg_segment.get_layer_name());
    int32_t line_coord = pg_segment.is_vertical() ? pg_segment.get_start_x() : pg_segment.get_start_y();
    pg_net_layer_coord_to_stripe_segment_list_map[pg_net_layer_key][line_coord].push_back(&pg_segment);
  }

  for (PGSegment& pg_segment : pg_segment_list) {
    if (pg_segment.get_type() != PGSegmentType::kVia) {
      continue;
    }

    std::vector<idb::IdbLayerCut*> idb_cut_layer_list;
    if (!pg_segment.get_cut_layer_name().empty()) {
      idb::IdbLayer* idb_layer = idb_layout->get_layers()->find_layer(pg_segment.get_cut_layer_name());
      idb::IdbLayerCut* idb_cut_layer = dynamic_cast<idb::IdbLayerCut*>(idb_layer);
      if (idb_cut_layer != nullptr) {
        idb_cut_layer_list.push_back(idb_cut_layer);
      }
    } else {
      idb_cut_layer_list = idb_layout->get_layers()->find_cut_layer_list(pg_segment.get_bottom_layer_name(), pg_segment.get_top_layer_name());
    }

    for (idb::IdbLayerCut* idb_cut_layer : idb_cut_layer_list) {
      idb::IdbVia* idb_via = getIDBVia(idb_cut_layer, pg_segment);
      if (idb_via == nullptr) {
        continue;
      }
      idb::IdbLayerShape idb_bottom_layer_shape = idb_via->get_bottom_layer_shape();
      idb::IdbRect idb_bottom_enclosure = idb_bottom_layer_shape.get_bounding_box();
      adjustLineSegmentListByViaEnclosure(
          pg_net_layer_coord_to_stripe_segment_list_map, pg_segment, idb_bottom_layer_shape.get_layer()->get_name(),
          pg_segment.get_start_x() + idb_bottom_enclosure.get_low_x(), pg_segment.get_start_y() + idb_bottom_enclosure.get_low_y(),
          pg_segment.get_start_x() + idb_bottom_enclosure.get_high_x(), pg_segment.get_start_y() + idb_bottom_enclosure.get_high_y());

      idb::IdbLayerShape idb_top_layer_shape = idb_via->get_top_layer_shape();
      idb::IdbRect idb_top_enclosure = idb_top_layer_shape.get_bounding_box();
      adjustLineSegmentListByViaEnclosure(pg_net_layer_coord_to_stripe_segment_list_map, pg_segment, idb_top_layer_shape.get_layer()->get_name(),
                                          pg_segment.get_start_x() + idb_top_enclosure.get_low_x(), pg_segment.get_start_y() + idb_top_enclosure.get_low_y(),
                                          pg_segment.get_start_x() + idb_top_enclosure.get_high_x(), pg_segment.get_start_y() + idb_top_enclosure.get_high_y());
    }
  }
}

idb::IdbVia* FPInterface::getIDBVia(idb::IdbLayerCut* idb_cut_layer, PGSegment& pg_segment)
{
  std::string via_name = idb_cut_layer->get_name() + "_" + std::to_string(pg_segment.get_via_width()) + "x" + std::to_string(pg_segment.get_via_height());
  idb::IdbVia* idb_via = dmInst->get_idb_design()->get_via_list()->find_via(via_name);
  if (idb_via == nullptr) {
    idb_via = buildIDBVia(via_name, idb_cut_layer, pg_segment);
  }
  return idb_via;
}

idb::IdbVia* FPInterface::buildIDBVia(std::string via_name, idb::IdbLayerCut* idb_cut_layer, PGSegment& pg_segment)
{
  idb::IdbViaRuleGenerate* idb_via_rule = idb_cut_layer->get_via_rule();
  if (idb_via_rule == nullptr) {
    return nullptr;
  }

  std::pair<int32_t, int32_t> row_col_pair = getIDBViaRowCol(idb_cut_layer, pg_segment);
  int32_t cut_row_num = row_col_pair.first;
  int32_t cut_col_num = row_col_pair.second;
  if (cut_row_num == 0 || cut_col_num == 0) {
    return nullptr;
  }

  int32_t cut_size_x = idb_via_rule->get_cut_rect()->get_width();
  int32_t cut_size_y = idb_via_rule->get_cut_rect()->get_height();
  int32_t cut_spacing_x = idb_via_rule->get_spacing_x();
  int32_t cut_spacing_y = idb_via_rule->get_spacing_y();
  std::vector<idb::IdbLayerCutSpacing*> idb_cut_spacing_list = idb_cut_layer->get_spacings();
  if (!idb_cut_spacing_list.empty()) {
    int32_t cut_spacing = idb_cut_spacing_list.front()->get_spacing();
    cut_spacing_x = std::max(cut_spacing_x, cut_size_x + cut_spacing);
    cut_spacing_y = std::max(cut_spacing_y, cut_size_y + cut_spacing);
  }
  cut_spacing_x -= cut_size_x;
  cut_spacing_y -= cut_size_y;

  int32_t cut_width = cut_col_num * cut_size_x + (cut_col_num - 1) * cut_spacing_x;
  int32_t cut_height = cut_row_num * cut_size_y + (cut_row_num - 1) * cut_spacing_y;
  int32_t enclosure_x = (pg_segment.get_via_width() - cut_width) / 2;
  int32_t enclosure_y = (pg_segment.get_via_height() - cut_height) / 2;

  idb::IdbVias* idb_via_list = dmInst->get_idb_design()->get_via_list();
  idb::IdbVia* idb_via = idb_via_list->add_via(via_name);
  idb::IdbViaMaster* idb_via_master = idb_via->get_instance();
  idb_via_master->set_type_generate();
  idb::IdbViaMasterGenerate* idb_via_master_generate = idb_via_master->get_master_generate();
  idb_via_master_generate->set_rule_name(idb_via_rule->get_name());
  idb_via_master_generate->set_rule_generate(idb_via_rule);
  idb_via_master_generate->set_layer_bottom(idb_via_rule->get_layer_bottom());
  idb_via_master_generate->set_layer_cut(idb_cut_layer);
  idb_via_master_generate->set_layer_top(idb_via_rule->get_layer_top());
  idb_via_master_generate->set_cut_size(cut_size_x, cut_size_y);
  idb_via_master_generate->set_cut_spacing(cut_spacing_x, cut_spacing_y);
  idb_via_master_generate->set_cut_row_col(cut_row_num, cut_col_num);
  idb_via_master->set_cut_row_col(cut_row_num, cut_col_num);
  std::string pattern_string = idb_via_list->createViaPatternString(cut_row_num, cut_col_num, idb_cut_layer->get_array_spacing());
  if (!pattern_string.empty()) {
    idb_via_master_generate->set_patttern(pattern_string);
  }
  idb_via_master_generate->set_enclosure_bottom(enclosure_x, enclosure_y);
  idb_via_master_generate->set_enclosure_top(enclosure_x, enclosure_y);

  int32_t cut_ll_x = -cut_width / 2 + idb_via_master_generate->get_original_offset_x();
  int32_t cut_ll_y = -cut_height / 2 + idb_via_master_generate->get_original_offset_y();
  for (int32_t row_idx = 0; row_idx < cut_row_num; ++row_idx) {
    for (int32_t col_idx = 0; col_idx < cut_col_num; ++col_idx) {
      if (idb_via_master_generate->get_patttern() != nullptr && !idb_via_master_generate->is_pattern_cut_exist(row_idx, col_idx)) {
        continue;
      }
      int32_t ll_x = cut_ll_x + col_idx * (cut_size_x + cut_spacing_x);
      int32_t ll_y = cut_ll_y + row_idx * (cut_size_y + cut_spacing_y);
      idb_via_master_generate->add_cut_rect(ll_x, ll_y, ll_x + cut_size_x, ll_y + cut_size_y);
    }
  }
  idb_via_master_generate->set_cut_bouding_rect(cut_ll_x, cut_ll_y, cut_ll_x + cut_width, cut_ll_y + cut_height);
  idb_via_master->set_via_shape();
  return idb_via;
}

std::pair<int32_t, int32_t> FPInterface::getIDBViaRowCol(idb::IdbLayerCut* idb_cut_layer, PGSegment& pg_segment)
{
  idb::IdbViaRuleGenerate* idb_via_rule = idb_cut_layer->get_via_rule();
  if (idb_via_rule == nullptr) {
    return std::make_pair(0, 0);
  }

  int32_t cut_size_x = idb_via_rule->get_cut_rect()->get_width();
  int32_t cut_size_y = idb_via_rule->get_cut_rect()->get_height();
  int32_t cut_pitch_x = idb_via_rule->get_spacing_x();
  int32_t cut_pitch_y = idb_via_rule->get_spacing_y();
  std::vector<idb::IdbLayerCutSpacing*> idb_cut_spacing_list = idb_cut_layer->get_spacings();
  if (!idb_cut_spacing_list.empty()) {
    int32_t cut_spacing = idb_cut_spacing_list.front()->get_spacing();
    cut_pitch_x = std::max(cut_pitch_x, cut_size_x + cut_spacing);
    cut_pitch_y = std::max(cut_pitch_y, cut_size_y + cut_spacing);
  }

  int32_t bottom_overhang_1 = std::max(idb_via_rule->get_enclosure_bottom()->get_overhang_1(), 0);
  int32_t bottom_overhang_2 = std::max(idb_via_rule->get_enclosure_bottom()->get_overhang_2(), 0);
  int32_t top_overhang_1 = std::max(idb_via_rule->get_enclosure_top()->get_overhang_1(), 0);
  int32_t top_overhang_2 = std::max(idb_via_rule->get_enclosure_top()->get_overhang_2(), 0);
  int32_t cut_row_num = 0;
  int32_t cut_col_num = 0;
  int64_t max_cut_num = 0;

  // LEF generated VIA 的 ENCLOSURE 使用长边和短边约束，X/Y 可以交换。
  for (int32_t bottom_swap = 0; bottom_swap < 2; ++bottom_swap) {
    int32_t bottom_enclosure_x = bottom_swap == 0 ? bottom_overhang_1 : bottom_overhang_2;
    int32_t bottom_enclosure_y = bottom_swap == 0 ? bottom_overhang_2 : bottom_overhang_1;
    for (int32_t top_swap = 0; top_swap < 2; ++top_swap) {
      int32_t top_enclosure_x = top_swap == 0 ? top_overhang_1 : top_overhang_2;
      int32_t top_enclosure_y = top_swap == 0 ? top_overhang_2 : top_overhang_1;
      int32_t min_width = cut_size_x + std::max(bottom_enclosure_x, top_enclosure_x) * 2;
      int32_t min_height = cut_size_y + std::max(bottom_enclosure_y, top_enclosure_y) * 2;
      if (pg_segment.get_via_width() < min_width || pg_segment.get_via_height() < min_height) {
        continue;
      }
      int32_t candidate_row_num = 1 + (pg_segment.get_via_height() - min_height) / cut_pitch_y;
      int32_t candidate_col_num = 1 + (pg_segment.get_via_width() - min_width) / cut_pitch_x;
      int64_t candidate_cut_num = static_cast<int64_t>(candidate_row_num) * candidate_col_num;
      if (max_cut_num < candidate_cut_num) {
        cut_row_num = candidate_row_num;
        cut_col_num = candidate_col_num;
        max_cut_num = candidate_cut_num;
      }
    }
  }
  return std::make_pair(cut_row_num, cut_col_num);
}

void FPInterface::adjustLineSegmentListByViaEnclosure(
    std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>>& pg_net_layer_coord_to_stripe_segment_list_map, PGSegment& pg_segment,
    std::string layer_name, int32_t enclosure_ll_x, int32_t enclosure_ll_y, int32_t enclosure_ur_x, int32_t enclosure_ur_y)
{
  adjustStripeSegmentListByViaEnclosure(pg_net_layer_coord_to_stripe_segment_list_map, pg_segment, layer_name, enclosure_ll_x, enclosure_ll_y, enclosure_ur_x,
                                        enclosure_ur_y);
}

bool FPInterface::adjustStripeSegmentListByViaEnclosure(
    std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>>& pg_net_layer_coord_to_line_segment_list_map, PGSegment& pg_segment,
    std::string layer_name, int32_t enclosure_ll_x, int32_t enclosure_ll_y, int32_t enclosure_ur_x, int32_t enclosure_ur_y)
{
  std::string pg_net_layer_key = FPUTIL.getString(pg_segment.get_net_name(), "|", layer_name);
  std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>>::iterator pg_net_layer_map_iter
      = pg_net_layer_coord_to_line_segment_list_map.find(pg_net_layer_key);
  if (pg_net_layer_map_iter == pg_net_layer_coord_to_line_segment_list_map.end()) {
    return false;
  }

  bool covered = false;
  std::map<int32_t, std::vector<PGSegment*>>& coord_to_line_segment_list_map = pg_net_layer_map_iter->second;
  std::map<int32_t, std::vector<PGSegment*>>::iterator x_coord_iter = coord_to_line_segment_list_map.find(pg_segment.get_start_x());
  if (x_coord_iter != coord_to_line_segment_list_map.end()) {
    covered = adjustLineSegmentByViaEnclosure(x_coord_iter->second, enclosure_ll_x, enclosure_ll_y, enclosure_ur_x, enclosure_ur_y);
  }
  std::map<int32_t, std::vector<PGSegment*>>::iterator y_coord_iter = coord_to_line_segment_list_map.find(pg_segment.get_start_y());
  if (y_coord_iter != coord_to_line_segment_list_map.end()) {
    covered = adjustLineSegmentByViaEnclosure(y_coord_iter->second, enclosure_ll_x, enclosure_ll_y, enclosure_ur_x, enclosure_ur_y) || covered;
  }
  return covered;
}

bool FPInterface::adjustLineSegmentByViaEnclosure(std::vector<PGSegment*>& line_segment_list, int32_t enclosure_ll_x, int32_t enclosure_ll_y,
                                                  int32_t enclosure_ur_x, int32_t enclosure_ur_y)
{
  bool covered = false;
  for (PGSegment* line_segment : line_segment_list) {
    int32_t half_width = line_segment->get_width() / 2;
    if (line_segment->is_vertical()) {
      if (enclosure_ll_x < line_segment->get_ll_x() || line_segment->get_ur_x() < enclosure_ur_x || line_segment->get_ur_y() < enclosure_ll_y
          || enclosure_ur_y < line_segment->get_ll_y()) {
        continue;
      }
      if (line_segment->get_start_y() <= line_segment->get_end_y()) {
        if (enclosure_ll_y < line_segment->get_ll_y()) {
          line_segment->set_start_y(enclosure_ll_y + half_width);
        }
        if (line_segment->get_ur_y() < enclosure_ur_y) {
          line_segment->set_end_y(enclosure_ur_y - half_width);
        }
      } else {
        if (enclosure_ll_y < line_segment->get_ll_y()) {
          line_segment->set_end_y(enclosure_ll_y + half_width);
        }
        if (line_segment->get_ur_y() < enclosure_ur_y) {
          line_segment->set_start_y(enclosure_ur_y - half_width);
        }
      }
      covered = (enclosure_ll_y >= line_segment->get_ll_y() && line_segment->get_ur_y() >= enclosure_ur_y) || covered;
    } else if (line_segment->is_horizontal()) {
      if (enclosure_ll_y < line_segment->get_ll_y() || line_segment->get_ur_y() < enclosure_ur_y || line_segment->get_ur_x() < enclosure_ll_x
          || enclosure_ur_x < line_segment->get_ll_x()) {
        continue;
      }
      if (line_segment->get_start_x() <= line_segment->get_end_x()) {
        if (enclosure_ll_x < line_segment->get_ll_x()) {
          line_segment->set_start_x(enclosure_ll_x + half_width);
        }
        if (line_segment->get_ur_x() < enclosure_ur_x) {
          line_segment->set_end_x(enclosure_ur_x - half_width);
        }
      } else {
        if (enclosure_ll_x < line_segment->get_ll_x()) {
          line_segment->set_end_x(enclosure_ll_x + half_width);
        }
        if (line_segment->get_ur_x() < enclosure_ur_x) {
          line_segment->set_start_x(enclosure_ur_x - half_width);
        }
      }
      covered = (enclosure_ll_x >= line_segment->get_ll_x() && line_segment->get_ur_x() >= enclosure_ur_x) || covered;
    }
  }
  return covered;
}

void FPInterface::outputPGVia(idb::IdbSpecialWire* idb_special_wire, PGSegment& pg_segment)
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  std::vector<idb::IdbLayerCut*> idb_cut_layer_list;
  if (!pg_segment.get_cut_layer_name().empty()) {
    idb::IdbLayer* idb_layer = idb_layout->get_layers()->find_layer(pg_segment.get_cut_layer_name());
    idb::IdbLayerCut* idb_cut_layer = dynamic_cast<idb::IdbLayerCut*>(idb_layer);
    if (idb_cut_layer != nullptr) {
      idb_cut_layer_list.push_back(idb_cut_layer);
    }
  } else {
    idb_cut_layer_list = idb_layout->get_layers()->find_cut_layer_list(pg_segment.get_bottom_layer_name(), pg_segment.get_top_layer_name());
  }
  for (idb::IdbLayerCut* idb_cut_layer : idb_cut_layer_list) {
    idb::IdbVia* idb_via = getIDBVia(idb_cut_layer, pg_segment);
    if (idb_via == nullptr) {
      continue;
    }
    idb::IdbSpecialWireSegment* idb_segment = idb_special_wire->add_segment();
    idb_segment->set_is_via(true);
    idb_segment->add_point(pg_segment.get_start_x(), pg_segment.get_start_y());
    idb_segment->set_layer_as_new();
    idb_segment->set_layer(idb_via->get_top_layer_shape().get_layer());
    idb_segment->set_shape_type(idb::IdbWireShapeType::kStripe);
    idb_segment->set_route_width(0);
    idb::IdbVia* idb_via_copy = idb_segment->copy_via(idb_via);
    idb_via_copy->set_coordinate(pg_segment.get_start_x(), pg_segment.get_start_y());
    idb_segment->set_bounding_box();
  }
}

#endif

#endif

#endif

}  // namespace ifp
