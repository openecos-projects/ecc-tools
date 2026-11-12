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
#include "FPInterface.hpp"
#include "IdbViaRule.h"
#include "Logger.hpp"
#include "PDNGenerator.hpp"
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

void addVia(std::string net, std::string bottom_layer, std::string top_layer, int32_t x, int32_t y, int32_t width, int32_t height)
{
  PGSegment via;
  via.set_net_name(net);
  via.set_type(PGSegmentType::kVia);
  via.set_bottom_layer_name(bottom_layer);
  via.set_top_layer_name(top_layer);
  via.set_start_coord(x, y);
  via.set_end_coord(x, y);
  via.set_via_width(width);
  via.set_via_height(height);
  via.set_generated(true);
  FPDM.getDatabase().get_pg_segment_list().push_back(via);
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
  bool rejected = false;
  try {
    FPPG.generate();
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("without a physical connection") != std::string::npos;
  }
  bool passed = check(rejected, "An unconnected rail must stop the PDN flow");
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kStripe) {
      passed = check(segment.get_start_y() == 2003 && segment.get_end_y() == 22003, "Unconnected rail must not extend a stripe") && passed;
    }
    passed = check(segment.get_type() != PGSegmentType::kVia, "Wrong-net or unconfigured-layer rail must not generate a via") && passed;
  }
  return passed;
}

bool testDisconnectedMesh(const std::string& directory)
{
  bool passed = true;
  for (bool lower_connection : {false, true}) {
    initFixture(directory);
    addLine("VDD_A", "M99", PGSegmentType::kFollowPin, 1001, 5003, 21001, 5003, 160);
    addLine("VDD_A", "M10", PGSegmentType::kStripe, 5001, 2003, 5001, 22003, 1000);
    if (lower_connection) {
      addConnect("M99", "M10");
      addLine("VDD_A", "M2", PGSegmentType::kStripe, 30001, 5003, 40001, 5003, 1000);
      addConnect("M10", "M2");
    }
    bool rejected = false;
    try {
      FPPG.generate();
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("without a physical connection") != std::string::npos;
    }
    passed = check(rejected, lower_connection ? "A rail/stripe island must connect to the supply mesh"
                                             : "Projected cross-layer overlap without a via must not count as connected") && passed;
  }
  return passed;
}

