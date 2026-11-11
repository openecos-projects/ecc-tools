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

#include <algorithm>
#include <fstream>
#include <cmath>
#include <set>
#include <sstream>

#include "DataManager.hpp"
#include "DieBuilder.hpp"
#include "IOPlacer.hpp"
#include "IdbHalo.h"
#include "IdbOrientTransform.h"
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

namespace {

struct MacroPlacement
{
  Instance* instance = nullptr;
  int32_t x = 0;
  int32_t y = 0;
  PlacementOrientation orient = PlacementOrientation::kNone;
};

void checkKeys(const nlohmann::json& json, std::initializer_list<const char*> allowed, const std::string& path)
{
  if (!json.is_object()) {
    return;
  }
  for (const auto& item : json.items()) {
    bool known = false;
    for (const char* key : allowed) {
      if (item.key() == key) {
        known = true;
        break;
      }
    }
    if (!known) {
      FPLOG.error(Loc::current(), "Unknown config key '", path, ".", item.key(), "'.");
    }
  }
}

}  // namespace

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
  _run_failed = false;
  _pg_via_min_size_cache.clear();
  _pg_via_stack_cache.clear();
  _adjusted_via_count = 0;
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
  _run_failed = true;
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  DieBuilder::initInst();
  FPDB.build();
  DieBuilder::destroyInst();

  IOPlacer::initInst();
  if (FPDM.getConfig().io_placer_mode == IOPlacerMode::kIOCell && !FPIP.planDieForIO()) {
    FPLOG.error(Loc::current(), "IO cell capacity check failed; stopping floorplan.");
    _run_failed = true;
    IOPlacer::destroyInst();
    return;
  }
  FPIP.place();
  IOPlacer::destroyInst();

  MacroPlacer::initInst();
  FPMP.place();
  MacroPlacer::destroyInst();

  PhyPlacer::initInst();
  FPPP.place();
  PhyPlacer::destroyInst();

  PDNGenerator::initInst();
  FPPG.generate();
  PDNGenerator::destroyInst();

  _run_failed = false;
  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void FPInterface::runSimpleFP()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  DieBuilder::initInst();
  FPDB.build();
  DieBuilder::destroyInst();

  IOPlacer::initInst();
  if (FPDM.getConfig().io_placer_mode == IOPlacerMode::kIOCell && !FPIP.planDieForIO()) {
    FPLOG.error(Loc::current(), "IO cell capacity check failed; stopping simple floorplan.");
    _run_failed = true;
    IOPlacer::destroyInst();
    return;
  }
  FPIP.placeAuto();
  IOPlacer::destroyInst();

  outputFloorplan();
  outputIOPinList();

  FPLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void FPInterface::destroyFP()
{
  Monitor monitor;
  FPLOG.info(Loc::current(), "Starting...");

  if (_run_failed) {
    FPLOG.warn(Loc::current(), "Skipping floorplan output because the run failed.");
    DieBuilder::destroyInst();
    IOPlacer::destroyInst();
    MacroPlacer::destroyInst();
    PhyPlacer::destroyInst();
    PDNGenerator::destroyInst();
  } else {
    FPDM.output();
  }
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

void FPInterface::debugInputMacro(std::map<std::string, std::any> config_map)
{
  const std::string path = FPUTIL.getConfigValue<std::string>(config_map, "-path", "");
  if (path.empty()) {
    FPLOG.error(Loc::current(), "The macro placement file path is empty!");
    return;
  }
  auto* design = dmInst->get_idb_design();
  auto* layout = dmInst->get_idb_layout();
  if (design == nullptr || layout == nullptr || design->get_instance_list() == nullptr) {
    FPLOG.error(Loc::current(), "Failed to get the IDB design or layout!");
    return;
  }
  int32_t dbu = design->get_units() ? design->get_units()->get_micron_dbu() : 0;
  if (dbu <= 0 && layout->get_units()) {
    dbu = layout->get_units()->get_micron_dbu();
  }
  if (dbu <= 0) {
    FPLOG.error(Loc::current(), "Failed to get a valid micron DBU from IDB!");
    return;
  }
  std::ifstream input(path);
  if (!input.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open macro placement file '", path, "'!");
    return;
  }
  int placed = 0;
  std::string line;
  int line_num = 0;
  while (std::getline(input, line)) {
    ++line_num;
    std::istringstream stream(line);
    std::string name, orient;
    double x = 0.0, y = 0.0;
    if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos || line[0] == '#') continue;
    if (!(stream >> name >> x >> y >> orient)) {
      FPLOG.error(Loc::current(), "Malformed macro placement at line ", line_num, " in '", path, "'.");
    }
    auto* instance = design->get_instance_list()->find_instance(name);
    if (instance == nullptr || instance->get_cell_master() == nullptr || !instance->get_cell_master()->is_block()) {
      FPLOG.error(Loc::current(), "Unknown or non-block macro '", name, "' at line ", line_num, " in '", path, "'.");
    }
    if (!dmInst->placeInst(name, static_cast<int32_t>(std::llround(x * dbu)),
                           static_cast<int32_t>(std::llround(y * dbu)), orient, "", "", "fixed", false)) {
      FPLOG.error(Loc::current(), "Failed to place macro '", name, "' at line ", line_num, " in '", path, "'.");
    }
    ++placed;
  }
  FPLOG.info(Loc::current(), "Loaded ", placed, " macro placement(s) from '", path, "'.");
}

#endif

#endif

