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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DataManager.hpp"
#include "Logger.hpp"
#include "RoutingChecker.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <omp.h>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ilvs {

struct DataManagerTestAccess
{
  static void buildPhysicalGraph(DataManager& data_manager) { data_manager.buildPhysicalGraph(); }
};

}  // namespace ilvs

namespace {

ilvs::RoutingShape shape(int layer, int x1, int y1, int x2, int y2)
{
  ilvs::Shape rect;
  rect.set_layer_idx(layer);
  rect.set_ll_x(x1);
  rect.set_ll_y(y1);
  rect.set_ur_x(x2);
  rect.set_ur_y(y2);
  ilvs::RoutingShape result;
  result.set_shape(rect);
  result.set_layer_order(layer);
  return result;
}

ilvs::PhysicalGraph& graph()
{
  return LVSDM.getDatabase().get_def_data().get_physical_graph();
}

void pin(const std::string& net, const std::string& terminal, ilvs::RoutingShape rect)
{
  auto& routing = graph().getOrCreateNetRoutingGraph(net);
  int index = static_cast<int>(routing.get_routing_shape_list().size());
  routing.get_terminal_shape_idx_map()[terminal].push_back(index);
  routing.get_routing_shape_list().push_back(rect);
  routing.set_terminal_routing_shape_num(index + 1);
  auto& terminals = LVSDM.getDatabase().get_def_data().get_net_map()[net].get_terminal_name_list();
  if (std::find(terminals.begin(), terminals.end(), terminal) == terminals.end()) {
    terminals.push_back(terminal);
  }
  routing.set_driver_terminal_name(terminals.front());
}

void wire(const std::string& net, ilvs::RoutingShape rect)
{
  graph().getOrCreateNetRoutingGraph(net).get_routing_shape_list().push_back(rect);
}

void expect(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void check(int expected_shorts, int expected_opens = 0)
{
  ilvs::DataManagerTestAccess::buildPhysicalGraph(LVSDM);
  ilvs::RoutingChecker::initInst();
  LVSRC.check();
  ilvs::RoutingChecker::destroyInst();
  const auto& summary = LVSDM.getDatabase().get_summary().rc_summary;
  expect(summary.short_net_num == expected_shorts,
         "short components: expected " + std::to_string(expected_shorts) + ", got " + std::to_string(summary.short_net_num));
  expect(summary.open_net_num == expected_opens, "unexpected routing open count");
}

using ShortNetSets = std::multiset<std::vector<std::string>>;

ShortNetSets shortNetSets()
{
  ShortNetSets result;
  for (const auto& violation : LVSDM.getDatabase().get_summary().rc_summary.violation_list) {
    if (violation.get_violation_type() == ilvs::ViolationType::kRoutingShort) {
      auto names = violation.get_related_net_name_list();
      std::sort(names.begin(), names.end());
      result.insert(names);
    }
  }
  return result;
}

void expectShortNets(const ShortNetSets& expected)
{
  expect(shortNetSets() == expected, "wrong short violation net ownership or count");
}

}  // namespace

int main()
{
  ilvs::Logger::initInst();
  ilvs::DataManager::initInst();
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
    {"wire_to_foreign_terminal_only_net", [] {
      pin("victim", "U/A", shape(0, 0, 0, 10, 10));
      wire("aggressor", shape(0, 9, 5, 20, 6));
      check(1);
    }},
    {"wire_before_foreign_signal_pin_still_shorts", [] {
      wire("aggressor", shape(0, 9, 5, 20, 6));
      pin("victim", "U/A", shape(0, 0, 0, 10, 10));
      check(1);
      expectShortNets({{"aggressor", "victim"}});
    }},
    {"signal_pin_area_overlap_without_wire_is_a_short", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("second", "U2/A", shape(0, 5, 5, 15, 15));
      check(1);
      expectShortNets({{"first", "second"}});
      expect(graph().get_component_shape_list(graph().get_short_component_id_list().front()).size() == 2,
             "pin-only short lost its geometry");
    }},
    {"signal_pin_shared_edge_without_wire_is_a_short", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("second", "U2/A", shape(0, 10, 0, 20, 10));
      check(1);
      expectShortNets({{"first", "second"}});
    }},
    {"signal_pin_shared_corner_without_wire_is_a_short", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("second", "U2/A", shape(0, 10, 10, 20, 20));
      check(1);
      expectShortNets({{"first", "second"}});
    }},
    {"signal_power_pin_contact_without_wire_is_a_short", [] {
      graph().get_power_net_name_set().insert("VDD");
      pin("VDD", "U1/VDD", shape(0, 0, 0, 10, 10));
      pin("signal", "U2/A", shape(0, 10, 0, 20, 10));
      check(1);
      expectShortNets({{"VDD", "signal"}});
    }},
    {"signal_ground_pin_contact_without_wire_is_a_short", [] {
      graph().get_ground_net_name_set().insert("VSS");
      pin("signal", "U1/A", shape(0, 0, 0, 10, 10));
      pin("VSS", "U2/VSS", shape(0, 10, 0, 20, 10));
      check(1);
      expectShortNets({{"VSS", "signal"}});
    }},
    {"power_ground_pin_contact_without_wire_is_a_short", [] {
      graph().get_power_net_name_set().insert("VDD");
      graph().get_ground_net_name_set().insert("VSS");
      pin("VDD", "U1/VDD", shape(0, 0, 0, 10, 10));
      pin("VSS", "U2/VSS", shape(0, 10, 0, 20, 10));
      check(1);
      expectShortNets({{"VDD", "VSS"}});
    }},
    {"pin_one_dbu_gap_is_not_a_short", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("second", "U2/A", shape(0, 11, 0, 20, 10));
      check(0);
    }},
    {"pins_on_different_layers_are_not_a_short", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("second", "U2/A", shape(1, 0, 0, 10, 10));
      check(0);
    }},
    {"pin_bounding_box_overlap_without_rect_contact_is_not_a_short", [] {
      pin("first", "U1/A", shape(0, 0, 0, 2, 2));
      pin("first", "U1/A", shape(0, 8, 0, 10, 2));
      pin("second", "U2/A", shape(0, 4, 0, 6, 2));
      check(0);
    }},
    {"multiple_rect_contacts_in_one_component_report_one_short", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("first", "U1/A", shape(0, 20, 0, 30, 10));
      pin("second", "U2/A", shape(0, 5, 5, 15, 15));
      pin("second", "U2/A", shape(0, 25, 5, 35, 15));
      check(1);
      expectShortNets({{"first", "second"}});
      expect(graph().get_component_shape_list(graph().get_short_component_id_list().front()).size() == 4,
             "multi-RECT short lost or duplicated geometry");
    }},
    {"separate_pin_contacts_between_same_nets_report_two_components", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("first", "U2/A", shape(0, 20, 0, 30, 10));
      pin("second", "U3/A", shape(0, 5, 5, 15, 15));
      pin("second", "U4/A", shape(0, 25, 5, 35, 15));
      check(2, 2);
      expectShortNets({{"first", "second"}, {"first", "second"}});
    }},
    {"three_net_pin_contact_chain_reports_all_nets", [] {
      pin("first", "U1/A", shape(0, 0, 0, 10, 10));
      pin("second", "U2/A", shape(0, 10, 0, 20, 10));
      pin("third", "U3/A", shape(0, 20, 0, 30, 10));
      check(1);
      expectShortNets({{"first", "second", "third"}});
    }},
    {"foreign_pin_bypass_does_not_hide_same_net_open", [] {
      pin("victim", "U1/A", shape(0, 0, 0, 10, 10));
      pin("victim", "U2/A", shape(0, 20, 0, 30, 10));
      pin("bridge", "U3/A", shape(0, 5, 5, 25, 6));
      check(1, 1);
      expectShortNets({{"bridge", "victim"}});
      const auto& violations = LVSDM.getDatabase().get_summary().rc_summary.violation_list;
      auto open = std::find_if(violations.begin(), violations.end(), [](const auto& violation) {
        return violation.get_violation_type() == ilvs::ViolationType::kRoutingOpen;
      });
      expect(open != violations.end() && open->get_net_name() == "victim"
                 && open->get_terminal_name_list() == std::vector<std::string>{"U2/A"},
             "foreign pin bypass masked the disconnected terminal");
    }},
    {"pin_contacts_across_three_layers_join_through_one_terminal", [] {
      pin("victim", "U/A", shape(0, 0, 0, 10, 10));
      pin("victim", "U/A", shape(1, 100, 100, 110, 110));
      pin("victim", "U/A", shape(2, 200, 200, 210, 210));
      pin("first", "U1/A", shape(0, 10, 0, 20, 10));
      pin("second", "U2/A", shape(1, 110, 100, 120, 110));
      pin("third", "U3/A", shape(2, 210, 200, 220, 210));
      check(1);
      expectShortNets({{"first", "second", "third", "victim"}});
    }},
    {"mdu_three_reported_contacts", [] {
      pin("u_core/n319", "u_core/U819/A0", shape(0, 1212625, 1558655, 1212925, 1558745));
      wire("u_core/n1247", shape(0, 1212900, 1558744, 1212925, 1558745));
      pin("u_core/n1248", "u_core/U746/A0", shape(0, 1209425, 1558655, 1209725, 1558745));
      wire("u_core/n1441", shape(0, 1209700, 1558744, 1209725, 1558745));
      pin("u_core/n1504", "u_core/U713/Y", shape(0, 1309170, 1607405, 1309545, 1607495));
      wire("u_core/n3223", shape(0, 1309170, 1607470, 1309175, 1607495));
      check(3);
      std::set<std::vector<std::string>> pairs;
      for (auto id : graph().get_short_component_id_list()) {
        auto names = graph().get_component_net_name_list(id);
        std::sort(names.begin(), names.end());
        pairs.insert(names);
      }
      expect(pairs == std::set<std::vector<std::string>>{
        {"u_core/n1247", "u_core/n319"}, {"u_core/n1248", "u_core/n1441"},
        {"u_core/n1504", "u_core/n3223"}}, "wrong short net ownership");
    }},
    {"same_net_wire_pin_is_connected", [] {
      pin("n", "U1/A", shape(0, 0, 0, 10, 10));
      pin("n", "U2/A", shape(0, 20, 0, 30, 10));
      wire("n", shape(0, 9, 5, 21, 6));
      check(0);
    }},
    {"separated_signal_pins_still_report_open", [] {
      pin("n", "U1/A", shape(0, 0, 0, 10, 10));
      pin("n", "U2/A", shape(0, 20, 0, 30, 10));
      check(0, 1);
    }},
    {"one_dbu_gap_is_not_a_short", [] {
      pin("victim", "U/A", shape(0, 0, 0, 10, 10));
      wire("aggressor", shape(0, 11, 5, 20, 6));
      check(0);
    }},
    {"different_layers_are_not_a_short", [] {
      pin("victim", "U/A", shape(0, 0, 0, 10, 10));
      wire("aggressor", shape(1, 0, 0, 10, 10));
      check(0);
    }},
    {"via_enclosure_to_foreign_signal_pin", [] {
      pin("victim", "U/A", shape(0, 0, 0, 10, 10));
      auto& routing = graph().getOrCreateNetRoutingGraph("aggressor");
      routing.get_routing_shape_list() = {shape(0, 9, 5, 12, 8), shape(1, 9, 5, 12, 8)};
      routing.get_via_shape_idx_pair_list().emplace_back(0, 1);
      check(1);
    }},
    {"pad_overlap_without_wire_stays_connected", [] {
      pin("pad", "PIN/IN", shape(0, 0, 0, 10, 10));
      pin("pad", "PAD/PAD", shape(0, 5, 5, 15, 15));
      check(0);
      auto& components = graph().get_terminal_component_map();
      expect(components.at("PIN/IN") == components.at("PAD/PAD"), "same-net pad contact was lost");
    }},
    {"same_net_signal_pin_abutment_is_connected", [] {
      pin("n", "U1/A", shape(0, 0, 0, 10, 10));
      pin("n", "U2/A", shape(0, 10, 0, 20, 10));
      check(0);
      auto& components = graph().get_terminal_component_map();
      expect(components.at("U1/A") == components.at("U2/A"), "same-net signal pin abutment was lost");
    }},
    {"pg_pin_abutment_is_preserved", [] {
      graph().get_power_net_name_set().insert("VDD");
      pin("VDD", "U1/VDD", shape(0, 0, 0, 10, 10));
      pin("VDD", "U2/VDD", shape(0, 10, 0, 20, 10));
      check(0);
      auto& components = graph().get_terminal_component_map();
      expect(components.at("U1/VDD") == components.at("U2/VDD"), "PG abutment was lost");
    }},
    {"same_terminal_multilayer_shapes_join_after_layer_scan", [] {
      pin("victim", "U/A", shape(0, 0, 0, 10, 10));
      pin("victim", "U/A", shape(1, 100, 100, 110, 110));
      wire("first", shape(0, 9, 5, 20, 6));
      wire("second", shape(1, 109, 105, 120, 106));
      check(1);
      auto names = graph().get_component_net_name_list(graph().get_short_component_id_list().front());
      expect(std::set<std::string>(names.begin(), names.end()) == std::set<std::string>{"victim", "first", "second"},
             "same terminal did not join layers");
    }},
  };
  int failed = 0;
  std::map<std::string, ShortNetSets> single_thread_shorts;
  omp_set_dynamic(0);
  for (int thread_number : {1, 3}) {
    omp_set_num_threads(thread_number);
    for (const auto& [name, run] : tests) {
      LVSDM.getDatabase().reset();
      LVSDM.getConfig().thread_number = thread_number;
      graph().reserveNetNum(20);
      try {
        run();
        if (thread_number == 1) {
          single_thread_shorts[name] = shortNetSets();
        } else {
          expect(shortNetSets() == single_thread_shorts.at(name), "short components differ between one and three threads");
        }
        std::cout << "RESULT PASS " << name << " threads=" << thread_number << '\n';
      } catch (const std::exception& error) {
        ++failed;
        std::cout << "RESULT FAIL " << name << " threads=" << thread_number << ": " << error.what() << '\n';
      }
    }
  }
  ilvs::DataManager::destroyInst();
  ilvs::Logger::destroyInst();
  return failed == 0 ? 0 : 1;
}