bool testPhyRailCoverage(const std::string& directory)
{
  bool passed = true;
  for (int32_t scenario = 0; scenario < 3; scenario++) {
    initFixture(directory);
    Database& database = FPDM.getDatabase();
    Row row;
    row.set_rect(1001, 2003, 21001, 3003);
    row.set_y(2003);
    row.set_orient(PlacementOrientation::kN);
    database.get_row_list().push_back(row);
    CellMaster master;
    master.set_name("PHY");
    master.set_width(200);
    master.set_height(1000);
    for (std::string pin_name : {"PWR", "GND"}) {
      bool power = pin_name == "PWR";
      master.get_pg_pin_types()[pin_name] = power ? PGNetType::kPower : PGNetType::kGround;
      InstancePinShape pin;
      pin.set_pin_name(pin_name);
      pin.set_layer_name("M99");
      int32_t y = power ? 1000 : 0;
      pin.set_rect(0, y - 80, 200, y + 80);
      master.get_pg_pin_shapes().push_back(pin);
    }
    database.get_cell_master_map()[master.get_name()] = master;
    Instance instance;
    instance.set_name("PHY_1");
    instance.set_master_name("PHY");
    instance.set_placed(true);
    instance.set_new_instance(true);
    instance.set_coord(scenario == 2 ? 0 : 2001, 2003);
    instance.set_orient(PlacementOrientation::kN);
    instance.set_bounding_rect(instance.get_x(), 2003, instance.get_x() + 200, 3003);
    instance.set_width(200);
    instance.set_height(1000);
    database.get_instance_list().push_back(instance);
    database.get_instance_name_to_idx_map()[instance.get_name()] = 0;
    addLine("VSS_B", "M99", PGSegmentType::kFollowPin, 1001, 2003, 21001, 2003, 160);
    if (scenario != 1) {
      addLine("VDD_A", "M99", PGSegmentType::kFollowPin, 1001, 3003, 21001, 3003, 160);
    }
    addLine("VDD_A", "M10", PGSegmentType::kStripe, 5001, 2003, 5001, 22003, 1000);
    addLine("VSS_B", "M10", PGSegmentType::kStripe, 15001, 2003, 15001, 22003, 1000);
    addConnect("M99", "M10");
    bool rejected = false;
    try {
      FPPG.generate();
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("without complete PDN rail coverage") != std::string::npos;
    }
    passed = check(rejected == (scenario != 0), "Physical cells must occupy a usable row with both power rails") && passed;
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

PlanarRect initNarrowGapRailFixture(const std::string& directory, int32_t channel_width, bool reverse_net_order)
{
  initPlacementHaloFixture(directory);
  if (reverse_net_order) {
    auto& connections = FPDM.getConfig().pg_connect_list;
    std::reverse(connections.begin(), connections.end());
  }
  for (RoutingLayer& layer : FPDM.getDatabase().get_routing_layer_list()) {
    if (layer.get_name() == "MET4") {
      layer.set_spacing(100);
    }
  }

  const PlanarRect channel(20000, 10000, 20000 + channel_width, 16000);
  addPlacementHaloMacro(10000, 5000, channel.get_ll_x() - 1000, 30000, 1000, 100);
  addPlacementHaloMacro(channel.get_ur_x() + 1000, 5000, channel.get_ur_x() + 10000, 30000, 1000, 100);
  for (std::string layer : {"MET4", "MET5"}) {
    PGStripe stripe;
    stripe.set_layer_name(layer);
    stripe.set_width_micron(0.5);
    // Suppress regular stripes and keep the channel outside core-edge repair bands.
    stripe.set_pitch_micron(10.0);
    stripe.set_offset_micron(500.0);
    FPDM.getConfig().pg_stripe_list.push_back(stripe);
  }
  addConnect("MET1", "MET4");
  addConnect("MET4", "MET5");

  for (int32_t rail_idx = 0; rail_idx < 4; ++rail_idx) {
    const int32_t y = channel.get_ll_y() + 2000 * rail_idx;
    addLine(rail_idx % 2 == 0 ? "VDD" : "VSS", "MET1", PGSegmentType::kFollowPin, channel.get_ll_x(), y, channel.get_ur_x(), y, 160);
  }
  // Remote trunks establish MET5 as the supply layer without covering any channel rail.
  for (std::string net : {"VDD", "VSS"}) {
    const int32_t x = net == "VDD" ? 4000 : 6000;
    const int32_t supply_y = channel.get_ur_y() + (net == "VDD" ? 4000 : 8000);
    addLine(net, "MET4", PGSegmentType::kStripe, x, 0, x, 400000, 1000);
    addLine(net, "MET5", PGSegmentType::kStripe, 0, supply_y, 400000, supply_y, 1000);
  }
  return channel;
}

bool checkNarrowGapRailRepair(const std::string& directory, int32_t channel_width, bool reverse_net_order)
{
  const PlanarRect channel = initNarrowGapRailFixture(directory, channel_width, reverse_net_order);
  const std::string scenario = std::to_string(channel_width) + " DBU channel, " + (reverse_net_order ? "VSS first: " : "VDD first: ");
  try {
    FPPG.generate();
  } catch (const std::runtime_error& error) {
    return check(false, scenario + "Joint rail repair must succeed: " + error.what());
  }

  bool passed = true;
  std::map<std::string, const PGSegment*> repairs;
  int32_t rail_count = 0;
  for (const PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kFollowPin) {
      ++rail_count;
    }
    if (segment.get_type() != PGSegmentType::kStripe || segment.get_layer_name() != "MET4" || segment.get_ur_x() <= channel.get_ll_x()
        || segment.get_ll_x() >= channel.get_ur_x()) {
      continue;
    }
    passed = check(segment.is_vertical() && segment.get_width() == 1000, scenario + "Repair stripes must retain their configured width") && passed;
    passed = check(segment.get_start_x() - segment.get_width() / 2 >= channel.get_ll_x()
                       && segment.get_start_x() + segment.get_width() / 2 <= channel.get_ur_x(),
                   scenario + "Repair stripes must stay between both placement halos") && passed;
    passed = check(repairs.emplace(segment.get_net_name(), &segment).second, scenario + "Each net needs only one grouped repair stripe") && passed;
  }
  passed = check(rail_count == 4, scenario + "Repair must preserve all four alternating rails") && passed;
  if (!check(repairs.size() == 2 && repairs.contains("VDD") && repairs.contains("VSS"), scenario + "Both nets must receive repair stripes")) {
    return false;
  }

  const PGSegment& power = *repairs.at("VDD");
  const PGSegment& ground = *repairs.at("VSS");
  const int32_t spacing = std::abs(power.get_start_x() - ground.get_start_x()) - (power.get_width() + ground.get_width()) / 2;
  passed = check(spacing >= 100, scenario + "Opposite-net repair stripes must keep MET4 spacing") && passed;
  if (channel_width == 2100) {
    passed = check(spacing == 100, scenario + "Exact-fit repair must use the available width without relaxing spacing") && passed;
  }

  for (int32_t rail_idx = 0; rail_idx < 4; ++rail_idx) {
    const std::string net = rail_idx % 2 == 0 ? "VDD" : "VSS";
    const int32_t y = channel.get_ll_y() + 2000 * rail_idx;
    const PGSegment& repair = *repairs.at(net);
    bool rail_present = false;
    for (const PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
      if (segment.get_type() == PGSegmentType::kFollowPin && segment.get_net_name() == net && segment.get_layer_name() == "MET1"
          && segment.is_horizontal() && segment.get_start_y() == y && segment.get_width() == 160) {
        rail_present = true;
      }
    }
    passed = check(rail_present, scenario + "Every original rail must survive with its net, layer and width intact") && passed;
    passed = check(std::min(repair.get_start_y(), repair.get_end_y()) <= y - 80
                       && std::max(repair.get_start_y(), repair.get_end_y()) >= y + 80,
                   scenario + "Each repair stripe must span both same-net rails") && passed;
    passed = check(hasVia(net, "MET1", "MET4", repair.get_start_x(), y), scenario + "Every rail must have a same-net repair via") && passed;
  }
  for (std::string net : {"VDD", "VSS"}) {
    const int32_t supply_y = channel.get_ur_y() + (net == "VDD" ? 4000 : 8000);
    passed = check(hasVia(net, "MET4", "MET5", repairs.at(net)->get_start_x(), supply_y),
                   scenario + "Each repair stripe must connect to its MET5 supply") && passed;
  }
  return passed;
}

bool testNarrowGapRailRepair(const std::string& directory)
{
  bool passed = true;
  for (bool reverse_net_order : {false, true}) {
    passed = checkNarrowGapRailRepair(directory, 2200, reverse_net_order) && passed;
  }
  return passed;
}

bool testExactFitGapRailRepair(const std::string& directory)
{
  bool passed = true;
  for (bool reverse_net_order : {false, true}) {
    passed = checkNarrowGapRailRepair(directory, 2100, reverse_net_order) && passed;
  }
  return passed;
}

bool testTooNarrowGapRailRepair(const std::string& directory)
{
  bool passed = true;
  for (bool reverse_net_order : {false, true}) {
    initNarrowGapRailFixture(directory, 2000, reverse_net_order);
    bool rejected = false;
    try {
      FPPG.generate();
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("without a physical connection") != std::string::npos;
    }
    passed = check(rejected, "A 2000 DBU channel cannot fit both 1000 DBU repair stripes and 100 DBU spacing; final validation must reject it") && passed;
  }
  return passed;
}

void initRepairReviewFixture(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  FPDM.getDatabase().set_manufacture_grid(10);
  for (RoutingLayer& layer : FPDM.getDatabase().get_routing_layer_list()) {
    if (layer.get_name() == "MET4") {
      layer.set_spacing(100);
      layer.set_prefer_track_pitch(400);
      layer.set_prefer_track_offset(0);
    }
  }
  for (std::string layer : {"MET4", "MET5"}) {
    PGStripe stripe;
    stripe.set_layer_name(layer);
    stripe.set_width_micron(0.5);
    stripe.set_pitch_micron(10.0);
    stripe.set_offset_micron(500.0);
    FPDM.getConfig().pg_stripe_list.push_back(stripe);
  }
  addConnect("MET1", "MET4");
  addConnect("MET4", "MET5");
}

bool testRepairClearanceBoundary(const std::string& directory)
{
  initRepairReviewFixture(directory);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 20000, 10000, 24000, 10000, 160);
  for (int32_t x : {20400, 22600}) {
    addLine("VSS", "MET4", PGSegmentType::kStripe, x, 0, x, 40000, 1000);
  }
  addLine("VDD", "MET5", PGSegmentType::kStripe, 0, 20000, 400000, 20000, 1000);
  addLine("VSS", "MET5", PGSegmentType::kStripe, 0, 24000, 400000, 24000, 1000);
  try {
    FPPG.generate();
  } catch (const std::runtime_error& error) {
    return check(false, std::string("The unique legal repair slot at x=21500 must be considered: ") + error.what());
  }

  bool passed = true;
  int32_t repair_count = 0;
  for (const PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() != PGSegmentType::kStripe || segment.get_net_name() != "VDD" || segment.get_layer_name() != "MET4") {
      continue;
    }
    ++repair_count;
    passed = check(segment.is_vertical() && segment.get_start_x() == 21500 && segment.get_width() == 1000,
                   "Repair must use the only spacing-legal position, even though it is not on the 400 DBU track grid") && passed;
  }
  passed = check(repair_count == 1, "A single rail needs exactly one full-width repair in the clearance slot") && passed;
  passed = check(hasVia("VDD", "MET1", "MET4", 21500, 10000), "The clearance-slot repair must connect to its rail") && passed;
  passed = check(hasVia("VDD", "MET4", "MET5", 21500, 20000), "The clearance-slot repair must connect to its supply") && passed;
  return passed;
}

