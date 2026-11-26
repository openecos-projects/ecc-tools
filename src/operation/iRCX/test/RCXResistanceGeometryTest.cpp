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
#include <sys/wait.h>
#include <unistd.h>

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
    loadInput("GLOBAL_TEMPERATURE = 25\nHALF_NODE_SCALE_FACTOR = " + std::to_string(scale) + "\n" + body,
              "process.itf", true, temperature);
  }

  void loadInput(const std::string& text, const std::string& filename = "process.data", bool legacy = false, double temperature = 25.0)
  {
    write(filename, text);
    loadConfiguration(std::string(R"([{"name":"TEST",")") + (legacy ? "itf_file" : "process_file") + "\":\"" + filename
                      + R"(","captab_file":"zero.captab","temperature":[)" + std::to_string(temperature) + "]}]");
    if (RCXDM.getDatabase().get_corner_data_list().size() != 1) {
      throw std::runtime_error("process was not loaded");
    }
  }

  void writeProcess(const std::string& name, const std::string& text) { write(name, text); }
  void addRoutingLayer(const std::string& name)
  {
    auto* layers = _lef_service.get_layout()->get_layers();
    layers->add_routing_layer(layers->set_layer(name, "ROUTING"));
  }

  void loadConfiguration(const std::string& corners)
  {
    write("mapping.map", mapping_text);
    write("zero.captab", "P M1 SUBSTRATE - .2\n-1 -1 0 0 0\n-1 .2 0 0 0\n.2 -1 0 0 0\n.2 .2 0 0 0\n");
    write("rcx.json", R"({"thread_num":2,"mapping_file":"mapping.map","output":"output","corners":)" + corners + "}");
    std::map<std::string, std::any> config{{"-config", (_directory / "rcx.json").string()}};
    RCXDM.input(config);
    auto& layers = RCXDM.getDatabase().get_layer_table();
    layers.register_design_layer(0, "M1");
    layers.register_design_layer(1, "V1");
  }

  void addGeometry(bool is_via = false, int32_t cut_x = 100, int32_t cut_y = 200, bool vertical = false, bool reversed = false,
                   bool adjacent = false)
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
        const int32_t center = adjacent ? 300 * net_idx : 0;
        edge.set_shape(vertical ? GTLRectInt(-100, 0, 100, 1000) : GTLRectInt(0, center - 100, 1000, center + 100));
      }
      database.get_topo_pool().add_net(std::move(nodes), {edge});
      EdgeEnvInterval interval;
      interval.set_start_coord(0);
      interval.set_end_coord(1000);
      database.get_net_env_list()[net_idx].append_edge_interval_list({interval});
    }
    if (adjacent) {
      auto& lower = database.get_net_env_list()[0].get_edge_interval_list(0)[0];
      auto& upper = database.get_net_env_list()[1].get_edge_interval_list(0)[0];
      lower.set_upper_adjacent_edge(&database.get_topo_pool().get_net_edge_list(1)[0]);
      lower.set_upper_spacing(300);
      upper.set_lower_adjacent_edge(&database.get_topo_pool().get_net_edge_list(0)[0]);
      upper.set_lower_spacing(300);
    }
    if (!defer_geometry) RCXVP.process();
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
        expectNear(new_intervals[i].get_capacitance_segments().front().geometry.width, old_intervals[i].get_capacitance_segments().front().geometry.width);
      }
    }
  }

  void expectInvalidGeometry()
  {
    try {
      if (defer_geometry) RCXVP.process();
      RCXRE.extract();
    } catch (const std::runtime_error&) {
      return;
    }
    throw std::runtime_error("invalid resistance geometry was accepted");
  }

  std::string mapping_text = "M1 metal1\nM2 metal2\nV1 via1\n";
  bool defer_geometry = false;

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
  fixture.loadProcess("CONDUCTOR metal1 { THICKNESS=.1 RPSQ=.12 } CONDUCTOR metal2 { THICKNESS=.1 RPSQ=.12 } "
                      "VIA via1 { FROM=metal1 TO=metal2 " + properties + " }", scale, temperature);
  fixture.addGeometry(true, cut_x, cut_y);
  fixture.extractAndExpect(expected);
}

