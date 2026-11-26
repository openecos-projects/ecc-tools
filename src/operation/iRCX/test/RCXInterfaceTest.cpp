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
#include "SPEFWriter.hpp"
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
    write("mapping.map", "conducting_layers\nM1 metal1\nvia_layers\nmarker_layers\nremove_layers\nPO\nCT\n");
    write("typical.itf", "CONDUCTOR metal1 { THICKNESS=0.1 RPSQ=0.12 }\n");
    write("typical.captab", "P M1 SUBSTRATE - .2\n-1 -1 0 0 0\n-1 .2 0 0 0\n.2 -1 0 0 0\n.2 .2 0 0 0\n");
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

  void addDensityMetal()
  {
    auto* design = _def_service.get_design();
    auto* pin = design->get_io_pin_list()->add_pin_list("floating");
    auto* shape = new idb::IdbLayerShape();
    shape->set_layer(_layer);
    shape->add_rect(0, 0, 100, 100);
    shape->add_rect(200, 0, 300, 100);
    pin->get_port_box_list().push_back(shape);
    design->get_fill_list()->add_fill_layer(_layer)->add_rect(400, 0, 500, 100);
    auto make_via = [&](int32_t x) {
      auto* master = new idb::IdbViaMaster();
      master->get_bottom_layer_shape()->set_layer(_layer);
      master->get_bottom_layer_shape()->add_rect(0, 0, 100, 100);
      master->get_bottom_layer_shape()->add_rect(200, 0, 300, 100);
      master->get_top_layer_shape()->set_layer(_layer);
      master->get_top_layer_shape()->add_rect(400, 0, 500, 100);
      auto* via = new idb::IdbVia();
      via->set_instance(master);
      via->set_coordinate(x, 0);
      return via;
    };
    auto* wire = design->get_net_list()->get_net_list().front()->get_wire_list()->add_wire()->add_segment();
    wire->set_is_via(true);
    wire->set_via(make_via(600));
    auto* special = design->get_special_net_list()->add_net("power")->get_wire_list()->add_wire()->add_segment();
    special->set_is_via(true);
    special->set_via(make_via(1200));
    special->set_layer(_layer);
    special->set_route_width(100);
    special->add_flush_point(3000,1000,0);
    special->add_point(4000,1000);
    special->add_flush_point(4000,2000,25);
    auto* patched = design->get_special_net_list()->get_net_list().front()->get_wire_list()->add_wire()->add_segment();
    patched->set_layer(_layer);
    patched->set_route_width(100);
    patched->add_point(3000,3000);
    patched->add_point(4000,3000);
    patched->set_is_rect(true);
    patched->set_delta_rect(5000,3000,5100,3100);
    patched->set_bounding_box();
    auto* fill = design->get_fill_list()->add_fill_via(make_via(50));
    fill->add_coordinate(1800, 0);
    fill->add_coordinate(2400, 0);
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

void checkPatternGridInput(const std::string& scenario)
{
  InterfaceFixture fixture;
  fixture.prepare();
  const std::string grid = "P M1 SUBSTRATE - .2\n"
                           "-1 -1 8 0 0\n-1 .2 5 0 3\n.2 -1 5 3 0\n.2 .2 2 3 3\n";
  const std::string v2_grid = "IRCX_PATTERN_GRID_V2\nP M1 SUBSTRATE - .2\n"
                              "-1 -1 8 0 0 0 0\n-1 .2 5 0 3 0 0\n.2 -1 5 3 0 0 0\n.2 .2 2 3 3 0 0\n";
  const std::string a_table = "A metal1 OVER SUBSTRATE\n.1 0 2\n2 0 3\n";
  const std::string b_table = "B metal1 OVER SUBSTRATE UNDER metal2\n.1 0 2\n2 0 3\n";
  const bool invalid = scenario.starts_with("reject_");
  std::ofstream(fixture.directory() / "typical.captab")
      << (scenario == "reject_a" ? a_table : scenario == "reject_b" ? b_table
          : scenario == "reject_empty" ? "" : scenario == "reject_mixed" ? grid + a_table
          : scenario == "reject_v2" ? v2_grid : grid);
  if (scenario == "override" || scenario == "override_corners" || scenario == "override_v2") {
    const auto path = fixture.directory() / "explicit-grid.captab";
    std::ofstream(path) << (scenario == "override_v2" ? v2_grid
        : "P M1 SUBSTRATE - .2\n-1 -1 4 0 0\n-1 .2 4 0 0\n.2 -1 4 0 0\n.2 .2 4 0 0\n");
    setenv("IRCX_PATTERN_GRID", path.c_str(), 1);
  }
  if (scenario == "corners" || scenario == "override_corners") {
    std::ofstream(fixture.directory() / "other.captab")
        << "P M1 SUBSTRATE - .2\n-1 -1 4 0 0\n-1 .2 4 0 0\n.2 -1 4 0 0\n.2 .2 4 0 0\n";
    std::ofstream(fixture.directory() / "ircx.config") << R"({
      "thread_num": 2, "output":"output", "mapping_file":"mapping.map",
      "corners":[
        {"name":"TYPICAL", "process_file":"typical.itf", "captab_file":"typical.captab", "temperature":[25,125]},
        {"name":"OTHER", "process_file":"typical.itf", "captab_file":"other.captab"}]
    })";
  }
  if (scenario == "overlap") fixture.addNet("n1", 1050);
  if (scenario == "coupling" || scenario == "corners") fixture.addNet("n1", 1400);
  if (scenario == "process_names") {
    auto process_grid = grid;
    process_grid.replace(process_grid.find("M1"), 2, "metal1");
    std::ofstream(fixture.directory() / "typical.captab") << process_grid;
  }
  if (invalid) {
    bool rejected = false;
    try {
      fixture.init();
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("grid") != std::string::npos;
    }
    require(rejected, "non-P captab input must fail during initialization: " + scenario);
    return;
  }
  fixture.init();
  if (scenario == "process_names") {
    bool rejected = false;
    try {
      RCXI.runRCX();
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("missing pattern grid context: M1|SUBSTRATE|-") != std::string::npos;
    }
    require(rejected, "captab must use design layer names without a process-name fallback");
    return;
  }
  if (scenario == "removed_mode") setenv("IRCX_MULTI_MODE", "off", 1);
  if (scenario == "removed_prebuilt") setenv("IRCX_MULTI_MODE", "prebuilt", 1);
  if (scenario == "removed_rank") setenv("IRCX_MULTI_RANK", "3", 1);
  if (scenario == "removed_table") setenv("IRCX_PATTERN_TABLE", "unused.captab", 1);
  if (scenario == "removed_interlayer") setenv("IRCX_XLAYER_TABLE", "unused.captab", 1);
  if (scenario == "override_corners" || scenario == "override_v2" || scenario.starts_with("removed_")) {
    bool rejected = false;
    try {
      RCXI.runRCX();
    } catch (const std::runtime_error& error) {
      const std::string diagnostic(error.what());
      rejected = diagnostic.find(scenario == "override_corners" ? "exactly one process corner"
                                 : scenario == "override_v2" ? "grid"
                                 : scenario == "removed_interlayer" ? "remove IRCX_XLAYER_TABLE" : "PatternGrid") != std::string::npos;
    }
    require(rejected, "unsupported extraction setting did not fail explicitly: " + scenario);
    return;
  }
  RCXI.runRCX();
  auto& database = RCXDM.getDatabase();
  const int corners = scenario == "corners" ? 3 : 1;
  require(database.get_corner_data_list().size() == corners, "unexpected number of P-grid corners");
  for (int corner = 0; corner < corners; ++corner) {
    for (int net = 0; net < database.get_layout_data().get_regular_net_num(); ++net) {
      const auto& ground = database.get_rc_data().get_corner_net_ground_capacitance_list(ircx::CornerNetIdx(corner, net));
      // The 1 um route has 0.1 um metal extensions at both ends.
      const double expected = scenario == "override" || corner == 2 ? 4.8 : scenario == "coupling" || scenario == "corners" ? 6.0 : 9.6;
      require(ground.size() == 1 && std::isfinite(ground.front()) && std::abs(ground.front() - expected) < 1e-10,
              "unexpected P-grid capacitance: got " + (ground.empty() ? std::string("no edges") : std::to_string(ground.front())));
    }
  }
  if (scenario == "coupling" || scenario == "corners") {
    const auto& coupling = database.get_rc_data().get_merged_coupling_capacitance_map();
    require(coupling.size() == 1, "grid coupling must be assembled once per reciprocal pair");
    for (int corner = 0; corner < corners; ++corner)
      require(std::abs(coupling.begin()->second.at(corner) - (corner == 2 ? 0.0 : 3.6)) < 1e-10,
              "coupling used the wrong corner grid");
  }
  RCXI.destroyRCX();
}

