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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <algorithm>

#include "DataManager.hpp"
#include "Logger.hpp"
#include "PDNGenerator.hpp"
#include "Utility.hpp"

namespace ifp {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    FPLOG.warn(Loc::current(), message);
  }
  return condition;
}

void addLayer(std::string name, int32_t order, Direction direction)
{
  RoutingLayer layer;
  layer.set_name(name);
  layer.set_order(order);
  layer.set_prefer_direction(direction);
  FPDM.getDatabase().get_routing_layer_name_to_idx_map()[name] = static_cast<int32_t>(FPDM.getDatabase().get_routing_layer_list().size());
  FPDM.getDatabase().get_routing_layer_list().push_back(layer);
}

void addConnect(std::string first_layer, std::string second_layer)
{
  PGLayerPair pair;
  pair.set_first_layer_name(first_layer);
  pair.set_second_layer_name(second_layer);
  FPDM.getConfig().pg_layer_pair_list.push_back(pair);
}

void initFixture(const std::string& directory)
{
  FPDM.getConfig() = Config();
  FPDM.getDatabase() = Database();
  FPDM.getConfig().pg_temp_directory_path = directory + "/";
  FPDM.getDatabase().set_micron_dbu(2000);
  FPDM.getDatabase().get_core().set_rect(1001, 2003, 21001, 22003);

  PGGlobalConnect power;
  power.set_net_name("VDD_A");
  power.set_pin_name("PWR");
  power.set_net_type(PGNetType::kPower);
  FPDM.getConfig().pg_connect_list.push_back(power);
  PGGlobalConnect ground;
  ground.set_net_name("VSS_B");
  ground.set_pin_name("GND");
  ground.set_net_type(PGNetType::kGround);
  FPDM.getConfig().pg_connect_list.push_back(ground);

  // Technology order, layer names and insertion order deliberately disagree.
  addLayer("M2", 20, Direction::kHorizontal);
  addLayer("RDL", 30, Direction::kHorizontal);
  addLayer("M99", 2, Direction::kHorizontal);
  addLayer("M10", 10, Direction::kVertical);
}

void addLine(std::string net, std::string layer, PGSegmentType type, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width = 200)
{
  PGSegment segment;
  segment.set_net_name(net);
  segment.set_layer_name(layer);
  segment.set_type(type);
  segment.set_start_coord(x1, y1);
  segment.set_end_coord(x2, y2);
  segment.set_width(width);
  FPDM.getDatabase().get_pg_segment_list().push_back(segment);
}

bool hasVia(std::string net, std::string bottom_layer, std::string top_layer, int32_t x, int32_t y)
{
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kVia && segment.get_net_name() == net && segment.get_bottom_layer_name() == bottom_layer
        && segment.get_top_layer_name() == top_layer && segment.get_start_x() == x && segment.get_start_y() == y) {
      return true;
    }
  }
  return false;
}

bool hasVerticalSegmentSpanning(const std::string& layer, int32_t x, int32_t y1, int32_t y2)
{
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (!segment.is_vertical() || segment.get_layer_name() != layer || segment.get_start_x() != x) {
      continue;
    }
    if (std::max(segment.get_ll_y(), y1) < std::min(segment.get_ur_y(), y2)) {
      return true;
    }
  }
  return false;
}

bool hasHorizontalSegmentSpanning(const std::string& layer, int32_t y, int32_t x1, int32_t x2)
{
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (!segment.is_horizontal() || segment.get_layer_name() != layer || segment.get_start_y() != y) {
      continue;
    }
    if (std::max(segment.get_ll_x(), x1) < std::min(segment.get_ur_x(), x2)) {
      return true;
    }
  }
  return false;
}

bool hasVerticalSegmentSpanning(const std::string& net, const std::string& layer, int32_t x, int32_t y1, int32_t y2)
{
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_net_name() != net || !segment.is_vertical() || segment.get_layer_name() != layer || segment.get_start_x() != x) {
      continue;
    }
    if (std::max(segment.get_ll_y(), y1) < std::min(segment.get_ur_y(), y2)) {
      return true;
    }
  }
  return false;
}

