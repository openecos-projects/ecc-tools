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

#include "FPInterface.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace ifp {

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
    FPLOG.error(Loc::current(), "The instance not initialized!");
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

void DataManager::input(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  FPI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  FPI.output();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "fp.log";
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  _config.db_temp_directory_path = _config.temp_directory_path + "die_builder/";
  _config.ip_temp_directory_path = _config.temp_directory_path + "io_placer/";
  _config.mp_temp_directory_path = _config.temp_directory_path + "macro_placer/";
  _config.pg_temp_directory_path = _config.temp_directory_path + "pdn_generator/";
  _config.pp_temp_directory_path = _config.temp_directory_path + "phy_placer/";

  FPUTIL.removeDir(_config.temp_directory_path);
  FPUTIL.createDir(_config.temp_directory_path);
  FPUTIL.createDirByFile(_config.log_file_path);
  FPUTIL.createDir(_config.dm_temp_directory_path);
  FPUTIL.createDir(_config.db_temp_directory_path);
  FPUTIL.createDir(_config.ip_temp_directory_path);
  FPUTIL.createDir(_config.mp_temp_directory_path);
  FPUTIL.createDir(_config.pg_temp_directory_path);
  FPUTIL.createDir(_config.pp_temp_directory_path);
  FPLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  buildInstanceNameToIdxMap();
  buildRoutingLayerNameToIdxMap();
  buildIOPinNameToIdxMap();
  buildIOPinSpecialNet();
}

void DataManager::buildInstanceNameToIdxMap()
{
  std::map<std::string, int32_t>& instance_name_to_idx_map = _database.get_instance_name_to_idx_map();
  instance_name_to_idx_map.clear();
  for (int32_t instance_idx = 0; instance_idx < static_cast<int32_t>(_database.get_instance_list().size()); instance_idx++) {
    instance_name_to_idx_map[_database.get_instance_list()[instance_idx].get_name()] = instance_idx;
  }
}

void DataManager::buildRoutingLayerNameToIdxMap()
{
  std::map<std::string, int32_t>& routing_layer_name_to_idx_map = _database.get_routing_layer_name_to_idx_map();
  routing_layer_name_to_idx_map.clear();
  for (int32_t routing_layer_idx = 0; routing_layer_idx < static_cast<int32_t>(_database.get_routing_layer_list().size()); routing_layer_idx++) {
    routing_layer_name_to_idx_map[_database.get_routing_layer_list()[routing_layer_idx].get_name()] = routing_layer_idx;
  }
}

void DataManager::buildIOPinNameToIdxMap()
{
  std::map<std::string, int32_t>& io_pin_name_to_idx_map = _database.get_io_pin_name_to_idx_map();
  io_pin_name_to_idx_map.clear();
  for (int32_t io_pin_idx = 0; io_pin_idx < static_cast<int32_t>(_database.get_io_pin_list().size()); io_pin_idx++) {
    io_pin_name_to_idx_map[_database.get_io_pin_list()[io_pin_idx].get_name()] = io_pin_idx;
  }
}

void DataManager::buildIOPinSpecialNet()
{
  std::vector<IOPin>& io_pin_list = _database.get_io_pin_list();
  std::map<std::string, int32_t>& io_pin_name_to_idx_map = _database.get_io_pin_name_to_idx_map();
  for (PGGlobalConnect& pg_connect : _config.pg_connect_list) {
    std::map<std::string, int32_t>::iterator io_pin_iter = io_pin_name_to_idx_map.find(pg_connect.get_pin_name());
    if (io_pin_iter != io_pin_name_to_idx_map.end()) {
      io_pin_list[io_pin_iter->second].set_special_net(true);
    }
  }
}

#endif

#if 1  // exhibit