using Test = std::pair<std::string, std::function<void()>>;

std::vector<Test> processInputTests()
{
  return {
      {"sectioned maps retain conducting pairs and ignore nonconductive sections",
       [] {
         ResistanceFixture fixture;
         fixture.mapping_text = "conducting_layers\nM1 metal1 # routing\nM2 metal2\nvia_layers\nV1 via1\n"
                                "marker_layers\nMARK ignored\nremove_layers\nPO\nCT\n";
         fixture.loadProcess("CONDUCTOR metal1 { THICKNESS=.1 RPSQ=.12 }");
         fixture.addGeometry();
         fixture.extractAndExpect(.6);
         const auto& mapping = RCXDM.getDatabase().get_layer_table().get_design_name_to_process_name_map();
         if (mapping.size() != 3 || mapping.contains("MARK"))
           throw std::runtime_error("nonconductive mapping was imported");
       }},
      {"cache distinguishes files and accepts the obsolete extension spelling",
       [] {
         ResistanceFixture fixture;
         fixture.writeProcess("a", "metal metal1 {Thickness .1 Resistance .12}");
         fixture.writeProcess("b", "metal metal1 {Thickness .1 Resistance .24}");
         fixture.loadConfiguration(R"([{"name":"A","process_file":"a","captab_file":"zero.captab"},
           {"name":"B","process_file":"b","captab_file":"zero.captab"},
           {"name":"C","process_file":"a","captab_file":"zero.captab","process_options":{"extensions":["dielectric_surface_fields"]}}])");
         const auto& corners = RCXDM.getDatabase().get_corner_data_list();
         if (corners[0].get_process_technology() == corners[1].get_process_technology()
             || corners[0].get_process_technology() != corners[2].get_process_technology())
           throw std::runtime_error("cache ignored source/options");
         expectNear(corners[1].get_process_conductor_list()[0].get_sheet_resistance(), .24);
       }},
      {"equivalent aliases and extensionless paths work, missing inputs fail",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("metal metal1 {Thickness .1 Resistance .12}", "process");
         fixture.loadConfiguration(R"([{"name":"A","process_file":"process","itf_file":"./process","captab_file":"zero.captab"}])");
         for (const auto& corner : {R"([{"name":"A","captab_file":"zero.captab"}])",
                                   R"([{"name":"A","process_file":"missing","captab_file":"zero.captab"}])"}) {
           bool failed = false;
           try {
             fixture.loadConfiguration(corner);
           } catch (const std::exception&) {
             failed = true;
           }
           if (!failed)
             throw std::runtime_error("missing source accepted");
         }
       }},
      {"used layers without a matching model cannot produce zero resistance",
       [] {
         for (bool via : {false, true}) {
           ResistanceFixture fixture;
           fixture.loadInput("metal unused {Thickness .1 Resistance .12}");
           fixture.addGeometry(via);
           fixture.expectInvalidGeometry();
         }
       }},
      {"via connections are checked at design binding",
       [] {
         for (const auto& upper : {"missing", "metal1"}) {
           ResistanceFixture fixture;
           bool failed = false;
           try {
             fixture.loadInput(std::string("metal metal1 {Thickness .1 Resistance .12} via via1 {Area .01 Resistance 4 LOWERLAYER metal1 UPPERLAYER ") + upper
                               + "}");
           } catch (const std::exception&) {
             failed = true;
           }
           if (!failed)
             throw std::runtime_error("invalid via connection accepted");
         }
       }},
      {"unmapped contact definitions do not prevent routing layer extraction",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput(
             "CONDUCTOR metal1 {THICKNESS=.1 RPSQ=.12} "
             "CONDUCTOR diffusion {LAYER_TYPE=DIFFUSION THICKNESS=.1 RPSQ=10} "
             "VIA contact {FROM=SUBSTRATE TO=diffusion RPV=.000001}");
         const auto& process = RCXDM.getDatabase().get_corner_data_list()[0];
         if (process.get_process_via_list().size() != 1)
           throw std::runtime_error("unmapped contact definition was discarded");
         fixture.addGeometry();
         fixture.extractAndExpect(.6);
       }},
      {"via connections may name process conductors",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("metal metal1 {Thickness .1 Resistance .12} metal metal2 {Thickness .1 Resistance .12} "
                           "via via1 {Area .01 Resistance 4 LOWERLAYER metal1 UPPERLAYER metal2}");
         fixture.addGeometry(true, 100, 100);
         fixture.extractAndExpect(4);
       }},
      {"via references can resolve through mapped LEF routing layers",
       [] {
         ResistanceFixture fixture;
         fixture.addRoutingLayer("M1");
         fixture.addRoutingLayer("M2");
         fixture.loadInput("via via1 {Area .01 Resistance 4 LOWERLAYER metal1 UPPERLAYER metal2}");
         fixture.addGeometry(true, 100, 100);
         fixture.extractAndExpect(4);
       }},
      {"multiple temperatures share immutable technology",
       [] {
         ResistanceFixture fixture;
         fixture.writeProcess("process.data", "metal metal1 {Thickness .1 Resistance .12}");
         fixture.loadConfiguration(R"([{"name":"A","process_file":"process.data","captab_file":"zero.captab","temperature":[25,125]},
                                       {"name":"B","process_file":"process.data","captab_file":"zero.captab"}])");
         const auto& corners = RCXDM.getDatabase().get_corner_data_list();
         if (corners.size() != 3 || corners[0].get_process_technology() != corners[1].get_process_technology()
             || corners[0].get_process_technology() != corners[2].get_process_technology())
           throw std::runtime_error("source not shared");
         expectNear(corners[1].get_tmpr(), 125);
       }},
      {"failed load preserves state and corrected input retries",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("metal metal1 {Thickness .1 Resistance .12}");
         const auto original = RCXDM.getDatabase().get_corner_data_list()[0].get_process_technology();
         const auto original_layers = RCXDM.getDatabase().get_layer_table().get_design_idx_to_name_map();
         bool failed = false;
         try {
           fixture.loadInput("metal metal1 {Thickness .1 Resistance broken}");
         } catch (const std::exception&) {
           failed = true;
         }
         if (!failed || RCXDM.getDatabase().get_corner_data_list()[0].get_process_technology() != original
             || RCXDM.getDatabase().get_layer_table().get_design_idx_to_name_map() != original_layers)
           throw std::runtime_error("failed load changed published state");
         fixture.loadInput("metal metal1 {Thickness .1 Resistance .24}");
         expectNear(RCXDM.getDatabase().get_corner_data_list()[0].get_process_conductor_list()[0].get_sheet_resistance(), .24);
         if (RCXDM.getDatabase().get_layout_data().get_regular_net_num() != 2)
           throw std::runtime_error("retry duplicated design data");
       }},
      {"malformed configuration uses the common logger and preserves state",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("metal metal1 {Thickness .1 Resistance .12}");
         const auto model = RCXDM.getDatabase().get_corner_data_list()[0].get_process_technology();
         for (const auto& corners :
              {std::string("["), std::string("null"), std::string("[7]"),
               std::string(R"([{"name":7,"process_file":"process.data","captab_file":"zero.captab"}])"),
               std::string(R"([{"name":"A","process_file":7,"captab_file":"zero.captab"}])"),
               std::string(R"([{"name":"A","process_file":"process.data","captab_file":"zero.captab","temperature":["hot"]}])"),
               std::string(R"([{"name":"A","process_file":"process.data","captab_file":"zero.captab","process_options":{"extensions":[7]}}])")}) {
           bool logged_error = false;
           try {
             fixture.loadConfiguration(corners);
           } catch (const std::runtime_error& error) {
             logged_error = std::string(error.what()).size() > 0;
           }
           if (!logged_error)
             throw std::runtime_error("configuration failure bypassed common logger");
           if (RCXDM.getDatabase().get_corner_data_list()[0].get_process_technology() != model)
             throw std::runtime_error("failed configuration replaced previous model");
         }
       }},
      {"process options and temperature require documented containers",
       [] {
         ResistanceFixture fixture;
         fixture.writeProcess("process.data", "metal metal1 {Thickness .1 Resistance .12}");
         for (const auto& tail : {R"("temperature":25)", R"("process_options":{"extensions":"dielectric_surface_fields"})"}) {
           bool failed = false;
           try {
             fixture.loadConfiguration(std::string(R"([{"name":"A","process_file":"process.data","captab_file":"zero.captab",)") + tail + "}]");
           } catch (const std::runtime_error&) {
             failed = true;
           }
           if (!failed)
             throw std::runtime_error("invalid configuration container accepted");
         }
       }},
      {"conflicting input aliases fail without replacing model",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("metal metal1 {Thickness .1 Resistance .12}");
         const auto original = RCXDM.getDatabase().get_corner_data_list()[0].get_process_technology();
         bool failed = false;
         try {
           fixture.loadConfiguration(R"([{"name":"A","process_file":"process.data","itf_file":"other.data","captab_file":"zero.captab"}])");
         } catch (const std::exception&) {
           failed = true;
         }
         if (!failed || RCXDM.getDatabase().get_corner_data_list()[0].get_process_technology() != original)
           throw std::runtime_error("conflicting aliases accepted");
       }},

      {"ITF through neutral path",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("CONDUCTOR metal1 { THICKNESS=0.1 RPSQ=0.12 RESISTIVE_ONLY_ETCH=0.02 }");
         fixture.addGeometry();
         fixture.extractAndExpect(0.75);
       }},
      {"Tech content with ITF extension",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("# CONDUCTOR and = in this comment are not syntax\nmetal metal1 { Thickness .1 Resistance .12 }", "wrong.itf");
         fixture.addGeometry();
         fixture.extractAndExpect(0.6);
       }},
      {"Tech through legacy path alias",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("metal metal1 { Thickness .1 Resistance .12 Tnom 25 Coeff_RT1 .003 }", "process", true, 125);
         fixture.addGeometry();
         fixture.extractAndExpect(0.78);
       }},
      {"Tech single-axis resistance etch",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput(R"(metal metal1 { Thickness .1 Resistance .12
           ETCH_VS_WIDTH_AND_SPACING RESISTIVE_ONLY { WIDTHS {.2} VALUES {{.02}} } })");
         fixture.addGeometry();
         fixture.extractAndExpect(0.75);
       }},
      {"Tech resistance width-spacing table selected",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput(R"(metal metal1 { Thickness .1 Resistance 99
           RPSQ_VS_WIDTH_AND_SPACING { WIDTHS {.1 .3} VALUES {{.08} {.16}} } })");
         fixture.addGeometry();
         fixture.extractAndExpect(0.6);
       }},
      {"Tech reference cut",
       [] {
         ResistanceFixture fixture;
         fixture.loadInput("via via1 { Area .01 Resistance 4 }");
         fixture.addGeometry(true, 100, 100);
         fixture.extractAndExpect(4.0);
       }},
  };
}