bool testBlockedRepairGroupSplit(const std::string& directory)
{
  initRepairReviewFixture(directory);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 20000, 10000, 23000, 10000, 160);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 20600, 14000, 23600, 14000, 160);
  addLine("VSS", "MET4", PGSegmentType::kStripe, 21800, 0, 21800, 40000, 1000);
  addLine("VDD", "MET5", PGSegmentType::kStripe, 0, 20000, 400000, 20000, 1000);
  addLine("VSS", "MET5", PGSegmentType::kStripe, 0, 24000, 400000, 24000, 1000);
  try {
    FPPG.generate();
  } catch (const std::runtime_error& error) {
    return check(false, std::string("A blocked common corridor must fall back to separate rail repairs: ") + error.what());
  }

  bool passed = true;
  const PGSegment* left_repair = nullptr;
  const PGSegment* right_repair = nullptr;
  int32_t repair_count = 0;
  int32_t rail_count = 0;
  for (const PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kFollowPin) {
      ++rail_count;
      passed = check(segment.get_net_name() == "VDD" && segment.get_layer_name() == "MET1" && segment.get_width() == 160,
                     "Splitting a repair group must preserve both original rails") && passed;
    }
    if (segment.get_type() != PGSegmentType::kStripe || segment.get_net_name() != "VDD" || segment.get_layer_name() != "MET4") {
      continue;
    }
    ++repair_count;
    const int32_t x = segment.get_start_x();
    passed = check(segment.is_vertical() && segment.get_width() == 1000 && std::abs(x - 21800) >= 1100,
                   "Split repairs must retain their width and clearance from the opposite-net stripe") && passed;
    if (20500 <= x && x <= 20700) {
      left_repair = &segment;
    } else if (22900 <= x && x <= 23100) {
      right_repair = &segment;
    }
  }
  passed = check(rail_count == 2, "Both rails must survive a blocked-group split") && passed;
  if (!check(repair_count == 2 && left_repair != nullptr && right_repair != nullptr,
             "The disjoint legal windows must receive separate full-width repairs")) {
    return false;
  }
  for (const auto& [repair, y] : {std::make_pair(left_repair, 10000), std::make_pair(right_repair, 14000)}) {
    passed = check(std::min(repair->get_start_y(), repair->get_end_y()) <= y - 80
                       && std::max(repair->get_start_y(), repair->get_end_y()) >= y + 80,
                   "Each split repair must span its original rail") && passed;
    passed = check(hasVia("VDD", "MET1", "MET4", repair->get_start_x(), y), "Each original rail must have its own repair via") && passed;
    passed = check(hasVia("VDD", "MET4", "MET5", repair->get_start_x(), 20000), "Each split repair must reach the supply mesh") && passed;
  }
  return passed;
}