bool checkPowerFile(const std::string& directory, const std::map<std::string, std::vector<PlanarCoord>>& expected)
{
  std::ifstream file(directory + "/power.ploc");
  std::string line;
  std::getline(file, line);
  if (!check(line == "# name x_um y_um layer net", "Missing or incorrect PLOC header")) {
    return false;
  }
  std::set<std::string> source_names;
  for (const auto& [expected_net, coord_list] : expected) {
    for (int32_t idx = 0; idx < static_cast<int32_t>(coord_list.size()); idx++) {
      if (!check(static_cast<bool>(std::getline(file, line)), "Missing supply point")) {
        return false;
      }
      std::istringstream stream(line);
      std::string name;
      double x = 0.0;
      double y = 0.0;
      std::string layer;
      std::string net;
      std::string extra;
      if (!check(static_cast<bool>(stream >> name >> x >> y >> layer >> net) && !(stream >> extra), "PLOC must have five columns")) {
        return false;
      }
      if (!check(name == expected_net + "_" + std::to_string(idx) && source_names.insert(name).second && net == expected_net && layer == "M2",
                 "Incorrect supply name, net or highest stripe layer")) {
        return false;
      }
      int32_t micron_dbu = FPDM.getDatabase().get_micron_dbu();
      if (!check(std::isfinite(x) && std::isfinite(y) && std::llround(x * micron_dbu) == coord_list[idx].get_x()
                     && std::llround(y * micron_dbu) == coord_list[idx].get_y(),
                 "Absolute micron coordinates must round-trip to the via position")) {
        return false;
      }
      if (!check(hasVia(net, "M10", "M2", coord_list[idx].get_x(), coord_list[idx].get_y()), "Supply point has no matching via")) {
        return false;
      }
    }
  }
  return check(!std::getline(file, line), "Unexpected or duplicate supply point");
}

bool testGeneratedStripes(const std::string& directory)
{
  initFixture(directory);
  for (std::string layer : {"M2", "M10"}) {
    PGStripe stripe;
    stripe.set_layer_name(layer);
    stripe.set_width_micron(0.2);
    stripe.set_pitch_micron(4.0);
    stripe.set_offset_micron(0.0);
    FPDM.getConfig().pg_stripe_list.push_back(stripe);
  }
  addConnect("M2", "M10");
  addConnect("M10", "M2");
  FPPG.generate();
  std::map<std::string, std::vector<PlanarCoord>> expected;
  for (int32_t x : {1201, 9201, 17201}) {
    for (int32_t y : {2203, 10203, 18203}) {
      expected["VDD_A"].emplace_back(x, y);
    }
  }
  for (int32_t x : {5201, 13201}) {
    for (int32_t y : {6203, 14203}) {
      expected["VSS_B"].emplace_back(x, y);
    }
  }
  return checkPowerFile(directory, expected);
}

bool testConnectionTypes(const std::string& directory)
{
  initFixture(directory);
  addLine("VDD_A", "M2", PGSegmentType::kStripe, 100001, 600007, 900001, 600007);
  addLine("VDD_A", "M10", PGSegmentType::kStripe, 400003, 200001, 400003, 1000001);
  addLine("VDD_A", "M10", PGSegmentType::kStripe, 400003, 200001, 400003, 1000001, 300);
  addLine("VDD_A", "M99", PGSegmentType::kStripe, 100001, 300007, 900001, 300007);
  addLine("VDD_A", "M2", PGSegmentType::kFollowPin, 100001, 700013, 900001, 700013);
  addLine("VSS_B", "M2", PGSegmentType::kStripe, 100001, 800011, 900001, 800011);
  addLine("VSS_B", "M10", PGSegmentType::kStripe, 700009, 200001, 700009, 1000001);
  addConnect("M10", "M99");
  addConnect("M2", "M10");
  addConnect("M10", "M2");

  Instance macro;
  macro.set_macro(true);
  macro.set_placed(true);
  macro.get_routing_halo_rect().set_rect(499917, 599907, 500117, 600107);
  InstancePinShape pin;
  pin.set_pin_name("PWR");
  pin.set_layer_name("M10");
  pin.set_rect(499967, 599957, 500067, 600057);
  macro.get_pin_shape_list().push_back(pin);
  FPDM.getDatabase().get_instance_list().push_back(macro);
  FPPG.generate();
  return checkPowerFile(directory, {{"VDD_A", {PlanarCoord(400003, 600007)}}, {"VSS_B", {PlanarCoord(700009, 800011)}}})
         && check(hasVia("VDD_A", "M99", "M10", 400003, 300007), "Fixture must contain a lower-layer via")
         && check(hasVia("VDD_A", "M10", "M2", 400003, 700013), "Fixture must contain a rail-to-stripe via")
         && check(hasVia("VDD_A", "M10", "M2", 500017, 600007), "Fixture must contain a macro-pin via");
}