void DataManager::printConfig()
{
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(0), "FP_CONFIG_INPUT");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "temp_directory_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "thread_number");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.thread_number);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "macro_placer");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "placement_halo: ", _config.macro_placement_halo, ", routing_halo: ", _config.macro_routing_halo);

  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "die");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "site: ", _config.die_site_name, ", mode: ", GetDieModeName()(_config.die_mode),
             ", margin: {l: ", _config.die_margin_left_micron, ", r: ", _config.die_margin_right_micron, ", t: ", _config.die_margin_top_micron,
             ", b: ", _config.die_margin_bottom_micron, "}");
  if (_config.die_mode == DieMode::kDieUtil) {
    FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "aspect_ratio: ", _config.die_aspect_ratio, ", utilization: ", _config.die_utilization);
  } else if (_config.die_mode == DieMode::kDieSize) {
    FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "width: ", _config.die_width_micron, ", height: ", _config.die_height_micron);
  }

  std::string io_layer_name_string = "{";
  for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(_config.io_pin_layer_name_list.size()); layer_idx++) {
    if (layer_idx != 0) {
      io_layer_name_string += " ";
    }
    io_layer_name_string += _config.io_pin_layer_name_list[layer_idx];
  }
  io_layer_name_string += "}";
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "io_pin");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "layer: ", io_layer_name_string);

  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "pg_connect_list");
  std::vector<std::string> pg_net_name_list;
  std::map<std::string, std::vector<std::string>> pg_net_to_pin_name_list_map;
  std::map<std::string, PGNetType> pg_net_to_type_map;
  for (PGGlobalConnect& pg_connect : _config.pg_connect_list) {
    std::string& pg_net_name = pg_connect.get_net_name();
    if (pg_net_to_pin_name_list_map.find(pg_net_name) == pg_net_to_pin_name_list_map.end()) {
      pg_net_name_list.push_back(pg_net_name);
    }
    pg_net_to_pin_name_list_map[pg_net_name].push_back(pg_connect.get_pin_name());
    pg_net_to_type_map[pg_net_name] = pg_connect.get_net_type();
  }
  for (std::string& pg_net_name : pg_net_name_list) {
    std::string pin_name_string = "{";
    std::vector<std::string>& pin_name_list = pg_net_to_pin_name_list_map[pg_net_name];
    for (int32_t pin_idx = 0; pin_idx < static_cast<int32_t>(pin_name_list.size()); pin_idx++) {
      if (pin_idx != 0) {
        pin_name_string += " ";
      }
      pin_name_string += pin_name_list[pin_idx];
    }
    pin_name_string += "}";
    FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "pg_net: ", pg_net_name, ", connect_pin: ", pin_name_string,
               ", type: ", pg_net_to_type_map[pg_net_name] == PGNetType::kPower ? "power" : "ground");
  }
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "pdn_mesh");
  for (PGRail& pg_rail : _config.pg_rail_list) {
    FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "rail: ", pg_rail.get_layer_name(), ", rail_width: ", pg_rail.get_width_micron());
  }
  for (PGStripe& pg_stripe : _config.pg_stripe_list) {
    FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "stripe: ", pg_stripe.get_layer_name(), ", width: ", pg_stripe.get_width_micron(),
               ", pitch: ", pg_stripe.get_pitch_micron(), ", offset: ", pg_stripe.get_offset_micron());
  }

  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "phy_insert");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "well_tap: ", _config.tapcell_name, ", interval: ", _config.tap_distance_micron);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "side_endcap: {left: ", _config.left_endcap_name, ", right: ", _config.right_endcap_name, "}");
  std::string top_endcap_name_string = "{";
  for (int32_t endcap_idx = 0; endcap_idx < static_cast<int32_t>(_config.top_endcap_name_list.size()); endcap_idx++) {
    if (endcap_idx != 0) {
      top_endcap_name_string += " ";
    }
    top_endcap_name_string += _config.top_endcap_name_list[endcap_idx];
  }
  top_endcap_name_string += "}";
  std::string bottom_endcap_name_string = "{";
  for (int32_t endcap_idx = 0; endcap_idx < static_cast<int32_t>(_config.bottom_endcap_name_list.size()); endcap_idx++) {
    if (endcap_idx != 0) {
      bottom_endcap_name_string += " ";
    }
    bottom_endcap_name_string += _config.bottom_endcap_name_list[endcap_idx];
  }
  bottom_endcap_name_string += "}";
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "edge_endcap: {top: ", top_endcap_name_string, ", bottom: ", bottom_endcap_name_string, "}");
  std::string top_boundary_tap_name_string = "{";
  for (int32_t tap_idx = 0; tap_idx < static_cast<int32_t>(_config.top_boundary_tap_name_list.size()); tap_idx++) {
    if (tap_idx != 0) {
      top_boundary_tap_name_string += " ";
    }
    top_boundary_tap_name_string += _config.top_boundary_tap_name_list[tap_idx];
  }
  top_boundary_tap_name_string += "}";
  std::string bottom_boundary_tap_name_string = "{";
  for (int32_t tap_idx = 0; tap_idx < static_cast<int32_t>(_config.bottom_boundary_tap_name_list.size()); tap_idx++) {
    if (tap_idx != 0) {
      bottom_boundary_tap_name_string += " ";
    }
    bottom_boundary_tap_name_string += _config.bottom_boundary_tap_name_list[tap_idx];
  }
  bottom_boundary_tap_name_string += "}";
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), "boundary_tap: {top: ", top_boundary_tap_name_string, ", bottom: ", bottom_boundary_tap_name_string,
             ", rule: ", _config.boundary_tap_rule_micron, "}");

  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(0), "FP_CONFIG_BUILD");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "log_file_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.log_file_path);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "dm_temp_directory_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.dm_temp_directory_path);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "db_temp_directory_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.db_temp_directory_path);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "ip_temp_directory_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.ip_temp_directory_path);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "mp_temp_directory_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.mp_temp_directory_path);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "pg_temp_directory_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.pg_temp_directory_path);
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "pp_temp_directory_path");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), _config.pp_temp_directory_path);
}

void DataManager::printDatabase()
{
  Database& database = _database;
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(0), "FP_DATABASE_INPUT");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "design_name");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_design_name());
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "micron_dbu");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_micron_dbu());
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "manufacture_grid");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_manufacture_grid());
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "cell_area");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_cell_area());
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "instance_num");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_instance_list().size());
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "net_num");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_net_list().size());
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "routing_layer_num");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_routing_layer_list().size());
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(1), "io_pin_num");
  FPLOG.info(Loc::current(), FPUTIL.getSpaceByTabNum(2), database.get_io_pin_list().size());
}

#endif

}  // namespace ifp