void FPInterface::inputMacroPlacement(const std::string& macro_place_file_path)
{
  if (macro_place_file_path.empty()) {
    return;
  }

  Database& database = FPDM.getDatabase();
  std::map<std::string, Instance*> instance_map;
  int32_t macro_num = 0;
  for (Instance& instance : database.get_instance_list()) {
    instance_map[instance.get_name()] = &instance;
    macro_num += instance.get_macro() ? 1 : 0;
  }
  int32_t micron_dbu = database.get_micron_dbu();
  if (micron_dbu <= 0) {
    FPLOG.error(Loc::current(), "Cannot load macro placement with an invalid micron DBU value: ", micron_dbu, ".");
  }

  std::ifstream macro_place_file(macro_place_file_path);
  if (!macro_place_file.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open macro placement file '", macro_place_file_path, "'!");
  }

  std::set<std::string> placed_macro_names;
  std::vector<MacroPlacement> placement_list;
  int32_t line_num = 0;
  std::string line;
  while (std::getline(macro_place_file, line)) {
    ++line_num;
    size_t first_character = line.find_first_not_of(" \t\r");
    if (first_character == std::string::npos || line[first_character] == '#') {
      continue;
    }

    std::istringstream line_stream(line);
    std::string command;
    line_stream >> command;
    if (command == "setInstancePlacementStatus") {
      std::string status, status_value, name_option, status_name, trailing_token;
      if (!(line_stream >> status >> status_value >> name_option >> status_name)
          || status != "-status" || status_value != "fixed" || name_option != "-name"
          || (line_stream >> trailing_token && trailing_token[0] != '#')) {
        FPLOG.error(Loc::current(), "Malformed placement status at line ", line_num, " in '", macro_place_file_path, "'.");
      }
      if (!placed_macro_names.contains(status_name)) {
        FPLOG.error(Loc::current(), "Placement status refers to macro before placeInstance at line ", line_num, " in '",
                    macro_place_file_path, "'.");
      }
      continue;
    }
    if (command != "placeInstance") {
      FPLOG.error(Loc::current(), "Unsupported command '", command, "' at line ", line_num, " in '", macro_place_file_path, "'.");
    }
    std::string instance_name;
    std::string orient_name;
    std::string trailing_token;
    double x_micron = 0.0;
    double y_micron = 0.0;
    if (!(line_stream >> instance_name >> x_micron >> y_micron >> orient_name)
        || (line_stream >> trailing_token && trailing_token[0] != '#')) {
      FPLOG.error(Loc::current(), "Malformed macro placement at line ", line_num, " in '", macro_place_file_path,
                  "'. Expected: instance_name x_micron y_micron orientation.");
    }
    if (!std::isfinite(x_micron) || !std::isfinite(y_micron)) {
      FPLOG.error(Loc::current(), "Non-finite macro coordinate at line ", line_num, " in '", macro_place_file_path, "'!");
    }

    auto instance_iter = instance_map.find(instance_name);
    if (instance_iter == instance_map.end()) {
      FPLOG.error(Loc::current(), "Unknown macro '", instance_name, "' at line ", line_num, " in '", macro_place_file_path, "'!");
    }
    Instance& instance = *instance_iter->second;
    if (!instance.get_macro()) {
      FPLOG.error(Loc::current(), "Instance '", instance_name, "' at line ", line_num, " in '", macro_place_file_path,
                  "' is not a block macro!");
    }
    if (!placed_macro_names.insert(instance_name).second) {
      FPLOG.error(Loc::current(), "Duplicate macro '", instance_name, "' at line ", line_num, " in '", macro_place_file_path, "'!");
    }

    static const std::map<std::string, std::string> orient_map = {
        {"R0", "N"}, {"R90", "W"}, {"R180", "S"}, {"R270", "E"},
        {"MX", "FS"}, {"MY", "FN"}, {"MX90", "FW"}, {"MY90", "FE"}};
    auto orient_iter = orient_map.find(orient_name);
    if (orient_iter == orient_map.end()) {
      FPLOG.error(Loc::current(), "Unsupported macro orientation '", orient_name, "' at line ", line_num, " in '", macro_place_file_path,
                  "'!");
    }
    PlacementOrientation orient = GetPlacementOrientationByName()(orient_iter->second);
    if (orient == PlacementOrientation::kNone) {
      FPLOG.error(Loc::current(), "Unsupported macro orientation '", orient_name, "' at line ", line_num, " in '", macro_place_file_path,
                  "'!");
    }

    double x_dbu = std::round(x_micron * micron_dbu);
    double y_dbu = std::round(y_micron * micron_dbu);
    constexpr double kMinCoordinate = static_cast<double>(std::numeric_limits<int32_t>::min());
    constexpr double kMaxCoordinate = static_cast<double>(std::numeric_limits<int32_t>::max());
    double max_dimension = static_cast<double>(std::max(instance.get_width(), instance.get_height()));
    if (x_dbu < kMinCoordinate || x_dbu > kMaxCoordinate || y_dbu < kMinCoordinate || y_dbu > kMaxCoordinate
        || x_dbu + max_dimension > kMaxCoordinate || y_dbu + max_dimension > kMaxCoordinate) {
      FPLOG.error(Loc::current(), "Macro coordinate is outside the supported DBU range at line ", line_num, " in '", macro_place_file_path,
                  "'!");
    }

    placement_list.push_back({&instance, static_cast<int32_t>(x_dbu), static_cast<int32_t>(y_dbu), orient});
  }

  if (static_cast<int32_t>(placement_list.size()) != macro_num) {
    std::string missing_macro_names;
    for (const auto& [instance_name, instance] : instance_map) {
      if (!instance->get_macro() || placed_macro_names.contains(instance_name)) {
        continue;
      }
      missing_macro_names += (missing_macro_names.empty() ? "" : ", ") + instance_name;
    }
    FPLOG.error(Loc::current(), "Macro placement file '", macro_place_file_path, "' contains ", placement_list.size(),
                " unique macro(s), but the design has ", macro_num, ". Missing: ", missing_macro_names, ".");
  }

  for (const MacroPlacement& placement : placement_list) {
    Instance& instance = *placement.instance;
    instance.set_coord(placement.x, placement.y);
    instance.set_orient(placement.orient);
    instance.set_placed(true);
    instance.set_fixed(true);
    instance.set_cover(false);
    instance.set_placement_updated(true);

    idb::IdbCoordinate<int32_t> origin(placement.x, placement.y);
    idb::IdbOrientTransform transform(unwrapPlacementOrientation(placement.orient), &origin, instance.get_width(), instance.get_height());
    idb::IdbRect bounding_rect(placement.x, placement.y, placement.x + instance.get_width(), placement.y + instance.get_height());
    transform.transformRect(&bounding_rect);
    instance.set_bounding_rect(bounding_rect.get_low_x(), bounding_rect.get_low_y(), bounding_rect.get_high_x(), bounding_rect.get_high_y());

    for (InstancePinShape& pin_shape : instance.get_pin_shape_list()) {
      idb::IdbRect pin_rect(pin_shape.get_ll_x() + placement.x, pin_shape.get_ll_y() + placement.y, pin_shape.get_ur_x() + placement.x,
                            pin_shape.get_ur_y() + placement.y);
      transform.transformRect(&pin_rect);
      pin_shape.set_rect(pin_rect.get_low_x(), pin_rect.get_low_y(), pin_rect.get_high_x(), pin_rect.get_high_y());
    }
  }

  FPLOG.info(Loc::current(), "Loaded ", placement_list.size(), " fixed macro placement(s) from '", macro_place_file_path,
             "' directly into the iFP database.");
}

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
  config.input_macro_path.clear();
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
  config.io_placer_mode = IOPlacerMode::kIOPin;
  config.io_pin_placement_mode = PlacementMode::kAuto;
  config.io_pin_layer_name_list.clear();
  config.input_io_pin_path.clear();
  config.pg_connect_list.clear();
  config.core_power_net.clear();
  config.core_ground_net.clear();
  config.pg_ring = {};
  config.pg_io_core_connect = {};
  config.pg_required_net_list.clear();
  config.pg_connect_pads = false;
  config.pg_require_connected_pins = true;
  config.pg_access_layer_list.clear();
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
  config.phy_orientations.clear();
  config.corner_cell_names.clear();

  std::filesystem::path config_file_path = std::filesystem::absolute(FPUTIL.getConfigValue<std::string>(config_map, "-config", ""));
  std::ifstream config_file_stream(config_file_path);
  if (!config_file_stream.is_open()) {
    FPLOG.error(Loc::current(), "Failed to open config file '", config_file_path.string(), "'!");
  }

  nlohmann::json config_json;
  config_file_stream >> config_json;
  std::filesystem::path config_directory_path = config_file_path.parent_path();

  nlohmann::json& ifp_json = config_json["ifp"];
  checkKeys(ifp_json, {"temp_directory_path", "thread_number"}, "ifp");
  config.temp_directory_path = FPUTIL.getAbsolutePath(config_directory_path, ifp_json["temp_directory_path"].get<std::string>());
  config.thread_number = std::max(ifp_json["thread_number"].get<int32_t>(), 1);

  nlohmann::json& macro_placer_json = config_json["macro_placer"];
  checkKeys(macro_placer_json, {"mode", "file_path", "macro_placement_halo", "macro_routing_halo"}, "macro_placer");
  // Older ECC configurations write mode; placement is now selected by file_path.
  if (macro_placer_json.contains("mode")) {
    std::string mode = macro_placer_json["mode"].get<std::string>();
    if (mode != "auto" && mode != "file") {
      FPLOG.error(Loc::current(), "Unknown macro_placer.mode '", mode, "'. Expected auto or file.");
    }
  }
  std::string macro_file_path = macro_placer_json.value("file_path", "");
  config.input_macro_path = macro_file_path.empty() ? "" : FPUTIL.getAbsolutePath(config_directory_path, macro_file_path);
  config.macro_placement_halo = macro_placer_json["macro_placement_halo"].get<double>();
  config.macro_routing_halo = macro_placer_json["macro_routing_halo"].get<double>();
  if (config.macro_placement_halo < 0.0) {
    FPLOG.error(Loc::current(), "The macro placement halo must not be negative!");
  }

  nlohmann::json& die_builder_json = config_json["die_builder"];
  checkKeys(die_builder_json, {"mode", "site_name", "margin", "die_util", "die_size"}, "die_builder");
  config.die_mode = GetDieModeByName()(die_builder_json["mode"].get<std::string>());
  config.die_site_name = die_builder_json["site_name"].get<std::string>();
  nlohmann::json& die_margin_json = die_builder_json["margin"];
  checkKeys(die_margin_json, {"left_micron", "right_micron", "top_micron", "bottom_micron"}, "die_builder.margin");
  config.die_margin_left_micron = die_margin_json["left_micron"].get<double>();
  config.die_margin_right_micron = die_margin_json["right_micron"].get<double>();
  config.die_margin_top_micron = die_margin_json["top_micron"].get<double>();
  config.die_margin_bottom_micron = die_margin_json["bottom_micron"].get<double>();
  if (config.die_mode == DieMode::kDieUtil) {
    nlohmann::json& die_util_json = die_builder_json["die_util"];
    checkKeys(die_util_json, {"aspect_ratio", "utilization"}, "die_builder.die_util");
    config.die_aspect_ratio = die_util_json["aspect_ratio"].get<double>();
    config.die_utilization = die_util_json["utilization"].get<double>();
  } else if (config.die_mode == DieMode::kDieSize) {
    nlohmann::json& die_size_json = die_builder_json["die_size"];
    checkKeys(die_size_json, {"width_micron", "height_micron"}, "die_builder.die_size");
    config.die_width_micron = die_size_json["width_micron"].get<double>();
    config.die_height_micron = die_size_json["height_micron"].get<double>();
  }

  nlohmann::json& io_placer_json = config_json["io_placer"];
  checkKeys(io_placer_json, {"mode", "file_path", "io_layer_list", "io_pin", "io_cell"}, "io_placer");
  if (io_placer_json.contains("io_pin")) {
    checkKeys(io_placer_json["io_pin"], {"mode", "file_path", "io_layer_list"}, "io_placer.io_pin");
  }
  const std::string io_mode = io_placer_json.value("mode", "io_pin");
  const bool legacy_io_pin_schema = io_mode == "auto" || io_mode == "file";
  config.io_placer_mode = legacy_io_pin_schema ? IOPlacerMode::kIOPin : GetIOPlacerModeByName()(io_mode);

  // Keep the old flat schema working while accepting the independent io_pin/io_cell schema.
  nlohmann::json io_pin_json = io_placer_json.contains("io_pin")
                                   ? io_placer_json["io_pin"]
                                   : (legacy_io_pin_schema ? io_placer_json : nlohmann::json::object());
  config.io_pin_placement_mode = GetPlacementModeByName()(io_pin_json.value("mode", "auto"));
  std::string io_pin_file_path = io_pin_json.value("file_path", "");
  config.input_io_pin_path = io_pin_file_path.empty() ? "" : FPUTIL.getAbsolutePath(config_directory_path, io_pin_file_path);
  if (io_pin_json.contains("io_layer_list")) {
    for (const nlohmann::json& layer_name_json : io_pin_json["io_layer_list"]) {
      config.io_pin_layer_name_list.push_back(layer_name_json.get<std::string>());
    }
  }

  if (io_placer_json.contains("io_cell")) {
    nlohmann::json& io_cell_json = io_placer_json["io_cell"];
    checkKeys(io_cell_json, {"mode", "file_path", "definition_file_path", "power", "auto_resize"}, "io_placer.io_cell");
    if (io_cell_json.contains("power")) {
      checkKeys(io_cell_json["power"], {"target_group_spacing"}, "io_placer.io_cell.power");
    }
    if (io_cell_json.contains("auto_resize")) {
      checkKeys(io_cell_json["auto_resize"], {"enable"}, "io_placer.io_cell.auto_resize");
    }
    config.io_cell_mode = GetPlacementModeByName()(io_cell_json.value("mode", "auto"));
    std::string io_cell_file_path = io_cell_json.value("file_path", "");
    std::string definition_file_path = io_cell_json.value("definition_file_path", "");
    config.io_cell_file = io_cell_file_path.empty() ? "" : FPUTIL.getAbsolutePath(config_directory_path, io_cell_file_path);
    config.io_cell_definition_file = definition_file_path.empty() ? "" : FPUTIL.getAbsolutePath(config_directory_path, definition_file_path);
    nlohmann::json power_json = io_cell_json.value("power", nlohmann::json::object());
    config.io_cell_power_spacing_micron = power_json.value("target_group_spacing", 1000.0);
    nlohmann::json auto_resize_json = io_cell_json.value("auto_resize", nlohmann::json::object());
    config.io_cell_auto_resize = auto_resize_json.value("enable", true);
  }

  nlohmann::json& phy_placer_json = config_json["phy_placer"];
  checkKeys(phy_placer_json, {"orientations", "corner_cells", "well_tap", "side_endcap", "edge_endcap", "boundary_tap"}, "phy_placer");
  const auto orientations = phy_placer_json.value("orientations", nlohmann::json::object());
  for (const auto& [role, mappings] : orientations.items()) {
    for (const auto& [row, orient] : mappings.items()) {
      const auto row_orient = GetPlacementOrientationByName()(row);
      const auto cell_orient = GetPlacementOrientationByName()(orient.get<std::string>());
      if (row_orient == PlacementOrientation::kNone || cell_orient == PlacementOrientation::kNone) {
        FPLOG.error(Loc::current(), "Invalid physical-cell orientation for role '", role, "'.");
      }
      config.phy_orientations[role][row_orient] = cell_orient;
    }
  }
  config.corner_cell_names = phy_placer_json.value("corner_cells", std::map<std::string, std::string>{});
  nlohmann::json& well_tap_json = phy_placer_json["well_tap"];
  checkKeys(well_tap_json, {"cell_name", "distance_micron"}, "phy_placer.well_tap");
  config.tapcell_name = well_tap_json["cell_name"].get<std::string>();
  config.tap_distance_micron = well_tap_json["distance_micron"].get<double>();
  nlohmann::json& side_endcap_json = phy_placer_json["side_endcap"];
  checkKeys(side_endcap_json, {"left_cell_name", "right_cell_name"}, "phy_placer.side_endcap");
  config.left_endcap_name = side_endcap_json["left_cell_name"].get<std::string>();
  config.right_endcap_name = side_endcap_json["right_cell_name"].get<std::string>();
  nlohmann::json& edge_endcap_json = phy_placer_json["edge_endcap"];
  checkKeys(edge_endcap_json, {"top_cell_name_list", "bottom_cell_name_list"}, "phy_placer.edge_endcap");
  for (nlohmann::json& cell_name_json : edge_endcap_json["top_cell_name_list"]) {
    config.top_endcap_name_list.push_back(cell_name_json.get<std::string>());
  }
  for (nlohmann::json& cell_name_json : edge_endcap_json["bottom_cell_name_list"]) {
    config.bottom_endcap_name_list.push_back(cell_name_json.get<std::string>());
  }
  nlohmann::json& boundary_tap_json = phy_placer_json["boundary_tap"];
  checkKeys(boundary_tap_json, {"top_cell_name_list", "bottom_cell_name_list", "rule_micron"}, "phy_placer.boundary_tap");
  for (nlohmann::json& cell_name_json : boundary_tap_json["top_cell_name_list"]) {
    config.top_boundary_tap_name_list.push_back(cell_name_json.get<std::string>());
  }
  for (nlohmann::json& cell_name_json : boundary_tap_json["bottom_cell_name_list"]) {
    config.bottom_boundary_tap_name_list.push_back(cell_name_json.get<std::string>());
  }
  config.boundary_tap_rule_micron = boundary_tap_json["rule_micron"].get<double>();

  nlohmann::json& pdn_generator_json = config_json["pdn_generator"];
  checkKeys(pdn_generator_json,
            {"global_connect", "rail", "stripe", "connect_layers", "core_power_net", "core_ground_net", "ring",
             "io_core_connect", "required_nets", "connect_pads", "require_connected_pins", "access_layers"},
            "pdn_generator");
  config.core_power_net = pdn_generator_json.value("core_power_net", "");
  config.core_ground_net = pdn_generator_json.value("core_ground_net", "");
  config.pg_required_net_list = pdn_generator_json.value("required_nets", std::vector<std::string>{});
  config.pg_connect_pads = pdn_generator_json.value("connect_pads", false);
  config.pg_require_connected_pins = pdn_generator_json.value("require_connected_pins", true);
  config.pg_access_layer_list = pdn_generator_json.value("access_layers", std::vector<std::string>{});
  if (pdn_generator_json.contains("ring")) {
    const auto& ring = pdn_generator_json["ring"];
    checkKeys(ring, {"nets", "horizontal_layer", "vertical_layer", "width_micron", "spacing_micron", "offset_micron"}, "pdn_generator.ring");
    config.pg_ring.nets = ring.at("nets").get<std::vector<std::string>>();
    config.pg_ring.horizontal_layer = ring.at("horizontal_layer").get<std::string>();
    config.pg_ring.vertical_layer = ring.at("vertical_layer").get<std::string>();
    config.pg_ring.width_micron = ring.at("width_micron").get<double>();
    config.pg_ring.spacing_micron = ring.at("spacing_micron").get<double>();
    config.pg_ring.offset_micron = ring.at("offset_micron").get<double>();
  }
  if (pdn_generator_json.contains("io_core_connect")) {
    const auto& io_core = pdn_generator_json["io_core_connect"];
    checkKeys(io_core, {"enable", "nets", "pin_layers", "min_contacts_per_port", "bridge_min_spacing_micron", "fail_on_unresolved"},
              "pdn_generator.io_core_connect");
    config.pg_io_core_connect.enabled = io_core.value("enable", true);
    config.pg_io_core_connect.nets = io_core.value("nets", std::vector<std::string>{});
    config.pg_io_core_connect.pin_layers = io_core.value("pin_layers", std::vector<std::string>{});
    config.pg_io_core_connect.fail_on_unresolved = io_core.value("fail_on_unresolved", false);
    config.pg_io_core_connect.min_contacts_per_port = io_core.value("min_contacts_per_port", 1);
    config.pg_io_core_connect.bridge_min_spacing_micron = io_core.value("bridge_min_spacing_micron", 0.0);
  }
  if (config.pg_io_core_connect.enabled) {
    if (config.pg_io_core_connect.min_contacts_per_port < 1) {
      FPLOG.error(Loc::current(), "io_core_connect.min_contacts_per_port must be at least 1.");
    }
    if (!std::isfinite(config.pg_io_core_connect.bridge_min_spacing_micron)
        || config.pg_io_core_connect.bridge_min_spacing_micron < 0.0) {
      FPLOG.error(Loc::current(), "io_core_connect.bridge_min_spacing_micron must be finite and non-negative.");
    }
    std::set<std::string> unique_nets(config.pg_io_core_connect.nets.begin(), config.pg_io_core_connect.nets.end());
    if (!config.pg_io_core_connect.nets.empty() && unique_nets.size() != config.pg_io_core_connect.nets.size()) {
      FPLOG.error(Loc::current(), "io_core_connect.nets contains duplicate networks.");
    }
  }
  for (nlohmann::json& pg_connect_json : pdn_generator_json["global_connect"]) {
    checkKeys(pg_connect_json, {"net_name", "instance_pin_name", "is_power", "instance_pattern", "master_pattern"},
              "pdn_generator.global_connect[]");
    PGGlobalConnect pg_connect;
    pg_connect.set_net_name(pg_connect_json["net_name"].get<std::string>());
    pg_connect.set_pin_name(pg_connect_json["instance_pin_name"].get<std::string>());
    pg_connect.set_net_type(pg_connect_json["is_power"].get<bool>() ? PGNetType::kPower : PGNetType::kGround);
    pg_connect.set_instance_pattern(pg_connect_json.value("instance_pattern", "*"));
    pg_connect.set_master_pattern(pg_connect_json.value("master_pattern", "*"));
    config.pg_connect_list.push_back(pg_connect);
  }
  for (nlohmann::json& pg_rail_json : pdn_generator_json["rail"]) {
    checkKeys(pg_rail_json, {"routing_layer_name", "width_micron"}, "pdn_generator.rail[]");
    PGRail pg_rail;
    pg_rail.set_layer_name(pg_rail_json["routing_layer_name"].get<std::string>());
    pg_rail.set_width_micron(pg_rail_json["width_micron"].get<double>());
    config.pg_rail_list.push_back(pg_rail);
  }
  for (nlohmann::json& pg_stripe_json : pdn_generator_json["stripe"]) {
    checkKeys(pg_stripe_json, {"routing_layer_name", "width_micron", "pitch_micron", "offset_micron"}, "pdn_generator.stripe[]");
    PGStripe pg_stripe;
    pg_stripe.set_layer_name(pg_stripe_json["routing_layer_name"].get<std::string>());
    pg_stripe.set_width_micron(pg_stripe_json["width_micron"].get<double>());
    pg_stripe.set_pitch_micron(pg_stripe_json["pitch_micron"].get<double>());
    pg_stripe.set_offset_micron(pg_stripe_json["offset_micron"].get<double>());
    config.pg_stripe_list.push_back(pg_stripe);
  }
  for (nlohmann::json& layer_connect_json : pdn_generator_json["connect_layers"]) {
    checkKeys(layer_connect_json, {"bottom_routing_layer_name", "top_routing_layer_name"}, "pdn_generator.connect_layers[]");
    PGLayerPair pg_layer_pair;
    pg_layer_pair.set_first_layer_name(layer_connect_json["bottom_routing_layer_name"].get<std::string>());
    pg_layer_pair.set_second_layer_name(layer_connect_json["top_routing_layer_name"].get<std::string>());
    config.pg_layer_pair_list.push_back(pg_layer_pair);
  }

  omp_set_num_threads(config.thread_number);
}

