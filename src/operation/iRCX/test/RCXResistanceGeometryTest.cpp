// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
// ***************************************************************************************
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "RCXInterface.hpp"
#include "ResExtractor.hpp"
#include "Utility.hpp"
#include "VarProcessor.hpp"
#include "idm.h"

namespace {

using namespace ircx;

void expectNear(double actual, double expected)
{
  if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-10 * std::max(1.0, std::abs(expected))) {
    throw std::runtime_error("expected " + std::to_string(expected) + ", got " + std::to_string(actual));
  }
}

class ResistanceFixture
{
 public:
  ResistanceFixture()
  {
    auto pattern = (std::filesystem::temp_directory_path() / "ircx-resistance-XXXXXX").string();
    if (mkdtemp(pattern.data()) == nullptr) {
      throw std::runtime_error("cannot create resistance test directory");
    }
    _directory = pattern;
    Logger::initInst();
    Utility::initInst();
    DataManager::initInst();
    VarProcessor::initInst();
    ResExtractor::initInst();
    // DataManager::input imports a real design before loading its ITF corners.
    dmInst->set_idb_lef_service(&_lef_service);
    dmInst->set_idb_def_service(&_def_service);
    auto* design = _def_service.get_design();
    design->get_units()->set_microns_dbu(1000);
    design->get_net_list()->add_net("n0");
    design->get_net_list()->add_net("n1");
  }

  ~ResistanceFixture()
  {
    ResExtractor::destroyInst();
    VarProcessor::destroyInst();
    DataManager::destroyInst();
    RCXInterface::destroyInst();
    Utility::destroyInst();
    Logger::destroyInst();
    dmInst->set_idb_def_service(nullptr);
    dmInst->set_idb_lef_service(nullptr);
    std::error_code error;
    std::filesystem::remove_all(_directory, error);
  }

