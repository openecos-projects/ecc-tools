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

namespace ifp {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    FPLOG.warn(Loc::current(), message);
  }
  return condition;
}

bool testLegacyMacroConfig(const std::string& directory)
{
  nlohmann::json config = nlohmann::json::parse(R"({
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
  passed = ifp::testFailedRunCleanup() && passed;
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