bool testSplitRepairCandidateInheritance(const std::string& directory)
{
  initRepairReviewFixture(directory);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 20000, 10000, 24000, 10000, 160);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 20700, 14000, 23700, 14000, 160);
  // The distant group forces both VDD groups in the shared conflict component to split.
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 19000, 90000, 21900, 90000, 160);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 20500, 94000, 22700, 94000, 160);
  addLine("VSS", "MET1", PGSegmentType::kFollowPin, 20600, 18000, 21600, 18000, 160);
  addLine("VSS", "MET1", PGSegmentType::kFollowPin, 22800, 22000, 23800, 22000, 160);
  const std::vector<PGSegment> original_rails = FPDM.getDatabase().get_pg_segment_list();
  addLine("VDD", "MET5", PGSegmentType::kStripe, 0, 150000, 400000, 150000, 1000);
  addLine("VSS", "MET5", PGSegmentType::kStripe, 0, 154000, 400000, 154000, 1000);
  try {
    FPPG.generate();
  } catch (const std::runtime_error& error) {
    return check(false, std::string("Splitting a failed component must retain the parent repair position at x=22200: ") + error.what());
  }

  bool passed = true;
  int32_t rail_count = 0;
  std::vector<const PGSegment*> repairs;
  for (const PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kFollowPin) {
      ++rail_count;
      passed = check(segment.get_layer_name() == "MET1" && segment.get_width() == 160,
                     "Candidate inheritance must preserve the original rail layer and width") && passed;
    }
    if (segment.get_type() != PGSegmentType::kStripe || segment.get_layer_name() != "MET4") {
      continue;
    }
    repairs.push_back(&segment);
    passed = check(segment.is_vertical() && segment.get_width() == 1000 && segment.get_start_x() % 10 == 0,
                   "Inherited repair candidates must retain full width and manufacturing-grid alignment") && passed;
    const int32_t supply_y = segment.get_net_name() == "VDD" ? 150000 : 154000;
    passed = check(hasVia(segment.get_net_name(), "MET4", "MET5", segment.get_start_x(), supply_y),
                   "Every split repair must connect to its own top supply") && passed;
  }
  passed = check(rail_count == 6, "Candidate inheritance must preserve all six rails") && passed;
  for (const PGSegment& rail : original_rails) {
    bool connected = false;
    for (const PGSegment* repair : repairs) {
      const int32_t x = repair->get_start_x();
      const int32_t y = rail.get_start_y();
      if (repair->get_net_name() == rail.get_net_name() && rail.get_start_x() <= x - repair->get_width() / 2
          && x + repair->get_width() / 2 <= rail.get_end_x()
          && std::min(repair->get_start_y(), repair->get_end_y()) <= y - rail.get_width() / 2
          && std::max(repair->get_start_y(), repair->get_end_y()) >= y + rail.get_width() / 2
          && hasVia(rail.get_net_name(), "MET1", "MET4", x, y)) {
        connected = true;
      }
    }
    passed = check(connected, "Every original rail must receive a full-width, physically connected repair after splitting") && passed;
  }
  for (int32_t y : {10000, 14000}) {
    passed = check(hasVia("VDD", "MET1", "MET4", 22200, y), "Both initial VDD rails must retain the inherited parent candidate at x=22200") && passed;
  }
  passed = check(hasVia("VSS", "MET1", "MET4", 21100, 18000) && hasVia("VSS", "MET1", "MET4", 23300, 22000),
                 "The narrow VSS rails must keep their forced repair positions") && passed;
  for (size_t first = 0; first < repairs.size(); ++first) {
    for (size_t second = first + 1; second < repairs.size(); ++second) {
      const PGSegment& a = *repairs[first];
      const PGSegment& b = *repairs[second];
      if (a.get_net_name() == b.get_net_name() || std::max(a.get_ll_y(), b.get_ll_y()) >= std::min(a.get_ur_y(), b.get_ur_y())) {
        continue;
      }
      passed = check(std::abs(a.get_start_x() - b.get_start_x()) - (a.get_width() + b.get_width()) / 2 >= 100,
                     "Inherited candidates must preserve spacing between all overlapping opposite-net repairs") && passed;
    }
  }
  return passed;
}

bool testRepeatedRepairTopStripes(const std::string& directory)
{
  bool passed = true;
  for (bool reverse_supply_order : {false, true}) {
    initRepairReviewFixture(directory);
    for (int32_t y : {10000, 30000}) {
      addLine("VDD", "MET1", PGSegmentType::kFollowPin, 20000, y, 24000, y, 160);
    }
    addLine("VSS", "MET4", PGSegmentType::kStripe, 6000, 0, 6000, 40000, 1000);
    addLine("VSS", "MET5", PGSegmentType::kStripe, 0, 24000, 400000, 24000, 1000);
    std::vector<int32_t> supply_y_list{20000, 12000, 28000, 20000, 8000, 32000};
    if (reverse_supply_order) {
      std::reverse(supply_y_list.begin(), supply_y_list.end());
    }
    for (int32_t y : supply_y_list) {
      addLine("VDD", "MET5", PGSegmentType::kStripe, 0, y, 400000, y, 1000);
    }
    try {
      FPPG.generate();
    } catch (const std::runtime_error& error) {
      passed = check(false, std::string("Repeated top supplies must allow one grouped repair: ") + error.what()) && passed;
      continue;
    }

    int32_t repair_count = 0;
    int32_t top_via_count = 0;
    for (const PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
      if (segment.get_net_name() != "VDD") {
        continue;
      }
      if (segment.get_type() == PGSegmentType::kStripe && segment.get_layer_name() == "MET4") {
        ++repair_count;
        passed = check(segment.is_vertical() && segment.get_start_x() == 22000 && segment.get_width() == 1000
                           && segment.get_start_y() == 9920 && segment.get_end_y() == 30080,
                       "In-span supplies must share a centered repair without a dominated extension beyond the rails") && passed;
      }
      if (segment.get_type() == PGSegmentType::kVia && segment.get_bottom_layer_name() == "MET4" && segment.get_top_layer_name() == "MET5") {
        ++top_via_count;
      }
    }
    passed = check(repair_count == 1, "Repeated or farther supplies must not emit duplicate same-X repairs") && passed;
    passed = check(top_via_count == 3, "Repeated top stripes must produce one via per distinct in-span crossing") && passed;
    for (int32_t y : {10000, 30000}) {
      passed = check(hasVia("VDD", "MET1", "MET4", 22000, y), "The shared repair must connect both grouped rails") && passed;
    }
    for (int32_t y : {12000, 20000, 28000}) {
      passed = check(hasVia("VDD", "MET4", "MET5", 22000, y), "The shared repair must connect every in-span supply") && passed;
    }
    for (int32_t y : {8000, 32000}) {
      passed = check(!hasVia("VDD", "MET4", "MET5", 22000, y), "A redundant farther supply must not lengthen the repair") && passed;
    }
  }
  return passed;
}