void FPInterface::wrapDatabase()
{
  FPDM.getDatabase().get_instance_rename_map().clear();
  wrapDBInfo();
  wrapMicronDBU();
  wrapManufactureGrid();
  wrapCellArea();
  wrapSiteMap();
  wrapCellMasterMap();
  wrapRoutingLayerList();
  wrapInstanceList();
  inputMacroPlacement(FPDM.getConfig().input_macro_path);
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
    // The die utilization target describes core logic, not the IO ring.  IO
    // pads, fillers and corners are placed around the die edge and would
    // otherwise inflate the area used by die_util, especially across repeated
    // runs where the generated ring cells are already present in the IDB.
    const bool corner_master = idb_cell_master->get_site() != nullptr && idb_cell_master->get_site()->is_corner_site();
    if (idb_cell_master->is_pad() || idb_cell_master->is_pad_filler() || idb_cell_master->is_spacer() || corner_master) {
      continue;
    }
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
    cell_master.set_symmetry_x(idb_cell_master->is_symmetry_x());
    cell_master.set_symmetry_y(idb_cell_master->is_symmetry_y());
    cell_master.set_pad_filler(idb_cell_master->is_pad_filler());
    cell_master.set_spacer(idb_cell_master->is_spacer());
    cell_master.set_corner(idb_cell_master->get_site() != nullptr && idb_cell_master->get_site()->is_corner_site());
    for (idb::IdbTerm* term : idb_cell_master->get_term_list()) {
      const bool pg = term->is_power() || term->is_ground();
      if (pg) {
        cell_master.get_pg_pin_types()[term->get_name()] = term->is_power() ? PGNetType::kPower : PGNetType::kGround;
      }
      int32_t port_idx = 0;
      for (idb::IdbPort* port : term->get_port_list()) {
        for (idb::IdbLayerShape* shape : port->get_layer_shape()) {
          if (!shape->get_layer()->is_routing()) {
            continue;
          }
          for (idb::IdbRect* rect : shape->get_rect_list()) {
            InstancePinShape pin_shape;
            pin_shape.set_pin_name(term->get_name());
            pin_shape.set_layer_name(shape->get_layer()->get_name());
            pin_shape.set_port_idx(port_idx);
            pin_shape.set_core_port(port->get_port_class() == idb::IdbPortClass::kCore);
            pin_shape.set_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
            (pg ? cell_master.get_pg_pin_shapes() : cell_master.get_obstruction_shapes()).push_back(pin_shape);
          }
        }
        ++port_idx;
      }
    }
    // IDB stores OBS rectangles in the same normalized frame as the pin
    // shapes (ORIGIN already applied); adding it again would shift the
    // obstruction by another ORIGIN.
    for (auto* obs : idb_cell_master->get_obs_list()) {
      for (auto* obs_layer : obs->get_obs_layer_list()) {
        auto* shape = obs_layer->get_shape();
        if (shape->get_layer() == nullptr || !shape->get_layer()->is_routing()) {
          continue;
        }
        for (auto* rect : shape->get_rect_list()) {
          InstancePinShape obstruction;
          obstruction.set_layer_name(shape->get_layer()->get_name());
          obstruction.set_rect(rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y());
          cell_master.get_obstruction_shapes().push_back(obstruction);
        }
      }
    }
    cell_master_map[cell_master.get_name()] = cell_master;
  }
}

