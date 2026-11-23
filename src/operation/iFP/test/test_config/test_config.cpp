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
  passed = ifp::testFailedRunCleanup() && passed;
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