void checkDensityMetal()
{
  InterfaceFixture fixture;
  fixture.prepare();
  fixture.addDensityMetal();
  fixture.init();
  const auto& shapes = RCXDM.getDatabase().get_layout_data().get_density_metal_shapes();
  require(shapes.size() == 19, "pins, via landings, special path legs, and fills must reach density geometry");
  std::set<int32_t> starts;
  bool horizontal=false,vertical=false,patch=false,patched_wire=false;
  for (const auto& shape : shapes) {
    if (gtl::yl(shape.get_shape())>0) {
      horizontal |= shape.get_shape()==GTLRectInt(3000,950,4050,1050);
      vertical |= shape.get_shape()==GTLRectInt(3950,950,4050,2025);
      patch |= shape.get_shape()==GTLRectInt(5000,3000,5100,3100);
      patched_wire |= shape.get_shape()==GTLRectInt(3000,2950,4000,3050);
      continue;
    }
    require(gtl::xh(shape.get_shape()) - gtl::xl(shape.get_shape()) == 100, "pin rectangles were replaced by a bounding box");
    starts.insert(gtl::xl(shape.get_shape()));
  }
  require(starts == std::set<int32_t>{0, 200, 400, 600, 800, 1000, 1200, 1400, 1600, 1800, 2000, 2200, 2400, 2600, 2800},
          "density geometry has incorrect database coordinates");
  require(horizontal && vertical,"every special route leg and endpoint extension must survive a via-marked path");
  require(patch && patched_wire,"a patch and route body in one special path must both survive");
  RCXI.wrapDensityMetal();
  require(shapes.size() == 19, "rebuilding the physical shape snapshot must not accumulate stale rectangles");
  RCXI.destroyRCX();
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
  require(RCXI.getSPEFName("bus[0]_reg_p") == "bus\\[0\\]_reg_p", "literal brackets in a scalar name were not escaped");
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

  // Also cover sanitizing externally generated SPEF files in an output directory.
  const std::filesystem::path sanitize_dir = fixture.directory() / "sanitize_spef_dir/spef_writer";
  std::filesystem::create_directories(sanitize_dir);
  const std::filesystem::path raw_spef_path = sanitize_dir / "external_raw.spef";
  {
    std::ofstream raw_spef(raw_spef_path);
    require(raw_spef.is_open(), "cannot write raw SPEF fixture");
    raw_spef << "*SPEF \"IEEE 1481-1998\"\n"
             << "*DESIGN \"external_raw\"\n"
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

void checkSpefWriter(bool check_names)
{
  InterfaceFixture fixture;
  fixture.prepare();
  fixture.init();
  RCXI.runRCX();
  auto& database = RCXDM.getDatabase();
  const auto path = fixture.directory() / "output/spef_writer/interface_test_TYPICAL_25C.spef";
  auto write_spef = [&]() {
    ircx::SPEFWriter::initInst();
    ircx::SPEFWriter::getInst().write();
    ircx::SPEFWriter::destroyInst();
    std::ifstream in(path);
    require(in.is_open(), "writer did not produce SPEF");
    return std::string(std::istreambuf_iterator<char>(in), {});
  };

  if (check_names) {
    auto nodes = database.get_topo_pool().get_net_node_list(0);
    require(nodes.size() == 2, "writer fixture needs two nodes");
    nodes[0].set_pin_name(R"(port\:1)");
    nodes[1].set_pin_name(R"(u\.x:B[3])");
    auto& names = database.get_spef_name_data();
    names.get_port_name_list().push_back(R"(port\:1)");
    names.get_port_direction_list().push_back(ircx::Direction::kInput);
    ircx::Pin pin;
    pin.set_pin_name(R"(u\.x:B[3])");
    pin.set_direction(ircx::Direction::kInput);
    database.get_layout_data().get_net_list()[0].get_pin_list().push_back(pin);
    const std::string text = write_spef();
    require(text.find("*I u\\.x:B[3] I") != std::string::npos, "writer changed an unmapped bus pin into a scalar name");
    spef::SpefReader reader;
    require(reader.read(path.string()), "cannot read writer pin names");
    reader.expandName();
    const auto& connections = reader.getSpefFile()->nets.at(0).conns;
    require(connections.size() == 2 && connections[0].conn_type == spef::ConnectionType::kExternal
                && connections[0].pin_port_name == "port:1" && connections[1].pin_port_name == "u.x:B[3]",
            "writer treated an escaped colon in a port as a pin delimiter");
  } else {
    auto& ground = database.get_rc_data().get_corner_net_ground_capacitance_list(ircx::CornerNetIdx(0, 0));
    require(ground.size() == 1, "writer fixture needs one capacitance edge");
    ground[0] = 2.0;
    const std::string nonempty = write_spef();
    require(nonempty.find("\n*CAP\n") != std::string::npos && nonempty.find("\n*RES\n") != std::string::npos,
            "writer omitted a nonempty CAP or RES section");
    ground[0] = 0.0;
    const std::string zero_cap = write_spef();
    require(zero_cap.find("\n*CAP\n") == std::string::npos && zero_cap.find("\n*RES\n") != std::string::npos,
            "writer emitted an empty CAP section or dropped a nonempty RES section");
    const auto old_nodes = database.get_topo_pool().get_net_node_list(0);
    std::vector<ircx::TopoNode> nodes(old_nodes.begin(), old_nodes.end());
    ircx::TopoPool isolated;
    isolated.add_net(std::move(nodes), {});
    database.get_topo_pool() = std::move(isolated);
    const std::string no_edges = write_spef();
    require(no_edges.find("\n*D_NET ") != std::string::npos && no_edges.find("\n*CAP\n") == std::string::npos
                && no_edges.find("\n*RES\n") == std::string::npos,
            "writer emitted empty CAP/RES sections for a net without edges");
  }
  RCXI.destroyRCX();
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
    for (const char* name : {"IRCX_MULTI_MODE", "IRCX_MULTI_RANK", "IRCX_PATTERN_GRID", "IRCX_PATTERN_TABLE", "IRCX_MULTI_OUTPUT",
                             "IRCX_XLAYER_TABLE"}) unsetenv(name);
    const std::string scenario = argc > 1 ? argv[1] : "extract";
    if (scenario == "extract") {
      checkExtraction();
    } else if (scenario.starts_with("captab_")) {
      checkPatternGridInput(scenario.substr(7));
    } else if (scenario == "density_metal") {
      checkDensityMetal();
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
    } else if (scenario == "spef_writer_sections") {
      checkSpefWriter(false);
    } else if (scenario == "spef_writer_names") {
      checkSpefWriter(true);
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
