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
  macro.get_placement_halo_rect().set_rect(2000 + narrow_width, 176000, 8000, 179000);
  database.get_instance_list().reserve(256);
  database.get_instance_list().push_back(macro);
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
  ifp::DataManager::initInst();
  ifp::PhyPlacer::initInst();
  bool passed = ifp::testMacroCutRow(200, 0);
  passed = ifp::testMacroCutRow(400, 1) && passed;
  passed = ifp::testMacroCutRow(600, 0) && passed;
  passed = ifp::testMacroCutRow(800, 2) && passed;
  ifp::PhyPlacer::destroyInst();
  ifp::DataManager::destroyInst();
  ifp::Utility::destroyInst();
  ifp::Logger::destroyInst();
  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
