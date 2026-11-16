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
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "DataManager.hpp"
#include "RCXInterface.hpp"
#include "Utility.hpp"
#include "idm.h"

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class InterfaceFixture
{
 public:
  InterfaceFixture()
  {
    auto pattern = (std::filesystem::temp_directory_path() / "ircx-interface-XXXXXX").string();
    require(mkdtemp(pattern.data()) != nullptr, "cannot create interface test directory");
    _directory = pattern;
  }

  ~InterfaceFixture()
  {
    ircx::DataManager::destroyInst();
    ircx::RCXInterface::destroyInst();
    ircx::Utility::destroyInst();
    ircx::Logger::destroyInst();
    dmInst->set_idb_def_service(nullptr);
    dmInst->set_idb_lef_service(nullptr);
    std::error_code error;
    std::filesystem::remove_all(_directory, error);
  }

  void prepare(bool has_lef = true, bool has_def = true)
  {
    write("mapping.map", "M1 metal1\n");
    write("typical.itf", "CONDUCTOR metal1 { THICKNESS=0.1 RPSQ=0.12 }\n");
    write("typical.captab", "");
    write("ircx.config", R"({
      "thread_num": 1,
      "output": "output",
      "mapping_file": "mapping.map",
      "corners": [{"name":"TYPICAL", "itf_file":"typical.itf", "captab_file":"typical.captab"}]
    })");

    connectDatabase(has_lef, has_def);
    auto* layout = _lef_service.get_layout();
    layout->get_die()->add_point(0, 0);
    layout->get_die()->add_point(4000, 4000);
    layout->get_die()->set_bounding_box();
    auto* layer = dynamic_cast<idb::IdbLayerRouting*>(layout->get_layers()->set_layer("M1", "ROUTING"));
    require(layer != nullptr, "cannot create routing layer");
    layer->set_type(idb::IdbLayerType::kLayerRouting);
    layer->set_width(200);
    layer->set_direction("HORIZONTAL");
    layout->get_layers()->add_routing_layer(layer);
    for (auto direction : {idb::IdbTrackDirection::kDirectionX, idb::IdbTrackDirection::kDirectionY}) {
      auto* grid = layout->get_track_grid_list()->add_track_grid();
      grid->set_track_number(21);
      grid->get_track()->set_direction(direction);
      grid->get_track()->set_start(0);
      grid->get_track()->set_pitch(200);
      grid->add_layer_list(layer);
      layer->add_track_grid(grid);
    }

    auto* design = _def_service.get_design();
    design->set_design_name("interface_test");
    design->get_units()->set_microns_dbu(1000);
    auto* net = design->get_net_list()->add_net("n0");
    auto* segment = net->get_wire_list()->add_wire()->add_segment();
    segment->set_layer(layer);
    segment->add_point(1000, 1000);
    segment->add_point(2000, 1000);
  }

  void connectDatabase(bool has_lef = true, bool has_def = true)
  {
    dmInst->set_idb_lef_service(has_lef ? &_lef_service : nullptr);
    dmInst->set_idb_def_service(has_def ? &_def_service : nullptr);
  }

  void init()
  {
    RCXI.initRCX({{"-config", (_directory / "ircx.config").string()}});
  }

  const std::filesystem::path& directory() const { return _directory; }

 private:
  void write(const std::string& name, const std::string& content)
  {
    std::ofstream file(_directory / name);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << content;
  }

  std::filesystem::path _directory;
  idb::IdbLefService _lef_service;
  idb::IdbDefService _def_service{_lef_service.get_layout()};
};

void checkExtraction()
{
  InterfaceFixture fixture;
  fixture.prepare();
  fixture.init();
  require(RCXDM.getDatabase().get_corner_data_list().size() == 1, "process corner was not loaded");
  RCXI.runRCX();
  const auto& resistance = RCXDM.getDatabase().get_rc_data().get_corner_net_resistance_list(ircx::CornerNetIdx(0, 0));
  // RPSQ 0.12 ohm/square, length 1 um, width 0.2 um: R = 0.6 ohm.
  require(resistance.size() == 1 && std::isfinite(resistance[0]) && std::abs(resistance[0] - 0.6) < 1e-10,
          "interface did not extract the expected wire resistance");
  RCXI.destroyRCX();

  std::ifstream spef(fixture.directory() / "output/spef_writer/interface_test_TYPICAL_25C.spef");
  require(spef.is_open(), "interface did not write the SPEF");
  bool found_design = false;
  bool found_resistance = false;
  for (std::string line; std::getline(spef, line);) {
    found_design |= line == "*DESIGN \"interface_test\"";
    if (line == "*RES") {
      int index = 0;
      std::string from, to;
      double value = 0.0;
      require(static_cast<bool>(spef >> index >> from >> to >> value), "missing SPEF resistor");
      require(index == 1 && from != to && std::isfinite(value) && std::abs(value - 0.6) < 1e-6,
              "SPEF resistor does not match the extracted wire");
      found_resistance = true;
    }
  }
  require(found_design && found_resistance, "SPEF is missing the design or resistance section");
}

void checkMissingDatabase(bool has_lef, bool has_def, bool retry = false)
{
  InterfaceFixture fixture;
  fixture.prepare(has_lef, has_def);
  bool rejected = false;
  try {
    fixture.init();
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    require(message.find("LEF") != std::string::npos && message.find("DEF") != std::string::npos,
            "missing database must identify the required LEF/DEF input");
    rejected = true;
  }
  require(rejected, "interface accepted an uninitialized database");
  require(!std::filesystem::exists(fixture.directory() / "output"), "invalid input created extraction output");
  if (retry) {
    fixture.connectDatabase();
    fixture.init();
    require(RCXDM.getDatabase().get_corner_data_list().size() == 1, "retry after missing database duplicated process corners");
    RCXI.runRCX();
    RCXI.destroyRCX();
  }
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    require(setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1) == 0, "cannot enable exception error reporting");
    require(setenv("IRCX_MULTI_MODE", "off", 1) == 0, "cannot select the test extraction mode");
    const std::string scenario = argc > 1 ? argv[1] : "extract";
    if (scenario == "extract") {
      checkExtraction();
    } else if (scenario == "missing_database") {
      checkMissingDatabase(false, false);
    } else if (scenario == "missing_lef") {
      checkMissingDatabase(false, true);
    } else if (scenario == "missing_def") {
      checkMissingDatabase(true, false);
    } else if (scenario == "retry_after_missing_database") {
      checkMissingDatabase(false, false, true);
    } else {
      throw std::runtime_error("unknown interface test scenario: " + scenario);
    }
    std::cout << "PASS: " << scenario << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