std::vector<InstancePinShape> FPInterface::getPlacedShapes(const Instance& instance, const std::vector<InstancePinShape>& shapes)
{
  auto placed = shapes;
  idb::IdbCoordinate<int32_t> origin(instance.get_x(), instance.get_y());
  idb::IdbOrientTransform transform(unwrapPlacementOrientation(instance.get_orient()), &origin, instance.get_width(), instance.get_height());
  for (auto& shape : placed) {
    idb::IdbRect rect(shape.get_ll_x() + instance.get_x(), shape.get_ll_y() + instance.get_y(),
                      shape.get_ur_x() + instance.get_x(), shape.get_ur_y() + instance.get_y());
    transform.transformRect(&rect);
    shape.set_rect(rect.get_low_x(), rect.get_low_y(), rect.get_high_x(), rect.get_high_y());
  }
  return placed;
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
    routing_layer.set_max_width(idb_routing_layer->get_max_width());
    routing_layer.set_min_area(std::max(idb_routing_layer->get_area(), 0));
    routing_layer.set_prefer_track_offset(idb_routing_layer->get_offset_prefer());
    routing_layer.set_spacing(idb_routing_layer->get_spacing(0));
    int32_t io_spacing = std::max(0, routing_layer.get_spacing());
    for (const auto* rule : idb_routing_layer->get_spacing_list()->get_spacing_list()) {
      io_spacing = std::max(io_spacing, rule->get_min_spacing());
      if (rule->get_has_parallel_edge()) io_spacing = std::max(io_spacing, rule->get_par_space());
    }
    const auto table = idb_routing_layer->get_spacing_table();
    if (table && table->is_parallel()) {
      for (const auto& row : table->get_parallel()->get_spacing_table()) {
        for (const int32_t value : row) io_spacing = std::max(io_spacing, value);
      }
    }
    for (const auto& rule : idb_routing_layer->get_lef58_spacing_eol_list()) {
      io_spacing = std::max(io_spacing, rule->get_eol_space());
      if (const auto edge = rule->get_parallel_edge()) io_spacing = std::max(io_spacing, edge->get_par_space());
      if (const auto ends = rule->get_end_to_end()) io_spacing = std::max(io_spacing, ends->get_end_to_end_space());
    }
    routing_layer.set_io_spacing(io_spacing);
    int32_t enclosed_area = 0;
    for (const auto& rule : idb_routing_layer->get_min_enclose_area_list()->get_min_area_list()) {
      enclosed_area = std::max(enclosed_area, rule._area);
    }
    routing_layer.set_min_enclosed_area(enclosed_area);
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
    instance.set_from_netlist(true);
    instance.set_width(idb_instance->get_cell_master()->get_width());
    instance.set_height(idb_instance->get_cell_master()->get_height());
    instance.set_macro(idb_instance->get_cell_master()->is_block());
    instance.set_orient(wrapPlacementOrientation(idb_instance->get_orient()));
    instance.set_fixed(idb_instance->is_fixed());
    instance.set_cover(idb_instance->is_cover());
    instance.set_placed(idb_instance->has_placed());
    if (idb_instance->has_placed()) {
      instance.set_coord(idb_instance->get_coordinate()->get_x(), idb_instance->get_coordinate()->get_y());
      idb_instance->set_bounding_box();
      instance.set_bounding_rect(idb_instance->get_bounding_box()->get_low_x(), idb_instance->get_bounding_box()->get_low_y(),
                                 idb_instance->get_bounding_box()->get_high_x(), idb_instance->get_bounding_box()->get_high_y());
    }

    if (idb_instance->get_cell_master()->is_io_cell()) {
      // IO Cell placement is regenerated by IOCellPlacer. Keep pin shapes in
      // master-local coordinates even when the input DEF already placed it.
      wrapUnplacedMacroPinShapeList(idb_instance, instance);
    } else if (instance.get_macro()) {
      // An external placement file transforms master-local pin shapes after this
      // function. Without one, IDB already owns the absolute placed pin shapes.
      if (FPDM.getConfig().input_macro_path.empty() && idb_instance->has_placed()) {
        wrapPlacedMacroPinShapeList(idb_instance, instance);
      } else {
        wrapUnplacedMacroPinShapeList(idb_instance, instance);
      }
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
  std::map<std::string, Instance*> instance_map;
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    instance_map[instance.get_name()] = &instance;
  }

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
      idb::IdbInstance* idb_instance = idb_pin->get_instance();
      net_pin.set_instance_name(idb_instance->get_name());
      net_pin.set_pin_name(idb_pin->get_pin_name());
      net_pin.set_offset_x(idb_pin->get_term()->get_average_position().get_x());
      net_pin.set_offset_y(idb_pin->get_term()->get_average_position().get_y());
      Instance& instance = *instance_map.at(idb_instance->get_name());
      if (instance.get_macro()) {
        idb::IdbCoordinate<int32_t> origin(instance.get_x(), instance.get_y());
        idb::IdbCoordinate<int32_t> pin_coord(instance.get_x() + net_pin.get_offset_x(), instance.get_y() + net_pin.get_offset_y());
        idb::IdbOrientTransform transform(unwrapPlacementOrientation(instance.get_orient()), &origin, instance.get_width(), instance.get_height());
        transform.transformCoordinate(&pin_coord);
        net_pin.set_coord(pin_coord.get_x(), pin_coord.get_y());
        net_pin.set_placed(instance.get_placed());
      } else {
        net_pin.set_coord(idb_pin->get_average_coordinate()->get_x(), idb_pin->get_average_coordinate()->get_y());
        net_pin.set_placed(idb_instance->has_placed());
      }
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

    // IOCellPlacer resolves full (instance, terminal) bindings from the net.
    // The legacy helper can return a PLL/SRAM on a shared supply net.
    if (FPDM.getConfig().io_placer_mode != IOPlacerMode::kIOCell && idb_pin->get_net() != nullptr) {
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
  outputInstanceNameChanges();
  outputFloorplan();
  outputIOPinList();
  outputIOInstancePlacement();
  outputMacroPlacement();
  outputMacroHalo();
  outputMacroRouteHalo();
  outputNewInstanceList();
  outputPGNetList();
  outputPGSegmentList();
}

void FPInterface::outputInstanceNameChanges()
{
  const auto& rename_map = FPDM.getDatabase().get_instance_rename_map();
  if (rename_map.empty()) {
    return;
  }
  idb::IdbInstanceList* instance_list = dmInst->get_idb_design()->get_instance_list();
  std::vector<std::pair<std::string, std::string>> renames(rename_map.begin(), rename_map.end());
  std::vector<std::string> temporary_names;
  temporary_names.reserve(renames.size());
  for (size_t index = 0; index < renames.size(); ++index) {
    std::string temporary = "__IFP_IO_POWER_RENAME_TMP_" + std::to_string(index);
    while (instance_list->contains(temporary)) {
      temporary += "_";
    }
    temporary_names.push_back(temporary);
    if (!instance_list->rename_instance(renames[index].first, temporary)) {
      FPLOG.error(Loc::current(), "Failed to rename IDB instance '", renames[index].first, "'.");
      return;
    }
  }
  for (size_t index = 0; index < renames.size(); ++index) {
    if (!instance_list->rename_instance(temporary_names[index], renames[index].second)) {
      FPLOG.error(Loc::current(), "Failed to rename IDB instance '", renames[index].first, "' to '", renames[index].second, "'.");
      return;
    }
  }
  FPDM.getDatabase().get_instance_rename_map().clear();
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
  for (idb::IdbLayer* idb_layer : idb_layout->get_layers()->get_routing_layers()) {
    dynamic_cast<idb::IdbLayerRouting*>(idb_layer)->get_track_grid_list().clear();
  }
  idb_layout->get_track_grid_list()->reset();

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
    idb::IdbNet* shadow_net = idb_design->get_net_list()->find_net(pg_net.get_name());
    bool remove_shadow_net = false;
    if (shadow_net != nullptr) {
      // A NETS entry can be dropped only when it carries no routing and all of
      // its terminals are already owned by the PG net.  Verilog often connects
      // PAD/PLL supply pins as ordinary wires; leaving both NETS and SPECIALNETS
      // would export the same connectivity twice.
      const std::set<std::pair<std::string, std::string>> pg_terminals(pg_net.get_instance_pins().begin(),
                                                                      pg_net.get_instance_pins().end());
      bool covered = shadow_net->get_segment_num() == 0;
      if (covered && shadow_net->get_instance_pin_list() != nullptr) {
        for (idb::IdbPin* pin : shadow_net->get_instance_pin_list()->get_pin_list()) {
          idb::IdbInstance* instance = pin == nullptr ? nullptr : pin->get_instance();
          if (instance == nullptr || !pg_terminals.contains({instance->get_name(), pin->get_pin_name()})) {
            covered = false;
            break;
          }
        }
      }
      if (covered && shadow_net->get_io_pins() != nullptr && !shadow_net->get_io_pins()->get_pin_list().empty()) {
        // Top-level PG PINS are removed by outputIOPinList; any that survived
        // would lose their only connectivity if the NETS entry were dropped.
        covered = false;
      }
      if (!covered) {
        FPLOG.error(Loc::current(), "NETS entry '", pg_net.get_name(),
                    "' duplicates SPECIALNETS and is not empty; refusing to export ambiguous DEF.");
        _run_failed = true;
        continue;
      }
      remove_shadow_net = true;
    }
    idb::IdbConnectType connect_type = idb::IdbConnectType::kNone;
    if (pg_net.get_type() == PGNetType::kPower) {
      connect_type = idb::IdbConnectType::kPower;
    } else if (pg_net.get_type() == PGNetType::kGround) {
      connect_type = idb::IdbConnectType::kGround;
    }
    idb::IdbSpecialNet* idb_special_net = idb_design->createOrFindSpecialNet(pg_net.get_name(), connect_type);
    if (idb_special_net == nullptr) {
      FPLOG.error(Loc::current(), "Cannot create PG net '", pg_net.get_name(), "'.");
    }
    // Only rules proven to apply to every instance may be exported as wildcards.
    idb_special_net->get_pin_string_list().clear();
    for (std::string& instance_pin_name : pg_net.get_instance_pin_name_list()) {
      if (std::find(idb_special_net->get_pin_string_list().begin(), idb_special_net->get_pin_string_list().end(), instance_pin_name)
          == idb_special_net->get_pin_string_list().end()) {
        idb_special_net->add_pin_string(instance_pin_name);
      }
    }
    std::vector<idb::IdbPin*> instance_pins;
    instance_pins.reserve(pg_net.get_instance_pins().size());
    for (const auto& [instance_name, pin_name] : pg_net.get_instance_pins()) {
      idb::IdbInstance* instance = idb_design->get_instance_list()->find_instance(instance_name);
      idb::IdbPin* pin = instance == nullptr ? nullptr : instance->get_pin_by_term(pin_name);
      if (pin == nullptr) {
        FPLOG.error(Loc::current(), "Missing PG terminal '", instance_name, "/", pin_name, "' while exporting '", pg_net.get_name(), "'.");
      }
      if (pin->get_net() != nullptr && pin->get_net()->get_net_name() != pg_net.get_name()) {
        FPLOG.error(Loc::current(), "PG terminal '", instance_name, "/", pin_name, "' already belongs to '", pin->get_net()->get_net_name(), "'.");
      }
      instance_pins.push_back(pin);
    }
    if (!idb_design->connectPinsToSpecialNet(instance_pins, idb_special_net)) {
      FPLOG.error(Loc::current(), "Cannot bind instance pins to PG net '", pg_net.get_name(), "'.");
    }
    if (remove_shadow_net) {
      // Bind SPECIALNETS first so removing the NETS entry cannot erase the only
      // remaining connectivity.  removeNetSafe() clears regular-net refs only.
      if (!idb_design->removeNetSafe(pg_net.get_name())) {
        FPLOG.error(Loc::current(), "Failed to remove shadow NETS entry '", pg_net.get_name(), "'.");
        _run_failed = true;
        continue;
      }
      FPLOG.info(Loc::current(), "Removed shadow NETS entry '", pg_net.get_name(), "'.");
    }
    // Power/ground is exported through SPECIALNETS and IO-cell terminals only;
    // do not recreate top-level PINS for it.
  }
  for (idb::IdbSpecialNet* idb_special_net : idb_design->get_special_net_list()->get_net_list()) {
    if (idb_special_net != nullptr && idb_design->get_net_list()->find_net(idb_special_net->get_net_name()) != nullptr) {
      FPLOG.error(Loc::current(), "SPECIALNETS/NETS name conflict: '", idb_special_net->get_net_name(), "'.");
      _run_failed = true;
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
  const Config& config = FPDM.getConfig();
  auto is_pg_pin_name = [&](const std::string& name) {
    if ((!config.core_power_net.empty() && name == config.core_power_net)
        || (!config.core_ground_net.empty() && name == config.core_ground_net)) {
      return true;
    }
    for (const PGGlobalConnect& rule : config.pg_connect_list) {
      if (rule.get_net_name() == name) {
        return true;
      }
    }
    return false;
  };
  for (IOPin& io_pin : FPDM.getDatabase().get_io_pin_list()) {
    idb::IdbPin* idb_pin = idb_design->get_io_pin_list()->find_pin(io_pin.get_name());
    // Power/ground is represented by SPECIALNETS and the IO-cell terminals;
    // it must not appear as a top-level PINS entry.
    if (io_pin.get_special_net() || is_pg_pin_name(io_pin.get_name())
        || (idb_pin != nullptr && idb_pin->is_special_net_pin())) {
      idb_design->removeIoPinSafe(idb_pin);
      continue;
    }
    if (!io_pin.is_updated() || idb_pin == nullptr) {
      continue;
    }

    idb::IdbTerm* idb_term = idb_pin->get_term();
    idb_pin->clear_port_layer_shape();
    idb_term->clear_port_list();
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

    // IO-cell mode: the placed PAD already carries the physical access shape,
    // and the reference flow keeps PINS logical-only. Emitting a port here
    // would duplicate the cell's pin metal at the top level.
    if (config.io_placer_mode != IOPlacerMode::kIOCell) {
      for (IOPort& io_port : io_pin.get_new_port_list()) {
        idb::IdbPort* idb_port = idb_term->add_port();
        idb_port->set_coordinate(io_port.get_x(), io_port.get_y());
        idb_port->set_orient(idb::IdbOrient::kN_R0);
        if (io_port.get_placed()) {
          idb_port->set_placement_status_place();
        }
        idb::IdbLayerShape* idb_layer_shape = idb_port->add_layer_shape();
        idb_layer_shape->set_type_rect();
        idb_layer_shape->set_layer(idb_layout->get_layers()->find_layer(io_port.get_layer_name()));
        idb_layer_shape->add_rect(io_port.get_ll_x(), io_port.get_ll_y(), io_port.get_ur_x(), io_port.get_ur_y());
      }
    }
    idb_pin->set_bounding_box();
    io_pin.get_new_port_list().clear();
    io_pin.set_updated(false);
  }
}

void FPInterface::outputIOInstancePlacement()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (instance.is_delete_instance()) {
      idb_design->removeInstanceSafe(instance.get_name());
      continue;
    }
    if (!instance.is_placement_updated() || instance.get_macro()) {
      continue;
    }
    // Reused ring cells may have been re-mastered in the local model (for
    // example a filler index that now resolves to a narrower master).  The
    // IDB instance still holds the previous master, which would make the DEF
    // footprint disagree with the computed coordinates.
    // Fillers are generated by iFP and never connected to nets, so it is safe
    // to rebuild their pin list when the master changes.  Other instances are
    // synced only when they have no pins at all.
    const bool filler = instance.get_name().rfind("IO_FILL_", 0) == 0;
    idb::IdbInstance* idb_instance = idb_design->get_instance_list()->find_instance(instance.get_name());
    if (idb_instance != nullptr && (filler || idb_instance->get_pin_list()->get_pin_list().empty())
        && (idb_instance->get_cell_master() == nullptr
            || idb_instance->get_cell_master()->get_name() != instance.get_master_name())) {
      idb::IdbCellMaster* master = idb_layout->get_cell_master_list()->find_cell_master(instance.get_master_name());
      if (master == nullptr) {
        FPLOG.error(Loc::current(), "Cannot find cell master '", instance.get_master_name(), "' for IO instance '",
                    instance.get_name(), "'.");
        continue;
      }
      idb_instance->get_pin_list()->reset();
      idb_instance->set_cell_master(master);
    }
    idb::IdbOrient orient = unwrapPlacementOrientation(instance.get_orient());
    if (orient == idb::IdbOrient::kNone) {
      orient = idb::IdbOrient::kN_R0;
    }
    idb_design->placeInstance(instance.get_name(), instance.get_x(), instance.get_y(), orient,
                              instance.get_fixed() ? idb::IdbPlacementStatus::kFixed : idb::IdbPlacementStatus::kPlaced);
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

void FPInterface::outputMacroHalo()
{
  idb::IdbInstanceList* idb_instance_list = dmInst->get_idb_design()->get_instance_list();
  for (const Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }

    const PlanarRect& macro_rect = instance.get_bounding_rect();
    const PlanarRect& halo_rect = instance.get_placement_halo_rect();
    int32_t extend_left = macro_rect.get_ll_x() - halo_rect.get_ll_x();
    int32_t extend_bottom = macro_rect.get_ll_y() - halo_rect.get_ll_y();
    int32_t extend_right = halo_rect.get_ur_x() - macro_rect.get_ur_x();
    int32_t extend_top = halo_rect.get_ur_y() - macro_rect.get_ur_y();
    if (extend_left < 0 || extend_bottom < 0 || extend_right < 0 || extend_top < 0) {
      FPLOG.error(Loc::current(), "The placement halo of macro '", instance.get_name(), "' does not enclose the macro!");
      continue;
    }

    idb::IdbInstance* idb_instance = idb_instance_list->find_instance(instance.get_name());
    if (idb_instance == nullptr) {
      FPLOG.error(Loc::current(), "Failed to find macro '", instance.get_name(), "' in IDB when outputting its placement halo!");
      continue;
    }

    idb::IdbHalo* idb_halo = idb_instance->get_halo();
    if (idb_halo == nullptr) {
      idb_halo = idb_instance->set_halo();
    }
    idb_halo->set_soft(false);
    idb_halo->set_extend_lef(extend_left);
    idb_halo->set_extend_bottom(extend_bottom);
    idb_halo->set_extend_right(extend_right);
    idb_halo->set_extend_top(extend_top);
    idb_instance->set_halo_coodinate();
  }
}

void FPInterface::outputMacroRouteHalo()
{
  Config& config = FPDM.getConfig();
  if (config.macro_routing_halo < 0) {
    return;
  }

  idb::IdbLayers* idb_layer_list = dmInst->get_idb_layout()->get_layers();
  idb::IdbLayer* bottom_layer = idb_layer_list->get_bottom_routing_layer();
  if (bottom_layer == nullptr) {
    FPLOG.error(Loc::current(), "Failed to determine the bottom routing layer for macro route halos!");
    return;
  }

  idb::IdbInstanceList* idb_instance_list = dmInst->get_idb_design()->get_instance_list();
  for (const Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.get_macro() || !instance.get_placed()) {
      continue;
    }

    const PlanarRect& macro_rect = instance.get_bounding_rect();
    const PlanarRect& halo_rect = instance.get_routing_halo_rect();
    int32_t extend_left = macro_rect.get_ll_x() - halo_rect.get_ll_x();
    int32_t extend_bottom = macro_rect.get_ll_y() - halo_rect.get_ll_y();
    int32_t extend_right = halo_rect.get_ur_x() - macro_rect.get_ur_x();
    int32_t extend_top = halo_rect.get_ur_y() - macro_rect.get_ur_y();
    if (extend_left < 0 || extend_left != extend_bottom || extend_left != extend_right || extend_left != extend_top) {
      FPLOG.error(Loc::current(), "The routing halo of macro '", instance.get_name(), "' is invalid or asymmetric!");
      continue;
    }

    idb::IdbInstance* idb_instance = idb_instance_list->find_instance(instance.get_name());
    if (idb_instance == nullptr) {
      FPLOG.error(Loc::current(), "Failed to find macro '", instance.get_name(), "' in IDB when outputting its route halo!");
      continue;
    }

    idb::IdbCellMaster* cell_master = idb_instance->get_cell_master();
    if (cell_master == nullptr) {
      FPLOG.error(Loc::current(), "The macro '", instance.get_name(), "' has no cell master when outputting its route halo!");
      continue;
    }
    idb::IdbLayer* top_layer = cell_master->get_top_layer();
    for (idb::IdbLayerShape* obs_shape : idb_instance->get_obs_box_list()) {
      idb::IdbLayer* obs_layer = obs_shape == nullptr ? nullptr : obs_shape->get_layer();
      if (obs_layer != nullptr && obs_layer->is_routing() && (top_layer == nullptr || top_layer->get_order() < obs_layer->get_order())) {
        top_layer = obs_layer;
      }
    }
    if (top_layer == nullptr || !top_layer->is_routing() || top_layer->get_order() < bottom_layer->get_order()) {
      FPLOG.error(Loc::current(), "Failed to determine a valid top routing layer for macro '", instance.get_name(), "'!");
      continue;
    }

    idb::IdbRouteHalo* idb_route_halo = idb_instance->get_route_halo();
    if (idb_route_halo == nullptr) {
      idb_route_halo = idb_instance->set_route_halo();
    }
    idb_route_halo->set_route_distance(extend_left);
    idb_route_halo->set_layer_bottom(bottom_layer);
    idb_route_halo->set_layer_top(top_layer);
  }
}