bool testPGSegmentRect()
{
  PGSegment vertical;
  vertical.set_type(PGSegmentType::kStripe);
  vertical.set_start_coord(1000, 2000);
  vertical.set_end_coord(1000, 5000);
  vertical.set_width(200);
  bool passed = check(vertical.get_ll_x() == 900 && vertical.get_ur_x() == 1100 && vertical.get_ll_y() == 2000 && vertical.get_ur_y() == 5000,
                      "Vertical PGSegment rectangle is incorrect");

  PGSegment horizontal;
  horizontal.set_type(PGSegmentType::kStripe);
  horizontal.set_start_coord(2000, 1000);
  horizontal.set_end_coord(5000, 1000);
  horizontal.set_width(200);
  passed = check(horizontal.get_ll_x() == 2000 && horizontal.get_ur_x() == 5000 && horizontal.get_ll_y() == 900 && horizontal.get_ur_y() == 1100,
                 "Horizontal PGSegment rectangle is incorrect")
           && passed;
  return passed;
}

bool testBoundaryRailConnections(const std::string& directory)
{
  initFixture(directory);
  addLine("VDD_A", "M99", PGSegmentType::kFollowPin, 1001, 2003, 21001, 2003, 160);
  addLine("VSS_B", "M99", PGSegmentType::kFollowPin, 1001, 22003, 21001, 22003, 160);
  addLine("VDD_A", "M10", PGSegmentType::kStripe, 5001, 2003, 5001, 22003, 1000);
  addLine("VSS_B", "M10", PGSegmentType::kStripe, 15001, 22003, 15001, 2003, 1000);
  addConnect("M99", "M10");
  FPPG.generate();

  bool passed = check(hasVia("VDD_A", "M99", "M10", 5001, 2003), "Bottom rail via must remain centered on the rail");
  passed = check(hasVia("VSS_B", "M99", "M10", 15001, 22003), "Top rail via must remain centered on the rail") && passed;
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kVia) {
      passed = check(segment.get_via_width() == 1000 && segment.get_via_height() == 160, "Boundary rail via must use the full rail width") && passed;
    } else if (segment.get_type() == PGSegmentType::kStripe) {
      passed = check(segment.get_width() == 1000, "Stripe width must not change") && passed;
      if (segment.get_net_name() == "VDD_A") {
        passed = check(segment.get_start_y() == 1923 && segment.get_end_y() == 22003, "Bottom stripe must cover the lower rail edge") && passed;
      } else {
        passed = check(segment.get_start_y() == 22083 && segment.get_end_y() == 2003, "Reversed stripe must cover the upper rail edge") && passed;
      }
    }
  }

  initFixture(directory);
  addLine("VDD_A", "M10", PGSegmentType::kFollowPin, 1001, 2003, 1001, 22003, 160);
  addLine("VSS_B", "M10", PGSegmentType::kFollowPin, 21001, 2003, 21001, 22003, 160);
  addLine("VDD_A", "M2", PGSegmentType::kStripe, 1001, 5003, 21001, 5003, 1000);
  addLine("VSS_B", "M2", PGSegmentType::kStripe, 21001, 15003, 1001, 15003, 1000);
  addConnect("M10", "M2");
  FPPG.generate();
  passed = check(hasVia("VDD_A", "M10", "M2", 1001, 5003), "Left rail via must remain centered on the rail") && passed;
  passed = check(hasVia("VSS_B", "M10", "M2", 21001, 15003), "Right rail via must remain centered on the rail") && passed;
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kVia) {
      passed = check(segment.get_via_width() == 160 && segment.get_via_height() == 1000, "Horizontal stripe must cover the full vertical rail") && passed;
    }
  }
  return passed;
}

