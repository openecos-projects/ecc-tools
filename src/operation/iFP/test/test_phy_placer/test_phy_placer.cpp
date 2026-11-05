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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "DataManager.hpp"
#include "Logger.hpp"
#include "MacroPlacer.hpp"
#include "PhyPlacer.hpp"
#include "Utility.hpp"

namespace ifp {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    FPLOG.warn(Loc::current(), message);
  }
  return condition;
}

void initFixture(int32_t narrow_width)
{
  FPDM.getConfig() = Config();
  FPDM.getDatabase() = Database();
  Config& config = FPDM.getConfig();
  config.tapcell_name = "TAP";
  config.left_endcap_name = "SIDE";
  config.right_endcap_name = "SIDE";
  config.top_endcap_name_list = {"EDGE"};
  config.bottom_endcap_name_list = {"EDGE"};
  config.top_boundary_tap_name_list = {"EDGE"};
  config.bottom_boundary_tap_name_list = {"EDGE"};
  config.tap_distance_micron = 4.0;
  config.boundary_tap_rule_micron = 2.0;

  Database& database = FPDM.getDatabase();
  database.set_micron_dbu(1000);
  database.get_core().set_rect(2000, 176000, 16000, 179000);
  database.get_core().set_core_site_name("SITE");
  Site site;
  site.set_name("SITE");
  site.set_width(200);
  site.set_height(1000);
  database.get_site_map()[site.get_name()] = site;
  for (std::string name : {"SIDE", "TAP", "EDGE"}) {
    CellMaster master;
    master.set_name(name);
    master.set_width(name == "EDGE" ? 200 : 400);
    master.set_height(1000);
    master.set_symmetry_x(true);
    database.get_cell_master_map()[name] = master;
  }
  for (int32_t row_idx = 0; row_idx < 3; row_idx++) {
    int32_t y = 176000 + row_idx * 1000;
    Row row;
    row.set_rect(2000, y, 16000, y + 1000);
    row.set_y(y);
    row.set_site_name("SITE");
    row.set_site_origin_x(2000);
    row.set_orient(row_idx % 2 == 0 ? PlacementOrientation::kN : PlacementOrientation::kFS);
    database.get_row_list().push_back(row);
  }

  Instance macro;
  macro.set_name("MACRO");
  macro.set_macro(true);
  macro.set_placed(true);
  macro.set_coord(2000 + narrow_width, 176000);
  macro.set_width(6000 - narrow_width);
  macro.set_height(3000);
  macro.set_bounding_rect(2000 + narrow_width, 176000, 8000, 179000);
  macro.get_placement_halo_rect().set_rect(2000 + narrow_width, 176000, 8000, 179000);
  std::vector<Instance>().swap(database.get_instance_list());
  database.get_instance_list().push_back(macro);
}

