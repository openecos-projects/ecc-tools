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
#include <set>
#include <stdexcept>

#include "DataManager.hpp"
#include "RCXInterface.hpp"
#include "SPEFSanitizer.hpp"
#include "SpefParser.hh"
#include "Utility.hpp"
#include "idm.h"
#include "reader/SpefReader.hh"

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
    _layer = layer;
    addNet("n0", 1000);
  }

  void addNet(const std::string& net_name, int32_t y)
  {
    auto* net = _def_service.get_design()->get_net_list()->add_net(net_name);
    auto* segment = net->get_wire_list()->add_wire()->add_segment();
    segment->set_layer(_layer);
    segment->add_point(1000, y);
    segment->add_point(2000, y);
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
  idb::IdbLayerRouting* _layer = nullptr;
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

bool hasUnescapedDollar(const std::string& text)
{
  bool in_quotes = false;
  for (std::size_t idx = 0; idx < text.size(); ++idx) {
    const char ch = text[idx];
    if (!in_quotes && ch == '/' && idx + 1 < text.size() && text[idx + 1] == '/') {
      return false;
    }
    if (ch == '"' && (idx == 0 || text[idx - 1] != '\\')) {
      in_quotes = !in_quotes;
      continue;
    }
    if (!in_quotes && ch == '$' && (idx == 0 || text[idx - 1] != '\\')) {
      return true;
    }
  }
  return false;
}

void checkSpefEscaping()
{
  require(RCXI.getSPEFName("state_8__$_OR__A_Y") == "state_8__\\$_OR__A_Y", "'$' in a flat name was not escaped");
  require(RCXI.getSPEFName("u_core.cnt_$_NAND__Y") == "u_core\\.cnt_\\$_NAND__Y", "'$' in a hierarchical name was not escaped");
  require(RCXI.getSPEFName("bus[0]_reg_p") == "bus[0]_reg_p", "flat bus name escaping changed");
  require(RCXI.getSPEFName("pre\\$escaped") == "pre\\$escaped", "already escaped '$' was escaped twice");

  InterfaceFixture fixture;
  fixture.prepare();
  fixture.addNet("state_8__$_OR__A_Y", 2000);
  fixture.addNet("u_core.cnt_$_NAND__Y", 3000);
  fixture.init();
  RCXI.runRCX();
  RCXI.destroyRCX();

  const std::filesystem::path spef_path = fixture.directory() / "output/spef_writer/interface_test_TYPICAL_25C.spef";
  std::ifstream spef(spef_path);
  require(spef.is_open(), "interface did not write the SPEF");
  bool found_flat = false;
  bool found_hier = false;
  for (std::string line; std::getline(spef, line);) {
    require(!hasUnescapedDollar(line), "SPEF contains an unescaped '$': " + line);
    require(line != "*PORTS", "SPEF must omit empty *PORTS section");
    require(line != "*CONN", "SPEF must omit pinless *CONN section");
    found_flat |= line.ends_with(" state_8__\\$_OR__A_Y");
    found_hier |= line.ends_with(" u_core\\.cnt_\\$_NAND__Y");
  }
  require(found_flat && found_hier, "SPEF name map is missing the escaped '$' nets");

  spef::SpefReader reader;
  require(reader.read(spef_path.string()), "SpefReader failed to parse emitted SPEF");
  reader.expandName();
  const spef::Exchange* exchange = reader.getSpefFile();
  require(exchange != nullptr && exchange->nets.size() == 3, "unexpected net count after SPEF round-trip");
  std::set<std::string> parsed_nets;
  for (const auto& parsed_net : exchange->nets) {
    parsed_nets.insert(parsed_net.name);
  }
  require(parsed_nets.count("n0") == 1 && parsed_nets.count("state_8__$_OR__A_Y") == 1
              && parsed_nets.count("u_core.cnt_$_NAND__Y") == 1,
          "SpefReader did not unescape SPEF net names back to netlist identifiers");

  // Also cover the directory post-pass used for precompiled backends (e.g. libircx_ics55.so).
  const std::filesystem::path sanitize_dir = fixture.directory() / "sanitize_spef_dir/spef_writer";
  std::filesystem::create_directories(sanitize_dir);
  const std::filesystem::path raw_spef_path = sanitize_dir / "ics55_raw.spef";
  {
    std::ofstream raw_spef(raw_spef_path);
    require(raw_spef.is_open(), "cannot write raw SPEF fixture");
    raw_spef << "*SPEF \"IEEE 1481-1998\"\n"
             << "*DESIGN \"ics55_raw\"\n"
             << "*DIVIDER /\n"
             << "*DELIMITER :\n"
             << "*BUS_DELIMITER []\n"
             << "*T_UNIT 1.0 NS\n"
             << "*C_UNIT 1.0 FF\n"
             << "*R_UNIT 1.0 OHM\n"
             << "*L_UNIT 1.0 HENRY\n\n"
             << "*NAME_MAP\n"
             << "*782 state_8__$_OR__A_Y\n\n"
             << "*PORTS\n\n"
             << "*D_NET *782 1.0\n"
             << "*CONN\n"
             << "*N *782:1 *C 1.0 1.0\n"
             << "*CAP\n"
             << "1 *782:1 1.0\n"
             << "*RES\n"
             << "1 *782:1 *782:1 0.5\n"
             << "*END\n";
  }
  require(ircx::sanitizeSPEFDirectory(sanitize_dir.parent_path()), "directory SPEF sanitization failed");

  std::ifstream sanitized(raw_spef_path);
  require(sanitized.is_open(), "cannot reopen sanitized SPEF");
  bool found_sanitized_map = false;
  for (std::string line; std::getline(sanitized, line);) {
    require(!hasUnescapedDollar(line), "sanitized SPEF contains an unescaped '$': " + line);
    require(line != "*PORTS", "sanitized SPEF kept empty *PORTS section");
    require(line != "*CONN", "sanitized SPEF kept pinless *CONN section");
    found_sanitized_map |= (line == "*782 state_8__\\$_OR__A_Y");
  }
  require(found_sanitized_map, "sanitized SPEF missing escaped *NAME_MAP entry");

  spef::SpefReader sanitized_reader;
  require(sanitized_reader.read(raw_spef_path.string()), "SpefReader failed to parse sanitized SPEF");
  sanitized_reader.expandName();
  const spef::Exchange* sanitized_exchange = sanitized_reader.getSpefFile();
  require(sanitized_exchange != nullptr && sanitized_exchange->nets.size() == 1
              && sanitized_exchange->nets.front().name == "state_8__$_OR__A_Y",
          "SpefReader round-trip failed on sanitized SPEF");

  // No-*NAME_MAP SPEF with escaped identifiers + report_geometry: true comment + *D_PNET / *PHYSICAL_PORTS.
  const std::filesystem::path no_map_spef = sanitize_dir / "no_name_map.spef";
  {
    std::ofstream out_stream(no_map_spef);
    require(out_stream.is_open(), "cannot write no_name_map SPEF fixture");
    out_stream << "*SPEF \"IEEE 1481-1998\"\n"
               << "*DESIGN \"no_map\"\n"
               << "*DIVIDER /\n"
               << "*DELIMITER :\n"
               << "*BUS_DELIMITER []\n"
               << "*T_UNIT 1.0 NS\n"
               << "*C_UNIT 1.0 FF\n"
               << "*R_UNIT 1.0 OHM\n"
               << "*L_UNIT 1.0 HENRY\n\n"
               << "*NAME_MAP\n"
               << "// empty name map comment\n\n"
               << "*PORTS\n\n"
               << "// *LAYER_MAP\n"
               << "// *1 M1\n\n"
               << "*D_NET flat_$_net 1.0\n"
               << "*CONN\n"
               << "*I u_cell:pin_$A I *C 1.0 1.0 // $llx=0\n"
               << "*CAP\n"
               << "1 u_cell:pin_$A 1.0\n"
               << "*RES\n"
               << "1 u_cell:pin_$A u_cell:pin_$A 0.5\n"
               << "*END\n\n"
               << "*PORTS\n"
               << "*PHYSICAL_PORTS\n"
               << "*END\n";
  }
  require(ircx::sanitizeSPEFFile(no_map_spef), "sanitizeSPEFFile failed on no_name_map.spef");

  std::ifstream no_map_in(no_map_spef);
  require(no_map_in.is_open(), "cannot reopen no_name_map.spef");
  bool saw_layer_map_comment = false;
  bool saw_geometry_comment = false;
  for (std::string line; std::getline(no_map_in, line);) {
    require(line != "*NAME_MAP", "empty *NAME_MAP followed by comment was not stripped");
    require(line != "*PORTS", "empty *PORTS followed by // *LAYER_MAP or *PHYSICAL_PORTS was not stripped");
    require(!hasUnescapedDollar(line), "no_name_map SPEF contains an unescaped '$': " + line);
    saw_layer_map_comment |= (line == "// *LAYER_MAP");
    saw_geometry_comment |= (line == "*I u_cell:pin_\\$A I *C 1.0 1.0 // $llx=0");
  }
  require(saw_layer_map_comment, "sanitizer stripped // *LAYER_MAP comment");
  require(saw_geometry_comment, "sanitizer altered trailing // $llx geometry comment or failed to escape *I pin");

  spef::SpefReader no_map_reader;
  require(no_map_reader.read(no_map_spef.string()), "SpefReader failed on no_name_map.spef");
  no_map_reader.expandName();
  const spef::Exchange* no_map_exchange = no_map_reader.getSpefFile();
  require(no_map_exchange != nullptr && no_map_exchange->nets.size() == 1
              && no_map_exchange->nets.front().name == "flat_$_net"
              && no_map_exchange->nets.front().conns.size() == 1
              && no_map_exchange->nets.front().conns.front().pin_port_name == "u_cell:pin_$A",
          "expandAllNames skipped unescaping when *NAME_MAP was empty");

  ircx::compare_spef::SpefReader cmp_reader;
  ircx::compare_spef::Data cmp_data;
  require(cmp_reader.read(no_map_spef.string(), cmp_data), "compare_spef::SpefReader failed on no_name_map.spef");
  require(cmp_data.nets.size() == 1 && cmp_data.nets.front().name == "flat_$_net"
              && cmp_data.nets.front().pins.size() == 1
              && cmp_data.nets.front().pins.front().name == "u_cell:pin_$A",
          "compare_spef::NameExpander failed to unescape non-indexed names");

  // Config-relative output sanitization must not depend on process CWD.
  const std::filesystem::path cfg_dir = fixture.directory() / "cfg_bundle";
  const std::filesystem::path rel_out = cfg_dir / "rel_out";
  std::filesystem::create_directories(rel_out);
  const std::filesystem::path cfg_spef = rel_out / "rel.spef";
  {
    std::ofstream s(cfg_spef);
    s << "*SPEF \"IEEE 1481-1998\"\n*DESIGN \"rel\"\n*DIVIDER /\n*DELIMITER :\n*BUS_DELIMITER []\n"
         "*T_UNIT 1.0 NS\n*C_UNIT 1.0 FF\n*R_UNIT 1.0 OHM\n*L_UNIT 1.0 HENRY\n*PORTS\n*D_NET sig_$x 1.0\n*CAP\n*RES\n*END\n";
  }
  const std::filesystem::path cfg_path = cfg_dir / "rcx.json";
  {
    std::ofstream s(cfg_path);
    s << "{\"output\": \"rel_out\"}\n";
  }
  const std::filesystem::path saved_cwd = std::filesystem::current_path();
  struct CwdRestorer {
    std::filesystem::path prev;
    ~CwdRestorer()
    {
      std::error_code ec;
      std::filesystem::current_path(prev, ec);
    }
  } restorer{saved_cwd};
  std::filesystem::current_path(fixture.directory());
  require(ircx::sanitizeSPEFOutputsForConfig(cfg_path), "sanitizeSPEFOutputsForConfig failed");
  std::ifstream cfg_in(cfg_spef);
  std::string cfg_line;
  bool saw_escaped_sig = false;
  while (std::getline(cfg_in, cfg_line)) {
    require(cfg_line != "*PORTS", "config-relative sanitizer missed empty *PORTS");
    saw_escaped_sig |= (cfg_line == "*D_NET sig_\\$x 1.0");
  }
  require(saw_escaped_sig, "config-relative sanitizer failed to escape relative output dir SPEF");
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
    } else if (scenario == "spef_escaping") {
      checkSpefEscaping();
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