void FPInterface::outputNewInstanceList()
{
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (!instance.is_new_instance() || instance.is_delete_instance()) {
      continue;
    }
    idb::IdbOrient orient = unwrapPlacementOrientation(instance.get_orient());
    if (orient == idb::IdbOrient::kNone) {
      orient = idb::IdbOrient::kN_R0;
    }
    idb::IdbInstance* idb_instance = dmInst->createInstance(instance.get_name(), instance.get_master_name(), instance.get_x(), instance.get_y(), orient,
                                                            idb::IdbInstanceType::kDist, idb::IdbPlacementStatus::kFixed);
    if (idb_instance == nullptr) {
      FPLOG.error(Loc::current(), "Cannot create floorplan instance '", instance.get_name(), "'.");
    }
    if (!instance.get_io_cell_net_name().empty() && !instance.get_io_cell_pin_name().empty()) {
      if (!dmInst->get_idb_design()->connectInstancePinToNet(instance.get_name(), instance.get_io_cell_pin_name(), instance.get_io_cell_net_name())) {
        FPLOG.error(Loc::current(), "Failed to connect generated IO power Cell '", instance.get_name(), "' pin '", instance.get_io_cell_pin_name(),
                    "' to net '", instance.get_io_cell_net_name(), "'.");
      }
    }
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
      FPLOG.error(Loc::current(), "PG geometry references undeclared net '", pg_segment.get_net_name(), "'.");
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
      if (!outputPGVia(idb_special_wire, pg_segment)) {
        FPLOG.error(Loc::current(), "Incomplete PG via stack on '", pg_segment.get_net_name(), "' from '", pg_segment.get_bottom_layer_name(),
                    "' to '", pg_segment.get_top_layer_name(), "' at (", pg_segment.get_start_x(), ",", pg_segment.get_start_y(), ").");
      }
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
    idb::IdbWireShapeType shape = idb::IdbWireShapeType::kStripe;
    switch (pg_segment.get_type()) {
      case PGSegmentType::kFollowPin: shape = idb::IdbWireShapeType::kFollowPin; break;
      case PGSegmentType::kRing: shape = idb::IdbWireShapeType::kRing; break;
      case PGSegmentType::kBlockWire: shape = idb::IdbWireShapeType::kBlockWire; break;
      case PGSegmentType::kIoWire: shape = idb::IdbWireShapeType::kIoWire; break;
      default: break;
    }
    idb_segment->set_shape_type(shape);
    idb_segment->add_point(pg_segment.get_start_x(), pg_segment.get_start_y());
    idb_segment->add_point(pg_segment.get_end_x(), pg_segment.get_end_y());
    idb_segment->set_bounding_box();
    pg_segment.set_generated(false);
  }
}

