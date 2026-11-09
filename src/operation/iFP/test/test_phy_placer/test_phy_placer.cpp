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

void initPowerChannelFixture(int32_t narrow_width)
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
}

void initSparseRowFixture()
{
  initPowerChannelFixture(1200);
  Database& database = FPDM.getDatabase();
  database.get_core().set_rect(2000, 176000, 16000, 184000);
  database.get_row_list().clear();
  for (int32_t row_idx = 0; row_idx < 8; row_idx++) {
    int32_t y = 176000 + row_idx * 1000;
    Row row;
    row.set_name("ROW_" + std::to_string(row_idx));
    row.set_rect(2000, y, 16000, y + 1000);
    row.set_y(y);
    row.set_site_name("SITE");
    row.set_site_origin_x(2000);
    row.set_orient(row_idx % 2 == 0 ? PlacementOrientation::kN : PlacementOrientation::kFS);
    if (row_idx == 1) {
      // Multiple input fragments at one y must count as one missing row level.
      Row right_row = row;
      right_row.set_name(row.get_name() + "_RIGHT");
      right_row.set_rect(9000, y, 16000, y + 1000);
      row.set_rect(2000, y, 9000, y + 1000);
      database.get_row_list().push_back(right_row);
    }
    database.get_row_list().push_back(row);
  }
  database.set_new_row_list(database.get_row_list());
  database.get_instance_list().clear();
  for (const std::pair<int32_t, int32_t>& interval : {std::make_pair(177000, 179000), std::make_pair(181000, 182000)}) {
    Instance macro;
    macro.set_name("MACRO_" + std::to_string(interval.first));
    macro.set_macro(true);
    macro.set_placed(true);
    macro.set_orient(PlacementOrientation::kN);
    macro.set_coord(3200, interval.first);
    macro.set_width(11600);
    macro.set_height(interval.second - interval.first);
    macro.set_bounding_rect(3200, interval.first, 14800, interval.second);
    macro.set_placement_halo_rect(macro.get_bounding_rect());
    macro.set_routing_halo_rect(macro.get_bounding_rect());
    database.get_instance_list().push_back(macro);
  }
}

std::string placeMacrosAndGetLog()
{
  std::stringstream captured_log;
  std::streambuf* previous_buffer = std::cout.rdbuf(captured_log.rdbuf());
  try {
    FPMP.place();
  } catch (...) {
    std::cout.rdbuf(previous_buffer);
    std::cout << captured_log.str();
    throw;
  }
  std::cout.rdbuf(previous_buffer);
  std::cout << captured_log.str();
  return captured_log.str();
}

bool checkRowList(const std::vector<Row>& actual_row_list, const std::vector<Row>& expected_row_list, const std::string& message)
{
  bool passed = check(actual_row_list.size() == expected_row_list.size(), message);
  for (size_t row_idx = 0; row_idx < std::min(actual_row_list.size(), expected_row_list.size()); row_idx++) {
    const Row& actual = actual_row_list[row_idx];
    const Row& expected = expected_row_list[row_idx];
    passed = check(actual == expected && actual.get_name() == expected.get_name() && actual.get_site_name() == expected.get_site_name()
                       && actual.get_site_origin_x() == expected.get_site_origin_x() && actual.get_y() == expected.get_y()
                       && actual.get_orient() == expected.get_orient(),
                   message)
             && passed;
  }
  return passed;
}