bool testUnconnectedRail(const std::string& directory)
{
  initFixture(directory);
  addLine("VSS_B", "M99", PGSegmentType::kFollowPin, 1001, 2003, 21001, 2003, 160);
  addLine("VDD_A", "M2", PGSegmentType::kFollowPin, 1001, 22003, 21001, 22003, 160);
  addLine("VDD_A", "M10", PGSegmentType::kStripe, 5001, 2003, 5001, 22003, 1000);
  addConnect("M99", "M10");
  FPPG.generate();
  bool passed = true;
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kStripe) {
      passed = check(segment.get_start_y() == 2003 && segment.get_end_y() == 22003, "Unconnected rail must not extend a stripe") && passed;
    }
    passed = check(segment.get_type() != PGSegmentType::kVia, "Wrong-net or unconfigured-layer rail must not generate a via") && passed;
  }
  return passed;
}

bool testMissingConnections(const std::string& directory)
{
  initFixture(directory);
  addLine("VDD_A", "M2", PGSegmentType::kStripe, 0, 2000, 8000, 2000);
  addLine("VDD_A", "M10", PGSegmentType::kStripe, 4000, 0, 4000, 8000);
  addLine("VDD_A", "M99", PGSegmentType::kStripe, 0, 6000, 8000, 6000);
  addConnect("M99", "M10");
  FPPG.generate();
  if (!checkPowerFile(directory, {})) {
    return false;
  }
  initFixture(directory);
  addLine("VDD_A", "M2", PGSegmentType::kStripe, 0, 2000, 8000, 2000);
  addLine("VDD_A", "M10", PGSegmentType::kFollowPin, 4000, 0, 4000, 8000);
  addConnect("M10", "M2");
  FPPG.generate();
  if (!checkPowerFile(directory, {})) {
    return false;
  }
  initFixture(directory);
  FPPG.generate();
  return checkPowerFile(directory, {});
}

void initPlacementHaloFixture(const std::string& directory)
{
  FPDM.getConfig() = Config();
  FPDM.getDatabase() = Database();
  FPDM.getConfig().pg_temp_directory_path = directory + "/";
  FPDM.getDatabase().set_micron_dbu(2000);
  FPDM.getDatabase().get_core().set_rect(0, 0, 400000, 400000);

  PGGlobalConnect power;
  power.set_net_name("VDD");
  power.set_pin_name("VDD");
  power.set_net_type(PGNetType::kPower);
  FPDM.getConfig().pg_connect_list.push_back(power);
  PGGlobalConnect ground;
  ground.set_net_name("VSS");
  ground.set_pin_name("VSS");
  ground.set_net_type(PGNetType::kGround);
  FPDM.getConfig().pg_connect_list.push_back(ground);

  addLayer("MET1", 1, Direction::kHorizontal);
  addLayer("MET4", 4, Direction::kVertical);
  addLayer("MET5", 5, Direction::kHorizontal);
}

void addPlacementHaloMacro(int32_t llx, int32_t lly, int32_t urx, int32_t ury, int32_t placement_halo, int32_t routing_halo)
{
  Instance macro;
  macro.set_macro(true);
  macro.set_placed(true);
  macro.set_bounding_rect(llx, lly, urx, ury);
  macro.get_placement_halo_rect().set_rect(llx - placement_halo, lly - placement_halo, urx + placement_halo, ury + placement_halo);
  macro.get_routing_halo_rect().set_rect(llx - routing_halo, lly - routing_halo, urx + routing_halo, ury + routing_halo);
  InstancePinShape pin;
  pin.set_pin_name("DUMMY");
  pin.set_layer_name("MET4");
  pin.set_rect(llx, lly, urx, ury);
  macro.get_pin_shape_list().push_back(pin);
  FPDM.getDatabase().get_instance_list().push_back(macro);
}