bool testUncoveredEdgeStripe(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  Row row;
  row.set_rect(100, 10000, 800, 11000);
  row.set_y(10000);
  row.set_orient(PlacementOrientation::kN);
  FPDM.getDatabase().get_row_list().push_back(row);
  PGRail rail;
  rail.set_layer_name("MET1");
  rail.set_width_micron(0.08);
  FPDM.getConfig().pg_rail_list.push_back(rail);
  PGStripe met4_stripe;
  met4_stripe.set_layer_name("MET4");
  met4_stripe.set_width_micron(0.5);
  met4_stripe.set_pitch_micron(8.0);
  met4_stripe.set_offset_micron(0.5);
  FPDM.getConfig().pg_stripe_list.push_back(met4_stripe);
  addConnect("MET1", "MET4");
  addConnect("MET4", "MET5");

  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 0, 11000, 400000, 11000, 160);
  addLine("VDD", "MET5", PGSegmentType::kStripe, 0, 20000, 400000, 20000, 1000);
  addLine("VSS", "MET5", PGSegmentType::kStripe, 0, 11000, 400000, 11000, 1000);
  FPPG.generate();

  bool passed = check(hasVerticalSegmentSpanning("VSS", "MET4", 500, 0, 400000), "Uncovered VSS rail must receive a core-edge stripe");
  passed = check(hasVerticalSegmentSpanning("VSS", "MET4", 9500, 0, 400000), "Existing VSS mesh stripe must remain") && passed;
  passed = check(!hasVerticalSegmentSpanning("VDD", "MET4", 500, 0, 400000), "Covered VDD edge must not receive another stripe") && passed;
  passed = check(hasVia("VSS", "MET1", "MET4", 450, 10000), "VSS edge stripe must connect at the center of its overlap with the narrow row rail") && passed;
  passed = check(hasVia("VSS", "MET4", "MET5", 500, 11000), "VSS edge stripe must connect to the VSS mesh") && passed;
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() == PGSegmentType::kFollowPin) {
      passed = check(segment.get_width() == 160, "Core-edge repair must preserve follow-pin width") && passed;
    }
  }
  return passed;
}

bool testDanglingFollowPinTrim(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  addLine("VDD", "MET1", PGSegmentType::kFollowPin, 0, 1000, 2000, 1000, 160);
  addLine("VDD", "MET1", PGSegmentType::kStripe, 500, 900, 500, 1100, 100);
  FPPG.generate();

  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() != PGSegmentType::kFollowPin || segment.get_net_name() != "VDD") {
      continue;
    }
    return check(segment.get_start_x() == 450 && segment.get_end_x() == 550, "Dangling follow-pin tail must be trimmed to its last same-layer contact");
  }
  return check(false, "Expected a VDD follow-pin rail");
}

bool testOverlappingViaStacks(const std::string& directory)
{
  initPlacementHaloFixture(directory);
  addVia("VDD", "MET1", "MET2", 10000, 10000, 1000, 160);
  addVia("VDD", "MET1", "MET2", 10000, 9960, 1000, 100);
  addVia("VDD", "MET4", "MET5", 20000, 20000, 1000, 1000);
  addVia("VDD", "MET4", "MET5", 20250, 20000, 500, 1000);
  FPPG.generate();

  int32_t via1_count = 0;
  int32_t via4_count = 0;
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() != PGSegmentType::kVia) {
      continue;
    }
    if (segment.get_bottom_layer_name() == "MET1" && segment.get_top_layer_name() == "MET2") {
      ++via1_count;
      if (!check(segment.get_via_width() == 1000 && segment.get_via_height() == 170 && segment.get_start_x() == 10000
                     && segment.get_start_y() == 9995,
                 "Overlapping VIA1 stacks must merge to their union")) {
        return false;
      }
    } else if (segment.get_bottom_layer_name() == "MET4" && segment.get_top_layer_name() == "MET5") {
      ++via4_count;
      if (!check(segment.get_via_width() == 1000 && segment.get_via_height() == 1000 && segment.get_start_x() == 20000
                     && segment.get_start_y() == 20000,
                 "Contained VIA4 stack must merge into the enclosing stack")) {
        return false;
      }
    }
  }
  return check(via1_count == 1, "Expected one merged VIA1 stack") && check(via4_count == 1, "Expected one merged VIA4 stack");
}