bool testPowerChannel(int32_t narrow_width, bool expected_narrow_rows, bool placed_cell = false)
{
  initPowerChannelFixture(narrow_width);
  Database& database = FPDM.getDatabase();

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
  std::string log = placeMacrosAndGetLog();
  bool passed = check(database.get_row_list().size() == (expected_narrow_rows ? 6U : 3U), "Macro-cut rows must reserve both PG tracks and spacing");
  passed = checkRowList(database.get_new_row_list(), database.get_row_list(), "IDB export must use identical pruned row geometry") && passed;
  passed = check(log.find("Sparse row grid:") == std::string::npos, "A surviving fragment at every original y must not report missing row levels") && passed;
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

bool testSparseRowGrid()
{
  // Regression 014: both 1200 DBU slivers disappear at some y, with usable rows on either side.
  initSparseRowFixture();
  Database& database = FPDM.getDatabase();
  const std::vector<Instance> original_macro_list = database.get_instance_list();
  const std::set<int32_t> expected_row_indices = {0, 3, 4, 6, 7};
  std::vector<Row> expected_row_list;
  for (const Row& row : database.get_row_list()) {
    if (expected_row_indices.count((row.get_y() - 176000) / 1000) != 0) {
      expected_row_list.push_back(row);
    }
  }

  std::string log = placeMacrosAndGetLog();
  bool passed = check(log.find("Sparse row grid: 3 original row y-level(s) lost all usable fragments") != std::string::npos,
                      "Sparse-row diagnostic must count missing y levels, not x fragments");
  passed = check(log.find("[177000,179000) (2 row y-level(s)), [181000,182000) (1 row y-level(s))") != std::string::npos,
                 "Sparse-row diagnostic must group contiguous missing heights and keep separated gaps distinct")
           && passed;
  passed = check(log.find("Downstream placement/sizing must support sparse physical row indices") != std::string::npos,
                 "Sparse-row diagnostic must expose the downstream sparse-grid requirement")
           && passed;
  passed = checkRowList(database.get_row_list(), expected_row_list, "Surviving rows must retain their original geometry and orientation") && passed;
  passed = checkRowList(database.get_new_row_list(), expected_row_list, "Working and export rows must contain the same sparse geometry") && passed;
  std::set<int32_t> actual_row_indices;
  for (const Row& row : database.get_row_list()) {
    int32_t row_idx = (row.get_y() - 176000) / 1000;
    actual_row_indices.insert(row_idx);
    passed = check(row.get_y() == 176000 + row_idx * 1000
                       && row.get_orient() == (row_idx % 2 == 0 ? PlacementOrientation::kN : PlacementOrientation::kFS),
                   "Row y and orientation must follow physical indices, not compacted vector indices")
             && passed;
  }
  passed = check(actual_row_indices == expected_row_indices, "Sparse row indices must remain {0,3,4,6,7}, without padding or compaction") && passed;
  log = placeMacrosAndGetLog();
  passed = check(log.find("Sparse row grid:") == std::string::npos, "Pre-existing row holes must not be reported as newly removed y levels") && passed;

  FPPP.place();
  std::set<int32_t> physical_row_indices;
  std::map<int32_t, std::vector<std::pair<int32_t, int32_t>>> occupied_intervals;
  for (const Instance& instance : database.get_instance_list()) {
    if (instance.get_macro()) {
      continue;
    }
    int32_t row_idx = (instance.get_y() - 176000) / 1000;
    physical_row_indices.insert(row_idx);
    passed = check(expected_row_indices.count(row_idx) != 0 && instance.get_y() == 176000 + row_idx * 1000,
                   "No physical cell may occupy a missing row level")
             && passed;
    passed = check(instance.get_orient() == (row_idx % 2 == 0 ? PlacementOrientation::kN : PlacementOrientation::kFS),
                   "Physical-cell orientation must follow its actual row above and below each hole")
             && passed;
    int32_t x = instance.get_x();
    int32_t end_x = x + instance.get_width();
    bool contained = std::any_of(expected_row_list.begin(), expected_row_list.end(), [&instance, x, end_x](const Row& row) {
      return row.get_ll_x() <= x && end_x <= row.get_ur_x() && row.get_ll_y() <= instance.get_y()
             && instance.get_y() + instance.get_height() <= row.get_ur_y();
    });
    passed = check(contained && (x - 2000) % 200 == 0, "Physical cells must fit a surviving row on the original site grid") && passed;
    for (const std::pair<int32_t, int32_t>& interval : occupied_intervals[instance.get_y()]) {
      passed = check(end_x <= interval.first || interval.second <= x, "Physical cells around sparse-row holes must not overlap") && passed;
    }
    occupied_intervals[instance.get_y()].emplace_back(x, end_x);
  }
  passed = check(physical_row_indices == expected_row_indices, "Physical placement must still visit all surviving rows on both sides of each hole") && passed;
  passed = checkRowList(database.get_row_list(), expected_row_list, "Physical placement must preserve the sparse working rows") && passed;
  passed = checkRowList(database.get_new_row_list(), expected_row_list, "Physical placement must preserve identical sparse export rows") && passed;
  for (const Instance& original : original_macro_list) {
    auto macro_iter = std::find_if(database.get_instance_list().begin(), database.get_instance_list().end(),
                                   [&original](const Instance& instance) { return instance.get_name() == original.get_name(); });
    passed = check(macro_iter != database.get_instance_list().end(), "Sparse-row pruning must preserve every macro") && passed;
    if (macro_iter != database.get_instance_list().end()) {
      passed = check(macro_iter->get_x() == original.get_x() && macro_iter->get_y() == original.get_y()
                         && macro_iter->get_width() == original.get_width() && macro_iter->get_height() == original.get_height()
                         && macro_iter->get_orient() == original.get_orient() && macro_iter->get_bounding_rect() == original.get_bounding_rect()
                         && macro_iter->get_placement_halo_rect() == original.get_placement_halo_rect()
                         && macro_iter->get_routing_halo_rect() == original.get_routing_halo_rect(),
                     "Sparse-row handling must not move/resize macros or change their halos")
               && passed;
    }
  }
  return passed;
}

bool testSparseRowPlacedCell(int32_t x)
{
  initSparseRowFixture();
  Database& database = FPDM.getDatabase();
  const std::vector<Row> original_row_list = database.get_row_list();
  Instance instance;
  instance.set_name("KEEP_SPARSE");
  instance.set_placed(true);
  instance.set_from_netlist(true);
  instance.set_coord(x, 177000);
  instance.set_width(200);
  instance.set_height(1000);
  instance.set_bounding_rect(x, 177000, x + 200, 178000);
  database.get_instance_list().push_back(instance);
  bool rejected = false;
  try {
    FPMP.place();
  } catch (const std::runtime_error& error) {
    rejected = std::string(error.what()).find("contains placed instance 'KEEP_SPARSE'") != std::string::npos;
  }
  bool passed = check(rejected, "Removing all fragments at a y must still reject any placed logical cell in either sliver");
  passed = check(database.get_instance_list().size() == 3 && database.get_instance_list().back().get_name() == instance.get_name()
                     && database.get_instance_list().back().get_x() == x && database.get_instance_list().back().get_y() == 177000
                     && database.get_instance_list().back().get_bounding_rect() == instance.get_bounding_rect()
                     && !database.get_instance_list().back().is_delete_instance(),
                 "Sparse-row pruning must not delete or displace a placed logical cell")
           && passed;
  passed = checkRowList(database.get_row_list(), original_row_list, "Rejected sparse-row pruning must leave working rows unchanged") && passed;
  passed = checkRowList(database.get_new_row_list(), original_row_list, "Rejected sparse-row pruning must leave export rows unchanged") && passed;
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
  passed = ifp::testSparseRowGrid() && passed;
  passed = ifp::testSparseRowPlacedCell(2000) && passed;
  passed = ifp::testSparseRowPlacedCell(14800) && passed;
  ifp::MacroPlacer::destroyInst();
  ifp::PhyPlacer::destroyInst();
  ifp::DataManager::destroyInst();
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