std::vector<Test> wireTests()
{
  return {
      {"later etch stages use both neighbors updated walls", [] {
         ResistanceFixture fixture;
         fixture.loadProcess(R"(CONDUCTOR metal1 { THICKNESS=.1 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING { WIDTHS {.2} VALUES {.01} }
           ETCH_VS_WIDTH_AND_SPACING { SPACINGS {.1 .12} VALUES {0 .01} }
         })");
         fixture.addGeometry(false, 100, 200, false, false, true);
         fixture.extractAndExpect(.75);
       }},
      {"special wires contribute their updated facing walls", [] {
         ResistanceFixture fixture;
         fixture.defer_geometry = true;
         fixture.loadProcess(R"(CONDUCTOR metal1 { THICKNESS=.1 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING { WIDTHS {.2} VALUES {.01} }
           ETCH_VS_WIDTH_AND_SPACING { SPACINGS {.1 .12} VALUES {0 .01} }
         })");
         fixture.addGeometry(false, 100, 200, false, false, true);
         auto& database = RCXDM.getDatabase();
         auto& topology = database.get_topo_pool();
         TopoEdge special;
         special.set_layer_idx(0);
         special.set_shape(GTLRectInt(0, 500, 1000, 700));
         topology.add_special_edge_list({special});
         auto& interval = database.get_net_env_list()[1].get_edge_interval_list(0)[0];
         interval.set_upper_adjacent_edge(&topology.get_special_edge_pool()[0]);
         interval.set_upper_spacing(300);
         EdgeEnvInterval special_interval;
         special_interval.set_start_coord(0);
         special_interval.set_end_coord(1000);
         special_interval.set_lower_adjacent_edge(&topology.get_net_edge_list(1)[0]);
         special_interval.set_lower_spacing(300);
         database.get_special_edge_env_list() = {{special_interval}};
         RCXVP.process();
         fixture.extractAndExpect(.75);
         RCXVP.process();
         fixture.extractAndExpect(.75);
       }},
      {"resistance integrates neighbor-induced pieces without repeating etch", [] {
         ResistanceFixture fixture;
         fixture.defer_geometry = true;
         fixture.loadProcess(R"(CONDUCTOR metal1 {THICKNESS=.1 RPSQ=.12
           ETCH_VS_WIDTH_AND_SPACING {SPACINGS {.1 .3} VALUES {.01 .03}}
           ETCH_VS_WIDTH_AND_SPACING {WIDTHS {.16 .18} VALUES {.01 .02}}
           ETCH_VS_WIDTH_AND_SPACING {SPACINGS {.15 .16} VALUES {0 .01}}})");
         fixture.addGeometry();
         auto& database = RCXDM.getDatabase();
         auto& topology = database.get_topo_pool();
         auto* master = &topology.get_net_edge_list(0)[0];
         auto* neighbor = &topology.get_net_edge_list(1)[0];
         neighbor->set_shape(GTLRectInt(0, -400, 1000, -200));
         TopoEdge upper, outer;
         upper.set_layer_idx(0); outer.set_layer_idx(0);
         upper.set_shape(GTLRectInt(0, 200, 1000, 400));
         outer.set_shape(GTLRectInt(500, -700, 1000, -500));
         topology.add_special_edge_list({upper, outer});
         auto* upper_edge = &topology.get_special_edge_pool()[0];
         auto* outer_edge = &topology.get_special_edge_pool()[1];
         auto interval = [](int32_t start, int32_t end, TopoEdge* lower, TopoEdge* upper) {
           EdgeEnvInterval value;
           value.set_start_coord(start); value.set_end_coord(end);
           value.set_lower_adjacent_edge(lower); value.set_upper_adjacent_edge(upper);
           value.set_lower_spacing(300); value.set_upper_spacing(300);
           return value;
         };
         database.get_net_env_list().assign(2, NetEnv{});
         database.get_net_env_list()[0].append_edge_interval_list({interval(0, 1000, neighbor, upper_edge)});
         database.get_net_env_list()[1].append_edge_interval_list({interval(0, 500, nullptr, master), interval(500, 1000, outer_edge, master)});
         database.get_special_edge_env_list() = {{interval(0, 1000, master, nullptr)}, {interval(500, 1000, nullptr, neighbor)}};
         RCXVP.process();
         RCXRE.extract();
         expectNear(database.get_rc_data().get_corner_net_resistance_list({0, 0}).at(0), .8901098901098901);
         const auto intervals = database.get_corner_net_etch_profile_pool().get_item({0, 0}).get_edge_interval_list(0);
         if (intervals.size() != 1 || intervals[0].get_resistance_segments().size() != 2)
           throw std::runtime_error("neighbor boundaries changed environment indexes");
       }},
      {"capacitance etch preserves drawn and resistance geometry", [] {
         ResistanceFixture fixture;
         fixture.loadProcess("CONDUCTOR metal1 { THICKNESS=.1 RPSQ=.12 CAPACITIVE_ONLY_ETCH=.02 }");
         fixture.addGeometry();
         const auto& interval = RCXDM.getDatabase().get_corner_net_etch_profile_pool().get_item({0, 0}).get_edge_interval_list(0)[0];
         expectNear(interval.get_width(), .2);
         expectNear(interval.get_resistance_segments().front().geometry.width, .2);
         expectNear(interval.get_capacitance_segments().front().geometry.width, .16);
         fixture.extractAndExpect(.6);
       }},
      {"odd database widths preserve rectangle sidewall positions", [] {
         ResistanceFixture fixture;
         fixture.defer_geometry = true;
         fixture.loadProcess("CONDUCTOR metal1 { THICKNESS=.1 RPSQ=.12 }");
         fixture.addGeometry(false, 100, 200, false, false, true);
         auto& database = RCXDM.getDatabase();
         database.get_topo_pool().get_net_edge_list(0)[0].set_shape(GTLRectInt(0, -50, 1000, 50));
         database.get_topo_pool().get_net_edge_list(1)[0].set_shape(GTLRectInt(0, 50, 1000, 151));
         RCXVP.process();
         const auto& upper = database.get_corner_net_etch_profile_pool().get_item({0, 1}).get_edge_interval_list(0)[0];
         expectNear(upper.get_center(), .1005);
         expectNear(upper.get_lower_spacing(), 0);
         expectNear(upper.get_resistance_segments().front().lower_spacing, 0);
       }},
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
         fixture.defer_geometry = true;
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
         try { fixture.loadProcess("VIA via1 { AREA=-0.01 RPV=4 }"); }
         catch (const std::exception&) { return; }
         throw std::runtime_error("invalid reference area accepted by loader");
       }},
      {"nonfinite reference area", [] {
         ResistanceFixture fixture;
         fixture.loadProcess("VIA via1 { AREA=0.01 RPV=4 }");
         auto& corner = RCXDM.getDatabase().get_corner_data_list()[0];
         auto model = std::make_shared<ProcessTechnology>(*corner.get_process_technology());
         model->vias[0].set_area(std::numeric_limits<double>::infinity());
         corner.set_process_technology(std::move(model));
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
  auto& corner = RCXDM.getDatabase().get_corner_data_list()[0];
  auto model = std::make_shared<ProcessTechnology>(*corner.get_process_technology());
  model->geometry_scale = scale;
  corner.set_process_technology(std::move(model));
  fixture.defer_geometry = !std::isfinite(scale) || scale <= 0;
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
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  try {
    const std::string group = argc > 1 ? argv[1] : "0.9";
    std::vector<Test> tests;
    if (group == "logger_exit") {
      // A child exercises the normal CLI contract without terminating this test.
      ResistanceFixture fixture;
      int output[2];
      if (pipe(output) != 0)
        throw std::runtime_error("pipe failed");
      std::cout.flush();
      const auto child = fork();
      if (child < 0)
        throw std::runtime_error("fork failed");
      if (child == 0) {
        close(output[0]);
        if (dup2(output[1], STDOUT_FILENO) < 0)
          _exit(99);
        close(output[1]);
        unsetenv("ECC_LOGGER_THROW_ON_ERROR");
        fixture.loadConfiguration("[");
        _exit(0);
      }
      close(output[1]);
      std::string log;
      char buffer[1024];
      for (ssize_t size; (size = read(output[0], buffer, sizeof(buffer))) > 0;)
        log.append(buffer, size);
      close(output[0]);
      int status = 0;
      if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != EXIT_FAILURE
          || log.find("Invalid JSON in config file") == std::string::npos || log.find("Error") == std::string::npos)
        throw std::runtime_error("CLI failure did not log and exit through the common logger");
      return EXIT_SUCCESS;
    }
    if (group == "process_input") {
      tests = processInputTests();
    } else if (group == "wire_etch") {
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
