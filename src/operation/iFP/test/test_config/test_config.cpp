// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "Logger.hpp"
#include "Utility.hpp"
#include "def_read.h"
#include "def_write.h"
#include "idm.h"

namespace ifp {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    FPLOG.warn(Loc::current(), message);
  }
  return condition;
}

nlohmann::json makeTestConfig()
{
  return nlohmann::json::parse(R"({
    "ifp": {"temp_directory_path": "output", "thread_number": 1},
    "macro_placer": {"file_path": "macro.tcl", "macro_placement_halo": 3.0, "macro_routing_halo": 3.0},
    "die_builder": {"mode": "die_size", "site_name": "SITE",
      "die_size": {"width_micron": 20.0, "height_micron": 20.0},
      "margin": {"left_micron": 2.0, "right_micron": 2.0, "top_micron": 2.0, "bottom_micron": 2.0}},
    "io_placer": {},
    "phy_placer": {
      "well_tap": {"cell_name": "TAP", "distance_micron": 10.0},
      "side_endcap": {"left_cell_name": "SIDE", "right_cell_name": "SIDE"},
      "edge_endcap": {"top_cell_name_list": ["EDGE"], "bottom_cell_name_list": ["EDGE"]},
      "boundary_tap": {"top_cell_name_list": ["EDGE"], "bottom_cell_name_list": ["EDGE"], "rule_micron": 5.0}},
    "pdn_generator": {}
  })");
}

bool testLegacyMacroConfig(const std::string& directory)
{
  auto config = makeTestConfig();
  std::string path = directory + "/config.json";
  std::map<std::string, std::any> config_map{{"-config", path}};
  bool passed = true;
  for (std::string mode : {"", "auto", "file", "invalid", "unknown_key"}) {
    config["macro_placer"].erase("mode");
    config["macro_placer"].erase("unexpected");
    if (mode == "unknown_key") {
      config["macro_placer"]["unexpected"] = true;
    } else if (!mode.empty()) {
      config["macro_placer"]["mode"] = mode;
    }
    std::ofstream* file = FPUTIL.getOutputFileStream(path);
    FPUTIL.pushStream(file, config.dump(2));
    FPUTIL.closeFileStream(file);
    bool rejected = false;
    try {
      FPI.wrapConfig(config_map);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    bool expected_rejection = mode == "invalid" || mode == "unknown_key";
    passed = check(rejected == expected_rejection, "Macro config compatibility/validation failed for mode: " + mode) && passed;
    if (!expected_rejection) {
      passed = check(FPDM.getConfig().input_macro_path == directory + "/macro.tcl", "file_path must remain the macro placement source") && passed;
    }
  }
  return passed;
}

bool testRemovedRingConfig(const std::string& directory)
{
  auto config = makeTestConfig();
  const std::string path = directory + "/ring.json";
  std::map<std::string, std::any> config_map{{"-config", path}};
  bool passed = true;
  for (const auto& nets : {nlohmann::json::array(), nlohmann::json::array({"VDD", "VSS"})}) {
    config["pdn_generator"]["ring"] = {{"nets", nets}, {"horizontal_layer", "MET5"}, {"vertical_layer", "MET4"},
                                      {"width_micron", 2.0}, {"spacing_micron", 2.0}, {"offset_micron", 5.0}};
    std::ofstream(path) << config.dump();
    bool rejected = false;
    try {
      FPI.wrapConfig(config_map);
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("Unknown config key 'pdn_generator.ring'.") != std::string::npos;
    }
    passed = check(rejected, "Expected pdn_generator.ring to be rejected for nets=" + nets.dump()) && passed;
  }
  return passed;
}

bool testIOCellSupplyNets(const std::string& directory)
{
  auto config = makeTestConfig();
  config["macro_placer"]["file_path"] = "";
  config["pdn_generator"]["global_connect"] = nlohmann::json::parse(R"([
    {"net_name":"CORE_PWR", "instance_pin_name":"PWR", "is_power":true},
    {"net_name":"CORE_PWR", "instance_pin_name":"PWR2", "is_power":true},
    {"net_name":"CORE_GND", "instance_pin_name":"GND", "is_power":false},
    {"net_name":"IO_PWR", "instance_pin_name":"PWRIO", "is_power":true},
    {"net_name":"IO_GND", "instance_pin_name":"GNDIO", "is_power":false}
  ])");
  const std::string path = directory + "/supply.json";
  std::map<std::string, std::any> config_map{{"-config", path}};
  idb::IdbLefService lef;
  idb::IdbDefService def(lef.get_layout());
  auto* previous_lef = dmInst->get_idb_lef_service();
  auto* previous_def = dmInst->get_idb_def_service();
  dmInst->set_idb_lef_service(&lef);
  dmInst->set_idb_def_service(&def);
  auto* design = def.get_design();
  design->createOrFindNet("IO_PWR", idb::IdbConnectType::kSignal);
  design->createOrFindSpecialNet("IO_GND", idb::IdbConnectType::kGround);

  bool passed = true;
  for (const std::string mode : {"io_pin", "io_cell", "io_cell"}) {
    config["io_placer"]["mode"] = mode;
    std::ofstream(path) << config.dump();
    FPI.input(config_map);
    std::map<std::string, idb::IdbConnectType> expected{{"IO_PWR", idb::IdbConnectType::kSignal}};
    if (mode == "io_cell") {
      expected.emplace("CORE_PWR", idb::IdbConnectType::kPower);
      expected.emplace("CORE_GND", idb::IdbConnectType::kGround);
    }
    std::map<std::string, idb::IdbConnectType> actual;
    for (auto* net : design->get_net_list()->get_net_list()) {
      actual.emplace(net->get_net_name(), net->get_connect_type());
    }
    passed = check(actual == expected && FPDM.getDatabase().get_net_list().size() == expected.size(),
                   "Unexpected supply nets after " + mode + " input") && passed;
  }
  dmInst->set_idb_def_service(previous_def);
  dmInst->set_idb_lef_service(previous_lef);
  return passed;
}

