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
  bool passed = ifp::testGeneratedStripes(directory);
  passed = ifp::testConnectionTypes(directory) && passed;
  passed = ifp::testMissingConnections(directory) && passed;
  ifp::PDNGenerator::destroyInst();
  ifp::DataManager::destroyInst();
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