bool checkPGExport(const std::string& directory, idb::IdbLefService& lef, idb::IdbDefService& def, int32_t rail_width)
{
  idb::IdbDesign* design = def.get_design();
  design->set_design_name("PG_EXPORT");
  design->get_units()->set_microns_dbu(1000);
  lef.get_layout()->get_units()->set_microns_dbu(1000);
  lef.get_layout()->get_die()->add_point(0, 0);
  lef.get_layout()->get_die()->add_point(30000, 30000);
  for (std::string name : {"VDD_A", "VSS_B", "DATA"}) {
    design->get_net_list()->add_net(name, idb::IdbConnectType::kSignal);
  }

  idb::IdbCellMaster* master = lef.get_layout()->get_cell_master_list()->set_cell_master("TEST_MACRO");
  master->set_type("BLOCK");
  master->set_width(1000);
  master->set_height(1000);
  idb::IdbInstance* macro = design->get_instance_list()->add_instance("macro");
  macro->set_cell_master(master);
  macro->set_orient(idb::IdbOrient::kN_R0);
  macro->set_status_fixed();
  macro->set_coodinate(24000, 24000);
  idb::IdbRouteHalo* route_halo = macro->set_route_halo();
  route_halo->set_route_distance(100);
  route_halo->set_layer_bottom(lef.get_layout()->get_layers()->find_layer("M99"));
  route_halo->set_layer_top(lef.get_layout()->get_layers()->find_layer("M10"));

  FPI.outputPGNetList();
  FPI.outputPGNetList();
  bool passed = check(design->get_net_list()->get_num() == 1 && design->get_net_list()->find_net("DATA") != nullptr,
                      "PG export must remove empty shadow nets and preserve unrelated signal nets")
                && check(design->get_special_net_list()->get_num() == 2, "Repeated PG binding must not duplicate special nets");
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    segment.set_generated(true);
  }
  FPI.outputPGSegmentList();
  std::string path = directory + "/pg_export.def";
  idb::DefWrite writer(&def);
  passed = check(writer.writeDb(path.c_str()), "PG regression DEF must be written") && passed;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    passed = check(line.find("ROUTEHALO") == std::string::npos, "DEF must not reintroduce unsupported ROUTEHALO output") && passed;
  }

  idb::IdbDefService reloaded(lef.get_layout());
  idb::DefRead reader(&reloaded);
  passed = check(reader.createDb(path.c_str()), "Exported PG DEF must read back successfully") && passed;
  passed = check(reloaded.get_design()->get_special_net_list()->get_num() == 2, "DEF round-trip must preserve both PG nets") && passed;
  for (idb::IdbSpecialNet* original : design->get_special_net_list()->get_net_list()) {
    idb::IdbSpecialNet* net = reloaded.get_design()->get_special_net_list()->find_net(original->get_net_name());
    passed = check(net != nullptr, "DEF round-trip lost a PG net") && passed;
    if (net == nullptr) {
      continue;
    }
    idb::IdbConnectType expected_type = net->get_net_name() == "VDD_A" ? idb::IdbConnectType::kPower : idb::IdbConnectType::kGround;
    passed = check(net->get_connect_type() == expected_type, "DEF round-trip must preserve POWER/GROUND types") && passed;
    passed = check(reloaded.get_design()->get_net_list()->find_net(net->get_net_name()) == nullptr,
                   "DEF NETS and SPECIALNETS must not share PG names") && passed;
    passed = check(net->get_segment_num() == original->get_segment_num() && net->get_segment_num() > 0
                       && net->get_via_num() == original->get_via_num() && net->get_via_num() > 0,
                   "DEF round-trip must preserve routed PG segments and vias") && passed;
    int32_t rail_count = 0;
    for (idb::IdbSpecialWire* wire : net->get_wire_list()->get_wire_list()) {
      for (idb::IdbSpecialWireSegment* segment : wire->get_segment_list()) {
        if (segment->is_follow_pin()) {
          rail_count++;
          passed = check(segment->get_route_width() == rail_width, "DEF round-trip must preserve configured follow-pin width") && passed;
        }
      }
    }
    passed = check(rail_count == 1, "DEF must retain the follow-pin rail on each PG net") && passed;
  }
  return passed;
}

void addIDBViaRule(idb::IdbLefService& lef, std::string bottom_name, std::string cut_name, std::string top_name)
{
  idb::IdbLayers* layers = lef.get_layout()->get_layers();
  idb::IdbLayerRouting* bottom = dynamic_cast<idb::IdbLayerRouting*>(layers->set_layer(bottom_name, "ROUTING"));
  idb::IdbLayerCut* cut = dynamic_cast<idb::IdbLayerCut*>(layers->set_layer(cut_name, "CUT"));
  idb::IdbLayerRouting* top = dynamic_cast<idb::IdbLayerRouting*>(layers->set_layer(top_name, "ROUTING"));
  idb::IdbViaRuleGenerate* rule = lef.get_layout()->get_via_rule_list()->add_via_rule_generate(cut_name + "_RULE");
  rule->set_layer_bottom(bottom);
  rule->set_layer_cut(cut);
  rule->set_layer_top(top);
  rule->set_cut_rect(-45, -45, 45, 45);
  rule->set_spacing(200, 200);
  rule->get_enclosure_bottom()->set_overhang_1(40);
  rule->get_enclosure_bottom()->set_overhang_2(35);
  rule->get_enclosure_top()->set_overhang_1(40);
  rule->get_enclosure_top()->set_overhang_2(35);
  cut->set_via_rule(rule);
}

