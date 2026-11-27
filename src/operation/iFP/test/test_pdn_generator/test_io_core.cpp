#include <fstream>
#include <iterator>
#include <stdexcept>

#include "DataManager.hpp"
#include "FPInterface.hpp"
#include "PDNGenerator.hpp"
#include "idm.h"
#include "test_pdn_fixtures.hpp"

namespace ifp {

bool testIOCoreStraightOnly(const std::string& directory)
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

  enum class Scenario
  {
    kBridge,
    kExtension,
    kBlocked,
    kOffset,
    kOutsideCore,
    kBlockedAdvisory
  };
  bool passed = true;
  for (const auto scenario :
       {Scenario::kBridge, Scenario::kExtension, Scenario::kBlocked, Scenario::kOffset, Scenario::kOutsideCore, Scenario::kBlockedAdvisory}) {
    const bool extension = scenario == Scenario::kExtension;
    const bool blocked = scenario == Scenario::kBlocked || scenario == Scenario::kBlockedAdvisory;
    const bool offset = scenario == Scenario::kOffset;
    const bool outside = scenario == Scenario::kOutsideCore;
    const bool expected_connected = scenario == Scenario::kBridge || extension;
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
    options.fail_on_unresolved = scenario != Scenario::kBlockedAdvisory;

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
    const int32_t pad_y = outside ? 85000 : 40000;
    instance.set_coord(0, pad_y);
    instance.set_width(10000);
    instance.set_height(10000);
    instance.set_bounding_rect(0, pad_y, 10000, pad_y + 10000);
    instance.set_orient(PlacementOrientation::kN);
    instance.set_placed(true);
    instance.set_io_cell_net_name("VDD_A");
    instance.set_io_cell_pin_name("PWR");
    database.get_instance_name_to_idx_map()[instance.get_name()] = 0;
    database.get_instance_list().push_back(instance);

    addConnect("M10", "M2");
    if (extension) {
      addLine("VDD_A", "M10", PGSegmentType::kStripe, 30000, 45000, 80000, 45000, 1000);
      addLine("VDD_A", "M2", PGSegmentType::kStripe, 60000, 20000, 60000, 80000, 1000);
    } else {
      addLine("VDD_A", "M10", PGSegmentType::kStripe, 30000, offset ? 55000 : 20000, 30000, 80000, 1000);
      addLine("VDD_A", "M2", PGSegmentType::kStripe, 20000, 60000, 80000, 60000, 1000);
    }
    // The pad's rail is inside the core's Y range, but the opposite supply
    // blocks every same-layer straight bridge to its own stripe.
    const int32_t ground_x = blocked ? 25000 : 50000;
    addLine("VSS_B", "M10", PGSegmentType::kStripe, ground_x, 20000, ground_x, 80000, 1000);
    addLine("VSS_B", "M2", PGSegmentType::kStripe, 20000, 30000, 80000, 30000, 1000);
    if (blocked) {
      // Existing foreign IO metal must not be crossed or bypassed by a jog.
      addLine("VSS_B", "M10", PGSegmentType::kIoWire, 18000, 40000, 18000, 60000, 1000);
      addLine("VSS_B", "M10", PGSegmentType::kIoWire, 18000, 60000, ground_x, 60000, 1000);
    }
    FPI.resetPGViaAdjustment();
    bool failed = false;
    try {
      FPPG.generate();
    } catch (const std::runtime_error& error) {
      failed = true;
      const std::string expected_error = options.fail_on_unresolved ? "IO core PG incomplete" : "Cannot write power.ploc: no connected IO power cell";
      passed = check(std::string(error.what()).find(expected_error) != std::string::npos, "Unconnected IO power must preserve the connectivity and PLOC gates")
               && passed;
    }
    passed
        = check(failed == !expected_connected, "Strict connectivity fails first; without it, zero connected supply pads still prevents PLOC output") && passed;

    std::ifstream report(directory + "/io_core_connect.rpt");
    const std::string text{std::istreambuf_iterator<char>(report), std::istreambuf_iterator<char>()};
    const std::string expected = expected_connected ? "Connected ports: 1\nUnresolved ports: 0\n" : "Connected ports: 0\nUnresolved ports: 1\n";
    passed = check(text.find(expected) != std::string::npos, "IO report must distinguish straight connections from unresolved ports") && passed;
    passed = check(text.find("policy=straight") != std::string::npos, "IO report must state the active routing policy") && passed;
    int32_t wire_count = 0;
    for (const auto& segment : database.get_pg_segment_list()) {
      if (segment.get_type() != PGSegmentType::kIoWire || segment.get_net_name() != "VDD_A")
        continue;
      ++wire_count;
      passed = check(segment.get_layer_name() == "M10" && segment.is_horizontal() && segment.get_start_y() >= 43000 && segment.get_start_y() <= 47000,
                     "Every accepted left-pad access must stay on its same-layer horizontal line")
               && passed;
    }
    passed = check((wire_count > 0) == expected_connected, "Only a legal straight connection may add IO access wires") && passed;
  }
  FPInterface::destroyInst();
  dmInst->set_idb_def_service(previous_def);
  dmInst->set_idb_lef_service(previous_lef);
  return passed;
}

}  // namespace ifp