bool testStripePlacementHalo(const std::string& directory)
{
  PGStripe met4_stripe;
  met4_stripe.set_layer_name("MET4");
  met4_stripe.set_width_micron(0.5);
  met4_stripe.set_pitch_micron(500.0);
  met4_stripe.set_offset_micron(74.75);

  initPlacementHaloFixture(directory);
  addPlacementHaloMacro(100000, 100000, 200000, 200000, 5000, 1000);
  addPlacementHaloMacro(100000, 210000, 200000, 310000, 5000, 1000);
  FPDM.getConfig().pg_stripe_list.push_back(met4_stripe);
  FPPG.generate();

  bool passed = true;
  passed = check(!hasVerticalSegmentSpanning("MET4", 150000, 100000, 310000), "MET4 stripe must not survive inside the placement halo corridor") && passed;
  passed = check(hasVerticalSegmentSpanning("MET4", 150000, 0, 100000), "MET4 stripe below macro A must remain") && passed;
  passed = check(hasVerticalSegmentSpanning("MET4", 150000, 310000, 400000), "MET4 stripe above macro B must remain") && passed;

  initPlacementHaloFixture(directory);
  addPlacementHaloMacro(100000, 100000, 200000, 200000, 5000, 1000);
  addPlacementHaloMacro(100000, 230000, 200000, 330000, 5000, 1000);
  FPDM.getConfig().pg_stripe_list.push_back(met4_stripe);
  FPPG.generate();
  passed = check(hasVerticalSegmentSpanning("MET4", 150000, 205000, 225000), "MET4 stripe must remain in the gap outside both placement halos") && passed;

  initPlacementHaloFixture(directory);
  addPlacementHaloMacro(100000, 100000, 200000, 200000, 5000, 1000);
  PGStripe met5_stripe;
  met5_stripe.set_layer_name("MET5");
  met5_stripe.set_width_micron(0.5);
  met5_stripe.set_pitch_micron(500.0);
  met5_stripe.set_offset_micron(74.75);
  FPDM.getConfig().pg_stripe_list.push_back(met5_stripe);
  FPPG.generate();
  passed = check(hasHorizontalSegmentSpanning("MET5", 150000, 0, 400000), "MET5 stripe must pass over the macro") && passed;
  return passed;
}

bool testActualViaOverlap(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  addLine("VDD", "MET4", PGSegmentType::kStripe, 150000, 819500, 150000, 900000, 1000);
  addLine("VDD", "MET5", PGSegmentType::kStripe, 0, 819000, 400000, 819000, 1000);
  addConnect("MET4", "MET5");
  FPPG.generate();
  bool passed = check(!hasVia("VDD", "MET4", "MET5", 150000, 819000), "Zero-overlap via must not be generated");

  initPlacementHaloFixture(directory);
  addLine("VDD", "MET4", PGSegmentType::kStripe, 150000, 818500, 150000, 900000, 1000);
  addLine("VDD", "MET5", PGSegmentType::kStripe, 0, 819000, 400000, 819000, 1000);
  addConnect("MET4", "MET5");
  FPPG.generate();
  passed = check(hasVia("VDD", "MET4", "MET5", 150000, 819000), "Positive-overlap via must be generated") && passed;
  return passed;
}

bool testRailExtensionHalo(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  addPlacementHaloMacro(100000, 100000, 200000, 200000, 0, 0);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 100000, 100000, 200000, 100000, 160);
  addLine("VDD", "MET4", PGSegmentType::kStripe, 150000, 0, 150000, 100000, 1000);
  addConnect("MET1", "MET4");
  FPPG.generate();
  return check(!hasVerticalSegmentSpanning("MET4", 150000, 100000, 200000), "Rail extension must not enter a macro placement halo");
}