bool testConfiguredRailWidth(const std::string& directory)
{
  idb::IdbLefService* previous_lef = dmInst->get_idb_lef_service();
  idb::IdbDefService* previous_def = dmInst->get_idb_def_service();
  idb::IdbLefService lef;
  idb::IdbDefService def(lef.get_layout());
  dmInst->set_idb_lef_service(&lef);
  dmInst->set_idb_def_service(&def);
  FPInterface::destroyInst();

  addIDBViaRule(lef, "M99", "VIA_TEST", "M10");
  lef.get_layout()->set_manufacture_grid(5);

  // A 170-by-160 via is legal; its square span must not widen a 160 DBU rail.
  bool passed = check(FPI.getPGViaMinSize("M99", "M10") == std::make_pair(170, 160), "Asymmetric via fixture must be active");
  passed = check(FPI.getPGViaMinSpan("M99", "M10") == 170, "Via square span must differ from the required rail width") && passed;
  for (int32_t configured_width : {160, 161}) {
    initFixture(directory);
    Database& database = FPDM.getDatabase();
    database.set_micron_dbu(1000);
    database.set_manufacture_grid(5);
    Row row;
    row.set_rect(1001, 2003, 21001, 22003);
    row.set_y(2003);
    row.set_orient(PlacementOrientation::kN);
    database.get_row_list().push_back(row);
    PGRail rail;
    rail.set_layer_name("M99");
    rail.set_width_micron(configured_width / 1000.0);
    FPDM.getConfig().pg_rail_list.push_back(rail);
    addLine("VSS_B", "M10", PGSegmentType::kStripe, 5001, 2003, 5001, 22003, 1000);
    addLine("VDD_A", "M10", PGSegmentType::kStripe, 15001, 2003, 15001, 22003, 1000);
    addConnect("M99", "M10");
    FPI.resetPGViaAdjustment();
    FPPG.generate();

    int32_t expected_width = FPUTIL.alignUp(configured_width, 5);
    int32_t rail_count = 0;
    int32_t via_count = 0;
    for (PGSegment& segment : database.get_pg_segment_list()) {
      if (segment.get_type() == PGSegmentType::kFollowPin) {
        rail_count++;
        passed = check(segment.get_width() == expected_width, "Follow-pin rail must retain its grid-aligned configured width") && passed;
      } else if (segment.get_type() == PGSegmentType::kStripe) {
        passed = check(segment.get_width() == 1000, "Rail connection must not change stripe width") && passed;
      } else if (segment.get_type() == PGSegmentType::kVia) {
        via_count++;
        // Integer DBU edges use half-width on each side of the rail centerline.
        passed = check(segment.get_via_height() == 2 * (expected_width / 2), "Rail via height must match the rail overlap, not the square via span") && passed;
        passed = check(FPI.getPGViaStack(segment).size() == 1, "Rail must retain a legal generated via") && passed;
        for (InstancePinShape& shape : FPI.getPGViaShapes(segment)) {
          passed = check(shape.get_height() <= expected_width, "Via enclosure must fit the rail transverse width") && passed;
        }
      }
    }
    passed = check(rail_count == 2 && via_count == 2, "Both VDD and VSS rails must remain connected") && passed;
    if (configured_width == 160) {
      passed = checkPGExport(directory, lef, def, expected_width) && passed;
    }
  }

  FPInterface::destroyInst();
  dmInst->set_idb_def_service(previous_def);
  dmInst->set_idb_lef_service(previous_lef);
  return passed;
}

bool testViaMinimumArea(const std::string& directory)
{
  idb::IdbLefService* previous_lef = dmInst->get_idb_lef_service();
  idb::IdbDefService* previous_def = dmInst->get_idb_def_service();
  idb::IdbLefService lef;
  idb::IdbDefService def(lef.get_layout());
  dmInst->set_idb_lef_service(&lef);
  dmInst->set_idb_def_service(&def);
  FPInterface::destroyInst();
  initFixture(directory);
  lef.get_layout()->get_units()->set_microns_dbu(1000);
  def.get_design()->get_units()->set_microns_dbu(1000);
  lef.get_layout()->get_die()->add_point(0, 0);
  lef.get_layout()->get_die()->add_point(10000, 10000);
  def.get_design()->set_design_name("via_min_area");
  for (int32_t layer_idx = 1; layer_idx < 4; layer_idx++) {
    addIDBViaRule(lef, "MET" + std::to_string(layer_idx), "VIA" + std::to_string(layer_idx), "MET" + std::to_string(layer_idx + 1));
    idb::IdbLayerCut* cut = dynamic_cast<idb::IdbLayerCut*>(lef.get_layout()->get_layers()->find_layer("VIA" + std::to_string(layer_idx)));
    cut->get_via_rule()->get_enclosure_bottom()->set_overhang_2(5);
    cut->get_via_rule()->get_enclosure_top()->set_overhang_2(5);
    cut->get_via_rule()->get_layer_bottom()->set_area(52000);
    cut->get_via_rule()->get_layer_top()->set_area(52000);
  }

  bool passed = true;
  idb::IdbLayerCut* cut = dynamic_cast<idb::IdbLayerCut*>(lef.get_layout()->get_layers()->find_layer("VIA2"));
  for (int32_t grid : {1, 5}) {
    lef.get_layout()->set_manufacture_grid(grid);
    for (bool rotate : {false, true}) {
      PGSegment segment;
      segment.set_bottom_layer_name("MET1");
      segment.set_top_layer_name("MET4");
      segment.set_via_width(rotate ? 160 : 400);
      segment.set_via_height(rotate ? 400 : 160);
      PGViaGeometry geometry = FPI.getIDBViaGeometry(cut, segment);
      int32_t width = 90 + (geometry.cut_col_num - 1) * 200 + 2 * geometry.bottom_enclosure_x;
      int32_t height = 90 + (geometry.cut_row_num - 1) * 200 + 2 * geometry.bottom_enclosure_y;
      passed = check(geometry.cut_row_num * geometry.cut_col_num == 2, "Area repair must preserve the narrow two-cut array") && passed;
      passed = check(width * height >= 52000 && width <= segment.get_via_width() && height <= segment.get_via_height(),
                     "Via metal must meet AREA without leaving the original footprint") && passed;
      passed = check(geometry.bottom_enclosure_x % grid == 0 && geometry.bottom_enclosure_y % grid == 0,
                     "Enclosure growth must use the manufacturing grid") && passed;
      passed = check((rotate ? width : height) == (grid == 1 ? 142 : 150), "Only the deficient pad dimension should grow") && passed;
      passed = check(geometry.top_enclosure_x == geometry.bottom_enclosure_x && geometry.top_enclosure_y == geometry.bottom_enclosure_y,
                     "Both via pads must meet the same fixture area rule") && passed;
    }
  }
  lef.get_layout()->set_manufacture_grid(1);
  PGSegment narrow;
  narrow.set_bottom_layer_name("MET1");
  narrow.set_top_layer_name("MET4");
  narrow.set_via_width(400);
  narrow.set_via_height(100);
  passed = check(FPI.getPGViaStack(narrow).empty(), "An infeasible pad must not produce an undersized via stack") && passed;
  narrow.set_via_height(160);
  passed = check(FPI.getPGViaStack(narrow).size() == 3, "Area repair must retain all three vias in the rail-to-stripe stack") && passed;
  for (InstancePinShape& shape : FPI.getPGViaShapes(narrow)) {
    passed = check(shape.get_width() * shape.get_height() >= 52000 && shape.get_width() <= 400 && shape.get_height() <= 160,
                   "Cached/exported shapes must use the repaired area and stay inside the footprint") && passed;
  }
  narrow.set_via_width(1000);
  PGViaGeometry wide = FPI.getIDBViaGeometry(cut, narrow);
  passed = check(wide.cut_row_num == 1 && wide.cut_col_num == 5 && wide.bottom_enclosure_x == 40 && wide.bottom_enclosure_y == 5
                     && wide.top_enclosure_x == 40 && wide.top_enclosure_y == 5,
                 "Existing legal wide vias must remain unchanged") && passed;

  int32_t enclosure_x = 40;
  int32_t enclosure_y = 5;
  narrow.set_via_width(200);
  narrow.set_via_height(300);
  passed = check(FPI.adjustIDBViaEnclosureArea(cut->get_via_rule()->get_layer_bottom(), 90, 90, narrow, enclosure_x, enclosure_y)
                     && (90 + 2 * enclosure_x) * (90 + 2 * enclosure_y) >= 52000 && 90 + 2 * enclosure_x <= 200 && 90 + 2 * enclosure_y <= 300,
                 "Repair may use both axes when neither one-axis extension fits") && passed;

  std::string path = directory + "/via_area.def";
  idb::DefWrite writer(&def);
  passed = check(writer.writeDb(path.c_str()), "Via area fixture must export to DEF") && passed;
  idb::IdbDefService reloaded(lef.get_layout());
  idb::DefRead reader(&reloaded);
  passed = check(reader.createDb(path.c_str()), "Via area fixture must read back from DEF") && passed;
  idb::IdbVia* via = reloaded.get_design()->get_via_list()->find_via("VIA2_400x160");
  passed = check(via != nullptr, "DEF round-trip must retain the repaired via") && passed;
  if (via != nullptr) {
    idb::IdbRect rect = via->get_bottom_layer_shape().get_bounding_box();
    passed = check(rect.get_width() == 370 && rect.get_height() == 142, "DEF round-trip must preserve repaired enclosure dimensions") && passed;
  }

  FPInterface::destroyInst();
  dmInst->set_idb_def_service(previous_def);
  dmInst->set_idb_lef_service(previous_lef);
  return passed;
}