  void loadProcess(const std::string& body, double scale = 1.0, double temperature = 25.0)
  {
    write("process.itf", "GLOBAL_TEMPERATURE = 25\nHALF_NODE_SCALE_FACTOR = " + std::to_string(scale) + "\n" + body);
    write("mapping.map", "M1 metal1\nV1 via1\n");
    write("empty.captab", "");
    write("rcx.json", R"({"thread_num":2,"mapping_file":"mapping.map","output":"output",
      "corners":[{"name":"TEST","itf_file":"process.itf","captab_file":"empty.captab","temperature":[)"
                          + std::to_string(temperature) + "]}]}");
    std::map<std::string, std::any> config{{"-config", (_directory / "rcx.json").string()}};
    RCXDM.input(config);
    auto& layers = RCXDM.getDatabase().get_layer_table();
    layers.register_design_layer(0, "M1");
    layers.register_design_layer(1, "V1");
    if (RCXDM.getDatabase().get_corner_data_list().size() != 1) {
      throw std::runtime_error("ITF process was not loaded");
    }
  }

  void addGeometry(bool is_via = false, int32_t cut_x = 100, int32_t cut_y = 200, bool vertical = false, bool reversed = false)
  {
    auto& database = RCXDM.getDatabase();
    database.get_net_env_list().resize(2);
    for (int32_t net_idx = 0; net_idx < 2; ++net_idx) {
      std::vector<TopoNode> nodes{TopoNode(net_idx), TopoNode(net_idx)};
      nodes[0].set_point(GTLPointInt(0, 0));
      nodes[1].set_point(is_via ? GTLPointInt(0, 0) : vertical ? GTLPointInt(0, 1000) : GTLPointInt(1000, 0));
      TopoEdge edge(net_idx);
      edge.set_start_node_idx(reversed ? 1 : 0);
      edge.set_end_node_idx(reversed ? 0 : 1);
      edge.set_layer_idx(is_via ? 1 : 0);
      if (is_via) {
        edge.set_via_name("cut");
        edge.set_shape(GTLRectInt(0, 0, cut_x, cut_y));
      } else {
        edge.set_shape(vertical ? GTLRectInt(-100, 0, 100, 1000) : GTLRectInt(0, -100, 1000, 100));
      }
      database.get_topo_pool().add_net(std::move(nodes), {edge});
      EdgeEnvInterval interval;
      interval.set_start_coord(0);
      interval.set_end_coord(1000);
      database.get_net_env_list()[net_idx].append_edge_interval_list({interval});
    }
    RCXVP.process();
  }

  void extractAndExpect(double expected)
  {
    auto& database = RCXDM.getDatabase();
    auto before = database.get_corner_net_etch_profile_pool();
    RCXRE.extract();
    for (int32_t net_idx = 0; net_idx < 2; ++net_idx) {
      const CornerNetIdx index(0, net_idx);
      expectNear(database.get_rc_data().get_corner_net_resistance_list(index).at(0), expected);
      const auto old_intervals = before.get_item(index).get_edge_interval_list(0);
      const auto new_intervals = database.get_corner_net_etch_profile_pool().get_item(index).get_edge_interval_list(0);
      if (old_intervals.size() != new_intervals.size()) {
        throw std::runtime_error("shared geometry interval count changed");
      }
      for (size_t i = 0; i < old_intervals.size(); ++i) {
        expectNear(new_intervals[i].get_width(), old_intervals[i].get_width());
        expectNear(new_intervals[i].get_center(), old_intervals[i].get_center());
        expectNear(new_intervals[i].get_thickness(), old_intervals[i].get_thickness());
        expectNear(new_intervals[i].get_start_coord(), old_intervals[i].get_start_coord());
        expectNear(new_intervals[i].get_end_coord(), old_intervals[i].get_end_coord());
        expectNear(new_intervals[i].get_lower_spacing(), old_intervals[i].get_lower_spacing());
        expectNear(new_intervals[i].get_upper_spacing(), old_intervals[i].get_upper_spacing());
        expectNear(new_intervals[i].get_capacitance_width(), old_intervals[i].get_capacitance_width());
      }
    }
  }

  void expectInvalidGeometry()
  {
    try {
      RCXRE.extract();
    } catch (const std::exception&) {
      return;
    }
    throw std::runtime_error("invalid resistance geometry was accepted");
  }

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

void checkWire(const std::string& properties, double expected, double scale = 1.0, double temperature = 25.0,
               bool vertical = false, bool reversed = false)
{
  ResistanceFixture fixture;
  fixture.loadProcess("CONDUCTOR metal1 { THICKNESS=0.1 " + properties + " }", scale, temperature);
  fixture.addGeometry(false, 100, 200, vertical, reversed);
  fixture.extractAndExpect(expected);
}

void checkVia(const std::string& properties, double expected, int32_t cut_x = 100, int32_t cut_y = 200,
              double scale = 1.0, double temperature = 25.0)
{
  ResistanceFixture fixture;
  fixture.loadProcess("VIA via1 { FROM=metal1 TO=metal2 " + properties + " }", scale, temperature);
  fixture.addGeometry(true, cut_x, cut_y);
  fixture.extractAndExpect(expected);
}

using Test = std::pair<std::string, std::function<void()>>;

std::vector<Test> wireTests()
{
  return {
      {"no scalar etch", [] { checkWire("RPSQ=0.12", 0.6); }},
      {"resistive shrink", [] { checkWire("RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.02", 0.75); }},
      {"resistive expansion", [] { checkWire("RPSQ=0.12 RESISTIVE_ONLY_ETCH=-0.025", 0.48); }},
      {"scale before scalar etch", [] { checkWire("RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.02", 1.0, 0.5); }},
      {"vertical reversed wire", [] { checkWire("RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.02", 0.75, 1.0, 25.0, true, true); }},
      {"common plus resistive etch", [] { checkWire("RPSQ=0.12 ETCH=0.005 RESISTIVE_ONLY_ETCH=0.02", 0.8); }},
      {"silicon width for RHO", [] { checkWire("RHO=0.012 RESISTIVE_ONLY_ETCH=0.02", 0.75); }},
      {"silicon width for RPSQ table", [] {
         checkWire("RESISTIVE_ONLY_ETCH=0.02 RPSQ_VS_SI_WIDTH { (0.16,0.08) (0.20,0.12) }", 0.5);
       }},
      {"silicon width for temperature table", [] {
         checkWire("RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.02 CRT_VS_SI_WIDTH { (0.16,0.002,0) (0.20,0.004,0) }", 0.9, 1.0, 125.0);
       }},
      {"exclude capacitance only etch", [] {
         checkWire(R"(RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.02 CAPACITIVE_ONLY_ETCH=0.05
           ETCH_VS_WIDTH_AND_SPACING CAPACITIVE_ONLY { WIDTHS {0.2} SPACINGS {0.1} VALUES {0.03} })", 0.75);
       }},
      {"sequential tables before scalar etch", [] {
         checkWire(R"(RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.02
           ETCH_VS_WIDTH_AND_SPACING { WIDTHS {0.2} SPACINGS {0.1} VALUES {0.01} }
           ETCH_VS_WIDTH_AND_SPACING RESISTIVE_ONLY { WIDTHS {0.16 0.20} SPACINGS {0.1} VALUES {0.005 0.015} })", 1.0);
       }},
  };
}

std::vector<Test> viaTests()
{
  return {
      {"reference area", [] { checkVia("AREA=0.01 RPV=4", 4.0, 100, 100); }},
      {"larger rectangular cut", [] { checkVia("AREA=0.01 RPV=4", 2.0); }},
      {"smaller cut", [] { checkVia("AREA=0.01 RPV=4", 8.0, 50, 100); }},
      {"squared geometry scale", [] { checkVia("AREA=0.01 RPV=4", 8.0, 100, 200, 0.5); }},
      {"temperature after area conversion", [] { checkVia("AREA=0.01 RPV=4 CRT1=0.002 CRT2=0.00001", 2.6, 100, 200, 1.0, 125.0); }},
      {"area after resistance etch", [] {
         checkVia(R"(AREA=0.01 RPV=4
           ETCH_VS_WIDTH_AND_LENGTH RESISTIVE_ONLY { WIDTHS {0.1} LENGTHS {0.2} VALUES {(0.025,0.01)} })", 3.3333333333333333);
       }},
      {"ignore capacitance only via etch", [] {
         checkVia(R"(AREA=0.01 RPV=4
           ETCH_VS_WIDTH_AND_LENGTH CAPACITIVE_ONLY { WIDTHS {0.1} LENGTHS {0.2} VALUES {(0.025,0.01)} })", 2.0);
       }},
      {"area table is not scaled again", [] { checkVia("AREA=0.01 RPV_VS_AREA { (0.01,4) (0.02,3) (0.04,1.5) }", 3.0); }},
  };
}

std::vector<Test> invalidTests()
{
  return {
      {"collapsed wire", [] {
         ResistanceFixture fixture;
         fixture.loadProcess("CONDUCTOR metal1 { THICKNESS=0.1 RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.1 }");
         fixture.addGeometry();
         fixture.expectInvalidGeometry();
       }},
      {"zero cut area", [] {
         ResistanceFixture fixture;
         fixture.loadProcess("VIA via1 { AREA=0.01 RPV=4 }");
         fixture.addGeometry(true, 0, 200);
         fixture.expectInvalidGeometry();
       }},
      {"missing reference area", [] {
         ResistanceFixture fixture;
         fixture.loadProcess("VIA via1 { RPV=4 }");
         fixture.addGeometry(true);
         fixture.expectInvalidGeometry();
       }},
      {"nonpositive reference area", [] {
         ResistanceFixture fixture;
         fixture.loadProcess("VIA via1 { AREA=-0.01 RPV=4 }");
         fixture.addGeometry(true);
         fixture.expectInvalidGeometry();
       }},
      {"nonfinite reference area", [] {
         ResistanceFixture fixture;
         fixture.loadProcess("VIA via1 { AREA=0.01 RPV=4 }");
         RCXDM.getDatabase().get_corner_data_list()[0].get_process_via_list()[0].set_area(std::numeric_limits<double>::infinity());
         fixture.addGeometry(true);
         fixture.expectInvalidGeometry();
       }},
  };
}

void checkExistingScaledGeometry(double scale)
{
  ResistanceFixture fixture;
  fixture.loadProcess(R"(CONDUCTOR metal1 { THICKNESS=0.1 RHO=0.02
    ETCH_VS_WIDTH_AND_SPACING { WIDTHS {0.05 0.4} SPACINGS {0.05 0.5} VALUES {0.02 0.02 0.02 0.02} } })");
  RCXDM.getDatabase().get_corner_data_list()[0].set_half_node_scale_factor(scale);
  fixture.addGeometry();
  if (!std::isfinite(scale) || scale <= 0.0) {
    fixture.expectInvalidGeometry();
  } else {
    // Independent hand-calculated expectations for the existing scale cases (one micron wire).
    const std::map<double, double> expected{{0.9, 1.2857142857142858}, {0.73, 1.3773584905660377}, {1.0, 1.25}};
    fixture.extractAndExpect(expected.at(scale));
  }
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    const std::string group = argc > 1 ? argv[1] : "0.9";
    std::vector<Test> tests;
    if (group == "wire_etch") {
      tests = wireTests();
    } else if (group == "via_area") {
      tests = viaTests();
    } else if (group == "invalid_geometry") {
      tests = invalidTests();
    } else {
      checkExistingScaledGeometry(std::stod(group));
      return EXIT_SUCCESS;
    }
    int failures = 0;
    for (const auto& [name, test] : tests) {
      try {
        test();
        std::cout << "PASS: " << name << '\n';
      } catch (const std::exception& error) {
        std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        ++failures;
      }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " cases passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