bool testPowerChannel(int32_t narrow_width, bool expected_narrow_rows, bool placed_cell = false)
{
  initFixture(narrow_width);
  Database& database = FPDM.getDatabase();
  Config& config = FPDM.getConfig();
  config.macro_placement_halo = 0.0;
  config.macro_routing_halo = 0.0;
  RoutingLayer layer;
  layer.set_name("VERTICAL");
  layer.set_prefer_direction(Direction::kVertical);
  layer.set_spacing(100);
  database.get_routing_layer_name_to_idx_map()[layer.get_name()] = 0;
  database.get_routing_layer_list().push_back(layer);
  PGStripe stripe;
  stripe.set_layer_name(layer.get_name());
  stripe.set_width_micron(1.0);
  config.pg_stripe_list.push_back(stripe);
  PGRail rail;
  rail.set_layer_name("RAIL");
  config.pg_rail_list.push_back(rail);
  PGLayerPair pair;
  pair.set_first_layer_name(rail.get_layer_name());
  pair.set_second_layer_name(layer.get_name());
  config.pg_layer_pair_list.push_back(pair);

  if (placed_cell) {
    Instance instance;
    instance.set_name("KEEP");
    instance.set_placed(true);
    instance.set_bounding_rect(2000, 176000, 2200, 177000);
    database.get_instance_list().push_back(instance);
    bool rejected = false;
    try {
      FPMP.place();
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("contains placed instance") != std::string::npos;
    }
    return check(rejected && database.get_instance_list().size() == 2 && database.get_row_list().size() == 3,
                 "Pruning must not delete or displace an existing logical instance");
  }
  FPMP.place();
  bool passed = check(database.get_row_list().size() == (expected_narrow_rows ? 6U : 3U), "Macro-cut rows must reserve both PG tracks and spacing");
  passed = check(database.get_new_row_list().size() == database.get_row_list().size(), "IDB export must use the same pruned row list") && passed;
  FPPP.place();
  for (const Instance& instance : database.get_instance_list()) {
    if (instance.get_macro()) {
      passed = check(instance.get_x() == 2000 + narrow_width, "Row pruning must not move the macro") && passed;
      continue;
    }
    if (!expected_narrow_rows) {
      passed = check(instance.get_x() >= 8000, "No physical cells may be inserted into discarded row slivers") && passed;
    }
  }
  return passed;
}

bool testMacroCutRow(int32_t narrow_width, int32_t expected_side_count)
{
  initFixture(narrow_width);
  FPPP.place();
  bool passed = true;
  int32_t narrow_side_count = 0;
  int32_t wide_side_count = 0;
  std::map<int32_t, std::vector<std::pair<int32_t, int32_t>>> row_intervals;
  for (Instance& instance : FPDM.getDatabase().get_instance_list()) {
    if (instance.get_macro()) {
      continue;
    }
    int32_t x = instance.get_x();
    int32_t y = instance.get_y();
    int32_t end_x = x + instance.get_width();
    passed = check(y >= 176000 && y < 179000 && (y - 176000) % 1000 == 0, "Physical cell must occupy a valid row") && passed;
    passed = check((x - 2000) % 200 == 0, "Physical cell must align to its site") && passed;
    passed = check((2000 <= x && end_x <= 2000 + narrow_width) || (8000 <= x && end_x <= 16000),
                   "Physical cell must stay inside a macro-cut row interval") && passed;
    for (const std::pair<int32_t, int32_t>& interval : row_intervals[y]) {
      passed = check(end_x <= interval.first || interval.second <= x, "Physical cells must not overlap") && passed;
    }
    row_intervals[y].emplace_back(x, end_x);
    if (y == 177000 && instance.get_master_name() == "SIDE") {
      if (x < 8000) {
        narrow_side_count++;
      } else {
        wide_side_count++;
      }
    }
  }
  passed = check(narrow_side_count == expected_side_count, "Narrow row side endcaps must follow the fit/exact-fit rule") && passed;
  passed = check(wide_side_count == 2, "Skipping a narrow interval must not skip the following wide interval") && passed;
  return passed;
}

}  // namespace ifp

int main()
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  ifp::DataManager::initInst();
  ifp::PhyPlacer::initInst();
  ifp::MacroPlacer::initInst();
  bool passed = ifp::testMacroCutRow(200, 0);
  passed = ifp::testMacroCutRow(400, 1) && passed;
  passed = ifp::testMacroCutRow(600, 0) && passed;
  passed = ifp::testMacroCutRow(800, 2) && passed;
  passed = ifp::testPowerChannel(200, false) && passed;
  passed = ifp::testPowerChannel(1200, false) && passed;
  passed = ifp::testPowerChannel(2000, false) && passed;
  passed = ifp::testPowerChannel(2200, true) && passed;
  passed = ifp::testPowerChannel(200, false, true) && passed;
  ifp::MacroPlacer::destroyInst();
  ifp::PhyPlacer::destroyInst();
  ifp::DataManager::destroyInst();
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
