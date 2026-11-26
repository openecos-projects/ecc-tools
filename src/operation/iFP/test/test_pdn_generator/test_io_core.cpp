#include <fstream>
#include <iterator>

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "PDNGenerator.hpp"
#include "idm.h"
#include "test_pdn_fixtures.hpp"

namespace ifp {

bool testIOCoreDetour(const std::string& directory)
{
  auto* previous_lef = dmInst->get_idb_lef_service();
  auto* previous_def = dmInst->get_idb_def_service();
  idb::IdbLefService lef;
  idb::IdbDefService def(lef.get_layout());
  dmInst->set_idb_lef_service(&lef);
  dmInst->set_idb_def_service(&def);
  FPInterface::destroyInst();
  addIDBViaRule(lef, "M99", "VIA_LOWER", "M10");
  addIDBViaRule(lef, "M10", "VIA_UPPER", "M2");
  lef.get_layout()->set_manufacture_grid(5);

  enum class Scenario { kStraight, kBlocked, kOffset };
  bool passed = true;
  for (const auto scenario : {Scenario::kStraight, Scenario::kBlocked, Scenario::kOffset}) {
    const bool blocked = scenario == Scenario::kBlocked;
    const bool offset = scenario == Scenario::kOffset;
    initFixture(directory);
    auto& database = FPDM.getDatabase();
    database.set_micron_dbu(1000);
    database.set_manufacture_grid(5);
    database.get_die().set_rect(0, 0, 100000, 100000);
    database.get_core().set_rect(20000, 20000, 80000, 80000);
    auto& options = FPDM.getConfig().pg_io_core_connect;
    options.enabled = true;
    options.nets = {"VDD_A"};
    options.pin_layers = {"M10"};
    options.fail_on_unresolved = true;

    CellMaster pad;
    pad.set_name("power_pad");
    pad.set_width(10000);
    pad.set_height(10000);
    pad.set_pad(true);
    pad.get_pg_pin_types()["PWR"] = PGNetType::kPower;
    InstancePinShape rail;
    rail.set_pin_name("PWR");
    rail.set_layer_name("M10");
    rail.set_core_port(true);
    rail.set_rect(9000, 3000, 10000, 7000);
    pad.get_pg_pin_shapes().push_back(rail);
    database.get_cell_master_map()[pad.get_name()] = pad;
    Instance instance;
    instance.set_name("pad");
    instance.set_master_name(pad.get_name());
    instance.set_coord(0, 40000);
    instance.set_width(10000);
    instance.set_height(10000);
    instance.set_bounding_rect(0, 40000, 10000, 50000);
    instance.set_orient(PlacementOrientation::kN);
    instance.set_placed(true);
    instance.set_io_cell_net_name("VDD_A");
    instance.set_io_cell_pin_name("PWR");
    database.get_instance_name_to_idx_map()[instance.get_name()] = 0;
    database.get_instance_list().push_back(instance);

    addConnect("M10", "M2");
    addLine("VDD_A", "M10", PGSegmentType::kStripe, 30000, offset ? 55000 : 20000, 30000, 80000, 1000);
    addLine("VDD_A", "M2", PGSegmentType::kStripe, 20000, 60000, 80000, 60000, 1000);
    // The pad's rail is inside the core's Y range, but the opposite supply
    // blocks every same-layer straight bridge to its own stripe.
    const int32_t ground_x = blocked ? 25000 : 50000;
    addLine("VSS_B", "M10", PGSegmentType::kStripe, ground_x, 20000, ground_x, 80000, 1000);
    addLine("VSS_B", "M2", PGSegmentType::kStripe, 20000, 30000, 80000, 30000, 1000);
    if (blocked) {
      // An earlier opposite-supply IO connection occupies the normal detour
      // corridor. The layer-change escape must occur before that conductor.
      addLine("VSS_B", "M10", PGSegmentType::kIoWire, 18000, 40000, 18000, 60000, 1000);
      addLine("VSS_B", "M10", PGSegmentType::kIoWire, 18000, 60000, ground_x, 60000, 1000);
    }
    FPI.resetPGViaAdjustment();
    FPPG.generate();

    std::ifstream report(directory + "/io_core_connect.rpt");
    const std::string text{std::istreambuf_iterator<char>(report), std::istreambuf_iterator<char>()};
    passed = check(text.find("Connected ports: 1\nUnresolved ports: 0\n") != std::string::npos,
                   "In-range power pad must connect even when a foreign stripe blocks its straight path") && passed;
    bool changed_layer = false;
    for (const auto& segment : database.get_pg_segment_list()) {
      if (segment.get_type() == PGSegmentType::kIoWire && segment.get_layer_name() != "M10") {
        changed_layer = true;
      }
      if (blocked && segment.get_type() == PGSegmentType::kIoWire && segment.get_layer_name() == "M10") {
        if (segment.get_net_name() != "VDD_A") continue;
        passed = check(segment.get_ur_x() <= ground_x - 500 || segment.get_ll_x() >= ground_x + 500
                           || segment.get_ur_y() <= 20000 || segment.get_ll_y() >= 80000,
                       "IO fallback must not cross the foreign supply on the same layer") && passed;
        passed = check(segment.get_ur_x() <= 17500 || segment.get_ll_x() >= 18500
                           || segment.get_ur_y() <= 40000 || segment.get_ll_y() >= 60000,
                       "Layer-change escape must avoid an existing IO detour corridor") && passed;
      }
    }
    passed = check(changed_layer == blocked, "Only the obstructed pad needs a checked layer-change detour") && passed;
    if (offset) {
      passed = check(text.find("Manhattan bridge connections: 0\n") == std::string::npos,
                     "A left-side pad must reach a tangentially offset stripe with a valid Manhattan bridge") && passed;
    }
  }
  FPInterface::destroyInst();
  dmInst->set_idb_def_service(previous_def);
  dmInst->set_idb_lef_service(previous_lef);
  return passed;
}

}  // namespace ifp