bool testFailedRunCleanup()
{
  FPDM.getConfig() = Config();
  FPDM.getDatabase() = Database();
  FPDM.getConfig().die_mode = DieMode::kDieUtil;
  FPDM.getConfig().die_utilization = 0.0;
  bool rejected = false;
  try {
    FPI.runFP();
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  bool passed = check(rejected, "Invalid floorplan must fail");
  // There is intentionally no IDB design: exporting partial data here would fail.
  FPI.destroyFP();
  FPInterface::destroyInst();
  return passed;
}

bool testIOCellEmbeddedPortExport(const std::string& directory)
{
  auto* previous_lef = dmInst->get_idb_lef_service();
  auto* previous_def = dmInst->get_idb_def_service();
  idb::IdbLefService lef;
  idb::IdbDefService def(lef.get_layout());
  dmInst->set_idb_lef_service(&lef);
  dmInst->set_idb_def_service(&def);
  FPInterface::destroyInst();

  constexpr int32_t kGrid = 5;
  idb::IdbLayout* layout = lef.get_layout();
  layout->get_units()->set_microns_dbu(1000);
  layout->set_manufacture_grid(kGrid);
  layout->get_die()->add_point(0, 0);
  layout->get_die()->add_point(200000, 200000);

  auto add_routing = [&](const std::string& name, idb::IdbLayerDirection dir, int32_t min_w, int32_t max_w, int32_t area) {
    auto* layer = dynamic_cast<idb::IdbLayerRouting*>(layout->get_layers()->set_layer(name, "ROUTING"));
    layout->get_layers()->add_routing_layer(layer);
    layer->set_direction(dir);
    layer->set_min_width(min_w);
    layer->set_max_width(max_w);
    layer->set_area(area);
    return layer;
  };
  auto* met5 = add_routing("MET5", idb::IdbLayerDirection::kHorizontal, 100, 10000, 52000);
  auto* t4m2 = add_routing("T4M2", idb::IdbLayerDirection::kVertical, 400, 20000, 400000);

  auto add_term_rect = [](idb::IdbCellMaster* master, const std::string& term_name, idb::IdbLayer* layer, int32_t x0, int32_t y0,
                          int32_t x1, int32_t y1, idb::IdbConnectType type = idb::IdbConnectType::kSignal) {
    idb::IdbTerm* term = master->findTerm(term_name);
    if (term == nullptr) {
      term = master->add_term(term_name);
      term->set_direction(idb::IdbConnectDirection::kInOut);
      term->set_type(type);
    }
    idb::IdbLayerShape* shape = term->add_port()->add_layer_shape();
    shape->set_type_rect();
    shape->set_layer(layer);
    shape->add_rect(x0, y0, x1, y1);
  };

  auto* corner = layout->get_cell_master_list()->set_cell_master("CORNER");
  corner->set_type("ENDCAP");
  corner->set_width(10000);
  corner->set_height(10000);

  auto* filler = layout->get_cell_master_list()->set_cell_master("IO_FILL");
  filler->set_type("PAD");
  filler->set_type_pad_filler();
  filler->set_width(1000);
  filler->set_height(10000);

  auto* vdd_master = layout->get_cell_master_list()->set_cell_master("IO_VDD");
  vdd_master->set_type("PAD");
  vdd_master->set_width(10000);
  vdd_master->set_height(10000);
  add_term_rect(vdd_master, "VDD", t4m2, 1000, 1000, 9000, 9000, idb::IdbConnectType::kPower);

  auto* wide_sig = layout->get_cell_master_list()->set_cell_master("IO_SIG_WIDE");
  wide_sig->set_type("PAD");
  wide_sig->set_width(30000);
  wide_sig->set_height(30000);
  add_term_rect(wide_sig, "PAD", met5, 2000, 2000, 8000, 8000);
  add_term_rect(wide_sig, "PAD", t4m2, 2000, 1000, 28000, 27000);

  auto* narrow_sig = layout->get_cell_master_list()->set_cell_master("IO_SIG_NARROW");
  narrow_sig->set_type("PAD");
  narrow_sig->set_width(10000);
  narrow_sig->set_height(10000);
  add_term_rect(narrow_sig, "PAD", t4m2, 4800, 2000, 5200, 7000);

  idb::IdbDesign* design = def.get_design();
  design->set_design_name("IO_CELL_PORTS");
  design->get_units()->set_microns_dbu(1000);
  auto add_pin = [&](const std::string& name, idb::IdbConnectType type) {
    idb::IdbPin* pin = design->createOrFindIoPin(name);
    pin->set_as_io();
    pin->set_net_name(name);
    idb::IdbTerm* term = pin->set_term(nullptr);
    term->set_name(name);
    term->set_direction(idb::IdbConnectDirection::kInOut);
    term->set_type(type);
    design->connectPinToNet(pin, design->createOrFindNet(name, type));
  };
  add_pin("VDD", idb::IdbConnectType::kPower);

  const std::vector<std::tuple<std::string, std::string, idb::IdbCellMaster*, int32_t, int32_t>> pads = {
      {"sig0", "u_pad_0", wide_sig, 640, 640},
      {"sig1", "u_pad_1", wide_sig, 640, 640},
      {"sig2", "u_pad_2", wide_sig, 640, 640},
      {"sig_narrow", "u_pad_narrow", narrow_sig, 400, 1000},
  };
  for (const auto& [pin_name, inst_name, master, exp_s, exp_l] : pads) {
    add_pin(pin_name, idb::IdbConnectType::kSignal);
    design->get_instance_list()->add_instance(inst_name)->set_cell_master(master);
    design->connectInstancePinToNet(inst_name, "PAD", pin_name);
  }

  const std::string def_json = directory + "/io_cell_def.json";
  std::ofstream(def_json) << R"({
    "corner": {"master": "CORNER"},
    "signal_masters": ["IO_SIG_WIDE", "IO_SIG_NARROW"],
    "power_members": [{"master": "IO_VDD", "pin": "VDD", "net_pattern": ["VDD"]}],
    "filler_masters": [{"master": "IO_FILL", "width_micron": 1.0}]
  })";

  FPDM.getConfig() = Config();
  FPDM.getDatabase() = Database();
  FPDM.getConfig().io_placer_mode = IOPlacerMode::kIOCell;
  FPDM.getConfig().io_cell_mode = PlacementMode::kAuto;
  FPDM.getConfig().io_cell_definition_file = def_json;
  FPDM.getConfig().core_power_net = "VDD";

  FPI.wrapDatabase();
  Database& db = FPDM.getDatabase();
  db.get_die().set_rect(0, 0, 200000, 200000);
  for (int32_t i = 0; i < static_cast<int32_t>(db.get_instance_list().size()); ++i) {
    db.get_instance_name_to_idx_map()[db.get_instance_list()[i].get_name()] = i;
  }
  for (int32_t i = 0; i < static_cast<int32_t>(db.get_routing_layer_list().size()); ++i) {
    db.get_routing_layer_name_to_idx_map()[db.get_routing_layer_list()[i].get_name()] = i;
  }
  for (int32_t i = 0; i < static_cast<int32_t>(db.get_io_pin_list().size()); ++i) {
    db.get_io_pin_name_to_idx_map()[db.get_io_pin_list()[i].get_name()] = i;
  }

  FPI.runSimpleFP();
  FPI.outputIOInstancePlacement();

  const std::string def_path = directory + "/io_cell_ports.def";
  bool passed = check(design->get_io_pin_list()->find_pin("VDD") == nullptr, "PG pin must remain excluded from PINS");
  passed = check(idb::DefWrite(&def).writeDb(def_path.c_str()), "IO-cell DEF export must succeed") && passed;

  idb::IdbDefService reloaded(layout);
  passed = check(idb::DefRead(&reloaded).createDb(def_path.c_str()), "IO-cell DEF round-trip must succeed") && passed;
  idb::IdbDesign* rd = reloaded.get_design();
  passed = check(rd->get_io_pin_list()->get_pin_num() == 4, "Reloaded DEF must contain only the 4 signal PINS") && passed;

  std::set<idb::IdbOrient> orients;
  for (const auto& [pin_name, inst_name, master, exp_s, exp_l] : pads) {
    idb::IdbPin* pin = rd->get_io_pin_list()->find_pin(pin_name);
    idb::IdbInstance* inst = rd->get_instance_list()->find_instance(inst_name);
    if (!check(pin != nullptr && inst != nullptr && pin->get_term()->is_port_exist() && pin->get_term()->get_port_list().size() == 1
                   && pin->get_port_box_list().size() == 1 && pin->get_port_box_list().front()->get_rect_list().size() == 1,
               "Reloaded signal pin must have a single embedded PORT shape: " + pin_name)) {
      passed = false;
      continue;
    }
    orients.insert(inst->get_orient());
    idb::IdbPort* port = pin->get_term()->get_port_list().front();
    idb::IdbLayerShape* shape = pin->get_port_box_list().front();
    const idb::IdbRect* pr = shape->get_rect_list().front();
    inst->set_bounding_box();
    idb::IdbPin* pad_pin = inst->get_pin_by_term("PAD");
    pad_pin->set_bounding_box();
    idb::IdbRect pad_rect;
    for (idb::IdbLayerShape* ls : pad_pin->get_port_box_list()) {
      if (ls->get_layer()->get_name() == "T4M2" && !ls->get_rect_list().empty()) {
        pad_rect = *ls->get_rect_list().front();
      }
    }
    const int32_t pw = pr->get_width();
    const int32_t ph = pr->get_height();
    passed = check(port->get_placement_status() == idb::IdbPlacementStatus::kFixed && port->get_orient() == idb::IdbOrient::kN_R0
                       && shape->get_layer()->get_name() == "T4M2" && port->get_coordinate()->get_x() % kGrid == 0
                       && port->get_coordinate()->get_y() % kGrid == 0 && pr->get_low_x() % kGrid == 0 && pr->get_low_y() % kGrid == 0
                       && pr->get_high_x() % kGrid == 0 && pr->get_high_y() % kGrid == 0 && std::min(pw, ph) == exp_s
                       && std::max(pw, ph) == exp_l && pad_rect.get_low_x() <= pr->get_low_x()
                       && pr->get_high_x() <= pad_rect.get_high_x() && pad_rect.get_low_y() <= pr->get_low_y()
                       && pr->get_high_y() <= pad_rect.get_high_y(),
                   "Embedded T4M2 FIXED port must be grid-aligned, legal, and inside PAD: " + pin_name)
             && passed;
  }
  passed = check(orients.size() == 4, "Signal PADs must exercise all four ring orientations N/W/S/E") && passed;

  dmInst->set_idb_def_service(previous_def);
  dmInst->set_idb_lef_service(previous_lef);
  FPInterface::destroyInst();
  return passed;
}

}  // namespace ifp

int main(int argc, char* argv[])
{
  if (argc != 2) {
    return EXIT_FAILURE;
  }
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  std::string directory = std::filesystem::absolute(argv[1]);
  ifp::DataManager::initInst();
  FPUTIL.createDir(directory);
  bool passed = ifp::testLegacyMacroConfig(directory);
  passed = ifp::testRemovedRingConfig(directory) && passed;
  passed = ifp::testIOCellSupplyNets(directory) && passed;
  passed = ifp::testIOCellEmbeddedPortExport(directory) && passed;
  passed = ifp::testFailedRunCleanup() && passed;
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