std::vector<InstancePinShape> FPInterface::getPGViaShapes(PGSegment& pg_segment)
{
  std::vector<InstancePinShape> shapes;
  for (auto* via : getPGViaStack(pg_segment)) {
    for (auto layer_shape : {via->get_bottom_layer_shape(), via->get_top_layer_shape()}) {
      for (auto* rect : layer_shape.get_rect_list()) {
        InstancePinShape shape;
        shape.set_layer_name(layer_shape.get_layer()->get_name());
        shape.set_rect(pg_segment.get_start_x() + rect->get_low_x(), pg_segment.get_start_y() + rect->get_low_y(),
                       pg_segment.get_start_x() + rect->get_high_x(), pg_segment.get_start_y() + rect->get_high_y());
        shapes.push_back(shape);
      }
    }
  }
  return shapes;
}

void FPInterface::resetPGViaAdjustment()
{
  _adjusted_via_count = 0;
}

void FPInterface::adjustPGLineSegmentListByViaEnclosure()
{
  idb::IdbLayout* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_layers() == nullptr) {
    return;
  }
  std::vector<PGSegment>& pg_segment_list = FPDM.getDatabase().get_pg_segment_list();
  size_t via_count = 0;
  for (const PGSegment& pg_segment : pg_segment_list) {
    if (pg_segment.get_type() == PGSegmentType::kVia) {
      ++via_count;
    }
  }
  if (via_count <= _adjusted_via_count) {
    return;
  }

  std::map<std::string, std::map<int32_t, std::vector<PGSegment*>>> pg_net_layer_coord_to_stripe_segment_list_map;
  for (PGSegment& pg_segment : pg_segment_list) {
    if (!pg_segment.is_line() || pg_segment.get_type() == PGSegmentType::kFollowPin) {
      continue;
    }
    std::string pg_net_layer_key = FPUTIL.getString(pg_segment.get_net_name(), "|", pg_segment.get_layer_name());
    int32_t line_coord = pg_segment.is_vertical() ? pg_segment.get_start_x() : pg_segment.get_start_y();
    pg_net_layer_coord_to_stripe_segment_list_map[pg_net_layer_key][line_coord].push_back(&pg_segment);
  }

  size_t via_ordinal = 0;
  for (PGSegment& pg_segment : pg_segment_list) {
    if (pg_segment.get_type() != PGSegmentType::kVia) {
      continue;
    }
    if (via_ordinal++ < _adjusted_via_count) {
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
  _adjusted_via_count = via_count;
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

std::vector<idb::IdbVia*> FPInterface::getPGViaStack(PGSegment& pg_segment)
{
  const std::string cache_key = FPUTIL.getString(pg_segment.get_bottom_layer_name(), "|", pg_segment.get_top_layer_name(), "|",
                                                 pg_segment.get_cut_layer_name(), "|", pg_segment.get_via_width(), "|", pg_segment.get_via_height());
  auto cache_iter = _pg_via_stack_cache.find(cache_key);
  if (cache_iter != _pg_via_stack_cache.end()) {
    return cache_iter->second;
  }

  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_layers() == nullptr) {
    return {};
  }
  auto* layers = idb_layout->get_layers();
  auto* bottom = layers->find_layer(pg_segment.get_bottom_layer_name());
  auto* top = layers->find_layer(pg_segment.get_top_layer_name());
  if (bottom == nullptr || top == nullptr || !bottom->is_routing() || !top->is_routing() || bottom == top) {
    return {};
  }
  if (bottom->get_order() > top->get_order()) {
    std::swap(bottom, top);
  }
  auto cuts = layers->find_cut_layer_list(bottom->get_name(), top->get_name());
  if (!pg_segment.get_cut_layer_name().empty()
      && (cuts.size() != 1 || cuts.front()->get_name() != pg_segment.get_cut_layer_name())) {
    return {};
  }
  std::vector<idb::IdbVia*> stack;
  idb::IdbLayer* current = bottom;
  for (auto* cut : cuts) {
    auto* rule = cut->get_via_rule();
    if (rule == nullptr || rule->get_layer_bottom() != current || rule->get_layer_top() == nullptr) {
      return {};
    }
    auto* via = getIDBVia(cut, pg_segment);
    if (via == nullptr) {
      return {};
    }
    stack.push_back(via);
    current = rule->get_layer_top();
  }
  if (current != top) {
    return {};
  }
  _pg_via_stack_cache[cache_key] = stack;
  return stack;
}

std::pair<int32_t, int32_t> FPInterface::getPGViaMinSize(const std::string& bottom_layer_name, const std::string& top_layer_name)
{
  const auto cache_key = std::make_pair(bottom_layer_name, top_layer_name);
  auto cache_iter = _pg_via_min_size_cache.find(cache_key);
  if (cache_iter != _pg_via_min_size_cache.end()) {
    return cache_iter->second;
  }

  auto* idb_layout = dmInst->get_idb_layout();
  if (idb_layout == nullptr || idb_layout->get_layers() == nullptr) {
    return {0, 0};
  }
  auto* layers = idb_layout->get_layers();
  auto* bottom = layers->find_layer(bottom_layer_name);
  auto* top = layers->find_layer(top_layer_name);
  if (bottom == nullptr || top == nullptr || !bottom->is_routing() || !top->is_routing() || bottom == top) {
    return {0, 0};
  }
  if (bottom->get_order() > top->get_order()) {
    std::swap(bottom, top);
  }
  int32_t required_width = 0;
  int32_t required_height = 0;
  for (auto* cut : layers->find_cut_layer_list(bottom->get_name(), top->get_name())) {
    auto* rule = cut->get_via_rule();
    if (rule == nullptr || rule->get_cut_rect() == nullptr || rule->get_enclosure_bottom() == nullptr
        || rule->get_enclosure_top() == nullptr) {
      return {0, 0};
    }
    const int32_t cut_x = rule->get_cut_rect()->get_width();
    const int32_t cut_y = rule->get_cut_rect()->get_height();
    const int32_t b1 = std::max(rule->get_enclosure_bottom()->get_overhang_1(), 0);
    const int32_t b2 = std::max(rule->get_enclosure_bottom()->get_overhang_2(), 0);
    const int32_t t1 = std::max(rule->get_enclosure_top()->get_overhang_1(), 0);
    const int32_t t2 = std::max(rule->get_enclosure_top()->get_overhang_2(), 0);
    // Pick the orientation that needs the smallest square; the layer's pair
    // must be feasible in one orientation, not two independently.
    int32_t layer_width = 0;
    int32_t layer_height = 0;
    int64_t best_span = INT64_MAX;
    for (int32_t bottom_swap = 0; bottom_swap < 2; ++bottom_swap) {
      const int32_t bottom_x = bottom_swap == 0 ? b1 : b2;
      const int32_t bottom_y = bottom_swap == 0 ? b2 : b1;
      for (int32_t top_swap = 0; top_swap < 2; ++top_swap) {
        const int32_t top_x = top_swap == 0 ? t1 : t2;
        const int32_t top_y = top_swap == 0 ? t2 : t1;
        const int32_t width = cut_x + 2 * std::max(bottom_x, top_x);
        const int32_t height = cut_y + 2 * std::max(bottom_y, top_y);
        const int64_t span = std::max(width, height);
        if (span < best_span) {
          best_span = span;
          layer_width = width;
          layer_height = height;
        }
      }
    }
    required_width = std::max(required_width, layer_width);
    required_height = std::max(required_height, layer_height);
  }
  const std::pair<int32_t, int32_t> result{required_width, required_height};
  _pg_via_min_size_cache[cache_key] = result;
  return result;
}

int32_t FPInterface::getPGViaMinSpan(const std::string& bottom_layer_name, const std::string& top_layer_name)
{
  const auto [width, height] = getPGViaMinSize(bottom_layer_name, top_layer_name);
  return std::max(width, height);
}

idb::IdbVia* FPInterface::buildIDBVia(std::string via_name, idb::IdbLayerCut* idb_cut_layer, PGSegment& pg_segment)
{
  idb::IdbViaRuleGenerate* idb_via_rule = idb_cut_layer->get_via_rule();
  if (idb_via_rule == nullptr) {
    return nullptr;
  }

  PGViaGeometry geometry = getIDBViaGeometry(idb_cut_layer, pg_segment);
  int32_t cut_row_num = geometry.cut_row_num;
  int32_t cut_col_num = geometry.cut_col_num;
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
  idb_via_master_generate->set_enclosure_bottom(geometry.bottom_enclosure_x, geometry.bottom_enclosure_y);
  idb_via_master_generate->set_enclosure_top(geometry.top_enclosure_x, geometry.top_enclosure_y);

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

PGViaGeometry FPInterface::getIDBViaGeometry(idb::IdbLayerCut* idb_cut_layer, PGSegment& pg_segment)
{
  PGViaGeometry result;
  idb::IdbViaRuleGenerate* idb_via_rule = idb_cut_layer->get_via_rule();
  if (idb_via_rule == nullptr || idb_via_rule->get_cut_rect() == nullptr || idb_via_rule->get_enclosure_bottom() == nullptr
      || idb_via_rule->get_enclosure_top() == nullptr) {
    return result;
  }

  int32_t cut_size_x = idb_via_rule->get_cut_rect()->get_width();
  int32_t cut_size_y = idb_via_rule->get_cut_rect()->get_height();
  int32_t cut_pitch_x = idb_via_rule->get_spacing_x();
  int32_t cut_pitch_y = idb_via_rule->get_spacing_y();
  if (cut_size_x <= 0 || cut_size_y <= 0 || cut_pitch_x <= 0 || cut_pitch_y <= 0) {
    return result;
  }
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
      int32_t cut_width = cut_size_x + (candidate_col_num - 1) * cut_pitch_x;
      int32_t cut_height = cut_size_y + (candidate_row_num - 1) * cut_pitch_y;
      int32_t candidate_bottom_x = bottom_enclosure_x;
      int32_t candidate_bottom_y = bottom_enclosure_y;
      int32_t candidate_top_x = top_enclosure_x;
      int32_t candidate_top_y = top_enclosure_y;
      if (!adjustIDBViaEnclosureArea(idb_via_rule->get_layer_bottom(), cut_width, cut_height, pg_segment, candidate_bottom_x, candidate_bottom_y)
          || !adjustIDBViaEnclosureArea(idb_via_rule->get_layer_top(), cut_width, cut_height, pg_segment, candidate_top_x, candidate_top_y)) {
        continue;
      }
      int64_t candidate_cut_num = static_cast<int64_t>(candidate_row_num) * candidate_col_num;
      if (max_cut_num < candidate_cut_num) {
        max_cut_num = candidate_cut_num;
        result.cut_row_num = candidate_row_num;
        result.cut_col_num = candidate_col_num;
        result.bottom_enclosure_x = candidate_bottom_x;
        result.bottom_enclosure_y = candidate_bottom_y;
        result.top_enclosure_x = candidate_top_x;
        result.top_enclosure_y = candidate_top_y;
      }
    }
  }
  return result;
}