bool testGapRailRepair(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  addPlacementHaloMacro(100000, 100000, 200000, 300000, 5000, 1000);
  addPlacementHaloMacro(230000, 100000, 330000, 300000, 5000, 1000);

  PGStripe met4_stripe;
  met4_stripe.set_layer_name("MET4");
  met4_stripe.set_width_micron(0.5);
  met4_stripe.set_pitch_micron(500.0);
  met4_stripe.set_offset_micron(74.75);
  FPDM.getConfig().pg_stripe_list.push_back(met4_stripe);

  PGStripe met5_stripe;
  met5_stripe.set_layer_name("MET5");
  met5_stripe.set_width_micron(0.5);
  met5_stripe.set_pitch_micron(500.0);
  met5_stripe.set_offset_micron(74.75);
  FPDM.getConfig().pg_stripe_list.push_back(met5_stripe);

  addConnect("MET1", "MET4");
  addConnect("MET4", "MET5");
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 205000, 120000, 225000, 120000, 160);

  FPPG.generate();

  bool passed = true;
  passed = check(hasVerticalSegmentSpanning("VDD", "MET4", 215000, 120000, 150000), "Isolated gap rail must receive a repair stripe") && passed;
  passed = check(hasVia("VDD", "MET1", "MET4", 215000, 120000), "Repair stripe must connect to the isolated rail") && passed;
  passed = check(hasVia("VDD", "MET4", "MET5", 215000, 150000), "Repair stripe must connect to the top stripe") && passed;
  return passed;
}

bool testUncoveredEdgeStripe(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  PGStripe met4_stripe;
  met4_stripe.set_layer_name("MET4");
  met4_stripe.set_width_micron(0.5);
  met4_stripe.set_pitch_micron(8.0);
  met4_stripe.set_offset_micron(0.5);
  FPDM.getConfig().pg_stripe_list.push_back(met4_stripe);
  addConnect("MET1", "MET4");
  addConnect("MET4", "MET5");

  addLine("VSS", "MET1", PGSegmentType::kFollowPin, 100, 10000, 800, 10000, 160);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 0, 11000, 400000, 11000, 160);
  addLine("VSS", "MET5", PGSegmentType::kStripe, 0, 11000, 400000, 11000, 1000);
  FPPG.generate();

  bool passed = check(hasVerticalSegmentSpanning("VSS", "MET4", 500, 0, 400000), "Uncovered VSS rail must receive a core-edge stripe");
  passed = check(hasVerticalSegmentSpanning("VSS", "MET4", 9500, 0, 400000), "Existing VSS mesh stripe must remain") && passed;
  passed = check(!hasVerticalSegmentSpanning("VDD", "MET4", 500, 0, 400000), "Covered VDD edge must not receive another stripe") && passed;
  passed = check(hasVia("VSS", "MET4", "MET5", 500, 11000), "VSS edge stripe must connect to the VSS mesh") && passed;
  return passed;
}

}  // namespace ifp

int main(int argc, char* argv[])
{
  if (argc != 2) {
    return EXIT_FAILURE;
  }
  std::string directory = argv[1];
  ifp::DataManager::initInst();
  ifp::PDNGenerator::initInst();
  FPUTIL.createDir(directory);
  bool passed = ifp::testPGSegmentRect();
  passed = ifp::testBoundaryRailConnections(directory) && passed;
  passed = ifp::testUnconnectedRail(directory) && passed;
  passed = ifp::testGeneratedStripes(directory) && passed;
  passed = ifp::testConnectionTypes(directory) && passed;
  passed = ifp::testMissingConnections(directory) && passed;
  passed = ifp::testStripePlacementHalo(directory) && passed;
  passed = ifp::testActualViaOverlap(directory) && passed;
  passed = ifp::testRailExtensionHalo(directory) && passed;
  passed = ifp::testGapRailRepair(directory) && passed;
  passed = ifp::testUncoveredEdgeStripe(directory) && passed;
  ifp::PDNGenerator::destroyInst();
  ifp::DataManager::destroyInst();
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