bool testTechnologyEdgeStripe(const std::string& directory)
{
  idb::IdbLefService* previous_lef = dmInst->get_idb_lef_service();
  idb::IdbDefService* previous_def = dmInst->get_idb_def_service();
  idb::IdbLefService lef;
  idb::IdbDefService def(lef.get_layout());
  dmInst->set_idb_lef_service(&lef);
  dmInst->set_idb_def_service(&def);
  FPInterface::destroyInst();
  lef.get_layout()->set_manufacture_grid(1);
  for (int32_t layer_idx = 1; layer_idx < 5; layer_idx++) {
    addIDBViaRule(lef, "MET" + std::to_string(layer_idx), "VIA" + std::to_string(layer_idx), "MET" + std::to_string(layer_idx + 1));
  }

  bool passed = testUncoveredEdgeStripe(directory);
  for (PGSegment& segment : FPDM.getDatabase().get_pg_segment_list()) {
    if (segment.get_type() != PGSegmentType::kVia) {
      continue;
    }
    int32_t expected_size = segment.get_bottom_layer_name() == "MET1" ? 3 : 1;
    passed = check(static_cast<int32_t>(FPI.getPGViaStack(segment).size()) == expected_size,
                   "Core-edge repair must retain the complete via stack from rail to mesh") && passed;
    if (segment.get_bottom_layer_name() == "MET1") {
      passed = check(segment.get_via_height() == 160, "Core-edge repair must fit the rail height through all three lower vias") && passed;
    }
  }

  FPInterface::destroyInst();
  dmInst->set_idb_def_service(previous_def);
  dmInst->set_idb_lef_service(previous_lef);
  return passed;
}

}  // namespace ifp

int main(int argc, char* argv[])
{
  if (argc != 2) {
    return EXIT_FAILURE;
  }
  std::string directory = argv[1];
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  ifp::DataManager::initInst();
  ifp::PDNGenerator::initInst();
  FPUTIL.createDir(directory);
  bool passed = ifp::testPGSegmentRect();
  passed = ifp::testBoundaryRailConnections(directory) && passed;
  passed = ifp::testUnconnectedRail(directory) && passed;
  passed = ifp::testDisconnectedMesh(directory) && passed;
  passed = ifp::testPhyRailCoverage(directory) && passed;
  passed = ifp::testGeneratedStripes(directory) && passed;
  passed = ifp::testConnectionTypes(directory) && passed;
  passed = ifp::testMissingConnections(directory) && passed;
  passed = ifp::testStripePlacementHalo(directory) && passed;
  passed = ifp::testActualViaOverlap(directory) && passed;
  passed = ifp::testRailExtensionHalo(directory) && passed;
  passed = ifp::testGapRailRepair(directory) && passed;
  passed = ifp::testNarrowGapRailRepair(directory) && passed;
  passed = ifp::testExactFitGapRailRepair(directory) && passed;
  passed = ifp::testTooNarrowGapRailRepair(directory) && passed;
  passed = ifp::testRepairClearanceBoundary(directory) && passed;
  passed = ifp::testBlockedRepairGroupSplit(directory) && passed;
  passed = ifp::testSplitRepairCandidateInheritance(directory) && passed;
  passed = ifp::testRepeatedRepairTopStripes(directory) && passed;
  passed = ifp::testUncoveredEdgeStripe(directory) && passed;
  passed = ifp::testOverlappingViaStacks(directory) && passed;
  passed = ifp::testDanglingFollowPinTrim(directory) && passed;
  passed = ifp::testConfiguredRailWidth(directory) && passed;
  passed = ifp::testViaMinimumArea(directory) && passed;
  passed = ifp::testTechnologyEdgeStripe(directory) && passed;
  ifp::PDNGenerator::destroyInst();
  ifp::DataManager::destroyInst();
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