bool FPInterface::adjustIDBViaEnclosureArea(idb::IdbLayerRouting* idb_layer, int32_t cut_width, int32_t cut_height, PGSegment& pg_segment,
                                          int32_t& enclosure_x, int32_t& enclosure_y)
{
  int32_t min_area = idb_layer->get_area();
  if (min_area <= 0 || (cut_width + 2.0 * enclosure_x) * (cut_height + 2.0 * enclosure_y) >= min_area) {
    return true;
  }

  int32_t grid = std::max(dmInst->get_idb_layout()->get_munufacture_grid(), 1);
  int32_t max_enclosure_x = FPUTIL.alignDown((pg_segment.get_via_width() - cut_width) / 2, grid);
  int32_t max_enclosure_y = FPUTIL.alignDown((pg_segment.get_via_height() - cut_height) / 2, grid);
  if (max_enclosure_x < enclosure_x || max_enclosure_y < enclosure_y) {
    return false;
  }

  // 中间层可能没有同网导线补足面积，仅在原 via 范围内沿两轴尝试扩展，不加宽 rail。
  double best_area = 0.0;
  int32_t best_growth = 0;
  int32_t best_x = enclosure_x;
  int32_t best_y = enclosure_y;
  for (int32_t swap_axis = 0; swap_axis < 2; swap_axis++) {
    int32_t first_cut = swap_axis == 0 ? cut_width : cut_height;
    int32_t second_cut = swap_axis == 0 ? cut_height : cut_width;
    int32_t first_enclosure = swap_axis == 0 ? enclosure_x : enclosure_y;
    int32_t second_enclosure = swap_axis == 0 ? enclosure_y : enclosure_x;
    int32_t first_limit = swap_axis == 0 ? max_enclosure_x : max_enclosure_y;
    int32_t second_limit = swap_axis == 0 ? max_enclosure_y : max_enclosure_x;
    int32_t second_span = second_cut + 2 * second_enclosure;
    int32_t required_first = min_area / second_span + (min_area % second_span != 0);
    first_enclosure = std::min(first_limit, std::max(first_enclosure, FPUTIL.alignUp((required_first - first_cut + 1) / 2, grid)));
    int32_t first_span = first_cut + 2 * first_enclosure;
    int32_t required_second = min_area / first_span + (min_area % first_span != 0);
    second_enclosure = std::max(second_enclosure, FPUTIL.alignUp((required_second - second_cut + 1) / 2, grid));
    if (second_limit < second_enclosure) {
      continue;
    }
    double area = first_span * (second_cut + 2.0 * second_enclosure);
    int32_t growth = first_enclosure + second_enclosure - enclosure_x - enclosure_y;
    if (area < min_area || (best_area > 0.0 && (best_growth < growth || (best_growth == growth && best_area <= area)))) {
      continue;
    }
    best_area = area;
    best_growth = growth;
    best_x = swap_axis == 0 ? first_enclosure : second_enclosure;
    best_y = swap_axis == 0 ? second_enclosure : first_enclosure;
  }
  if (best_area == 0.0) {
    return false;
  }
  enclosure_x = best_x;
  enclosure_y = best_y;
  return true;
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
    if (line_segment->is_vertical()) {
      if (enclosure_ll_x < line_segment->get_ll_x() || line_segment->get_ur_x() < enclosure_ur_x || line_segment->get_ur_y() < enclosure_ll_y
          || enclosure_ur_y < line_segment->get_ll_y()) {
        continue;
      }
      if (line_segment->get_start_y() <= line_segment->get_end_y()) {
        if (enclosure_ll_y < line_segment->get_ll_y()) {
          line_segment->set_start_y(enclosure_ll_y);
        }
        if (line_segment->get_ur_y() < enclosure_ur_y) {
          line_segment->set_end_y(enclosure_ur_y);
        }
      } else {
        if (enclosure_ll_y < line_segment->get_ll_y()) {
          line_segment->set_end_y(enclosure_ll_y);
        }
        if (line_segment->get_ur_y() < enclosure_ur_y) {
          line_segment->set_start_y(enclosure_ur_y);
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
          line_segment->set_start_x(enclosure_ll_x);
        }
        if (line_segment->get_ur_x() < enclosure_ur_x) {
          line_segment->set_end_x(enclosure_ur_x);
        }
      } else {
        if (enclosure_ll_x < line_segment->get_ll_x()) {
          line_segment->set_end_x(enclosure_ll_x);
        }
        if (line_segment->get_ur_x() < enclosure_ur_x) {
          line_segment->set_start_x(enclosure_ur_x);
        }
      }
      covered = (enclosure_ll_x >= line_segment->get_ll_x() && line_segment->get_ur_x() >= enclosure_ur_x) || covered;
    }
  }
  return covered;
}

bool FPInterface::outputPGVia(idb::IdbSpecialWire* idb_special_wire, PGSegment& pg_segment)
{
  const auto stack = getPGViaStack(pg_segment);
  if (stack.empty()) {
    return false;
  }
  for (idb::IdbVia* idb_via : stack) {
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
  return true;
}

#endif

#endif

#endif

}  // namespace ifp
