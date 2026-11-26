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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <boost/polygon/polygon.hpp>

#include "AntennaChecker.hpp"
#include "IdbDesign.h"
#include "IdbGeometry.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbRegularWire.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"
#include "MJInterface.hpp"
#include "idm.h"
#include "json.hpp"

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string readText(const std::filesystem::path& path)
{
  std::ifstream in(path);
  require(in.is_open(), "Failed to read file: " + path.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  require(out.is_open(), "Failed to write file: " + path.string());
  out << text;
}

bool rectsViolateSpacing(const idb::IdbRect& a, const idb::IdbRect& b, int32_t spacing)
{
  const int64_t sp = spacing;
  return static_cast<int64_t>(a.get_low_x()) < static_cast<int64_t>(b.get_high_x()) + sp &&
         static_cast<int64_t>(a.get_high_x()) + sp > static_cast<int64_t>(b.get_low_x()) &&
         static_cast<int64_t>(a.get_low_y()) < static_cast<int64_t>(b.get_high_y()) + sp &&
         static_cast<int64_t>(a.get_high_y()) + sp > static_cast<int64_t>(b.get_low_y());
}

const char* kTechLef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";

const char* kCellLef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO GATE_M2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M2
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";

class AntennaRepairTest
{
 public:
  void run()
  {
    setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
    std::filesystem::create_directories(_root);
    writeText(_tech_lef, kTechLef);
    writeText(_cell_lef, kCellLef);

    testM1ObstacleDetourRepair();
    testViaBridgeOverBlockedM1();
    testLayerQualifiedDiodeDiscovery();
    testCongestedNoSiteRollback();
    testM1ViolationDoesNotBridgeViaM2();
    testTieCellNotSelectedAsDiode();
    testWrongLayerDiodeRejected();
    testSpecialNetRectObstacleNotDoubleShifted();
    testDiodePGPinShortRejected();
    testCoalescedPRLSpacingReroutesCleanly();
    testRepairRoutingBoundedByDieArea();
    testSegmentedTargetWirePRLInvariant();
    testPendingDiodeSignalCutSpacingValidated();
    testPendingMultiNewViaCutSpacingValidated();
    testMultiDiodeRepairProgress();
    testManufacturingGridAlignment();
    testIntermediateViaStackMinArea();
    testMaxViaStackEnforcedAndStaggered();
    testMacroSymmetryFenceAndHaloPlacement();
    testEndOfLineSpacingAndCombinedSpacingTable();
    testSameNetSpacingNotchAndMinEnclosedArea();
    testMaxWidthMinStepAndWireExtension();
    testDefRoutingBlockageAndMacroObsSpacing();
    testAdjacentCutsAndGridCompatibleViaSelection();
    testCrossLayerAntennaDiffAreaOriginAndDefRect();

    dmInst->reset();
    std::filesystem::remove_all(_root);
  }

 private:
  std::filesystem::path _root = std::filesystem::temp_directory_path() / ("test_imj_antenna_repair_" + std::to_string(getpid()));
  std::filesystem::path _tech_lef = _root / "tech.lef";
  std::filesystem::path _cell_lef = _root / "cells.lef";

  void loadCase(const std::string& name, const std::string& def_text)
  {
    std::filesystem::path case_dir = _root / name;
    std::filesystem::path def_path = case_dir / "input.def";
    writeText(def_path, def_text);

    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
    require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
    require(dmInst->readDef(def_path.string()), "read DEF for " + name);
  }

  void verifyRoundTripClean(const std::filesystem::path& def_out, const std::string& mj_tmp)
  {
    require(dmInst->saveDef(def_out.string()), "save repaired DEF");
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "reload tech LEF");
    require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "reload cell LEF");
    require(dmInst->readDef(def_out.string()), "reload repaired DEF");

    MJI.initMJ({{"-temp_directory_path", mj_tmp}});
    std::filesystem::path check_json = def_out.parent_path() / "recheck.json";
    MJI.checkAntenna({{"-step_json", check_json.string()}});
    MJI.destroyMJ();

    nlohmann::json parsed = nlohmann::json::parse(readText(check_json));
    require(parsed.at("antenna_check").at("violation_count").get<int>() == 0,
            "expected 0 violations after DEF save/reload round-trip, got: " + parsed.dump());
  }

  void verifyCustomRoundTripClean(const std::filesystem::path& tech_lef, const std::filesystem::path& cell_lef,
                                    const std::filesystem::path& def_out, const std::string& mj_tmp)
  {
    require(dmInst->saveDef(def_out.string()), "save custom repaired DEF");
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{tech_lef.string()}, true), "reload custom tech LEF");
    require(dmInst->readLef(std::vector<std::string>{cell_lef.string()}), "reload custom cell LEF");
    require(dmInst->readDef(def_out.string()), "reload custom repaired DEF");

    MJI.initMJ({{"-temp_directory_path", mj_tmp}});
    std::filesystem::path check_json = def_out.parent_path() / "recheck.json";
    MJI.checkAntenna({{"-step_json", check_json.string()}});
    MJI.destroyMJ();

    nlohmann::json parsed = nlohmann::json::parse(readText(check_json));
    require(parsed.at("antenna_check").at("violation_count").get<int>() == 0,
            "expected 0 violations after custom DEF round-trip, got: " + parsed.dump());
  }

  void testM1ObstacleDetourRepair()
  {
    // Sites between x=12000 and x=18000: blocker occupies x=14000..16000 + foreign M1 obstacle at x=16000, y=[2500..3500].
    // Nearest free site left of blocker is (12000, 0), forcing an M1 detour around the foreign M1 wire.
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN repair_m1_detour ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 12 BY 1 STEP 2000 0 ;
COMPONENTS 7 ;
- u_block_0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_6 FILL_CELL + PLACED ( 14000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 18000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 19000 3000 ) ( 19000 10000 ) ;
- obs_net
  + USE SIGNAL
  + ROUTED M1 ( 16000 2500 ) ( 16000 3500 ) ;
END NETS
END DESIGN
)";

    loadCase("m1_detour", def);
    std::filesystem::path case_dir = _root / "m1_detour";
    std::filesystem::path step_json = case_dir / "repair.step.json";
    std::filesystem::path rpt_path = case_dir / "antenna_repair.rpt";

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    require(MJI.repairAntenna({{"-max_iter", 2},
                               {"-search_radius_um", 30.0},
                               {"-report_path", rpt_path.string()},
                               {"-step_json", step_json.string()}}),
            "repairAntenna should return true");
    MJI.destroyMJ();

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    require(payload.at("step").get<std::string>() == "antenna_repair",
            "expected step name antenna_repair in step JSON");
    const auto& ar = payload.at("antenna_repair");
    require(ar.at("violations_before").get<int>() > 0, "expected violations before M1 detour repair");
    require(ar.at("violations_after").get<int>() == 0, "expected 0 violations after M1 detour repair: " + payload.dump());
    require(ar.at("diodes_inserted").get<int>() >= 1, "expected diode insertion");

    // Verify no spacing violation or short with foreign M1 obstacle net obs_net.
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
    idb::IdbNet* obs_net = design->get_net_list()->find_net("obs_net");
    require(viol_net && obs_net, "expected both nets");
    idb::IdbRect obs_rect = obs_net->get_wire_list()->get_wire_list()[0]->get_segment_list()[0]->get_segment_rect();
    for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
        if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M1") {
          idb::IdbRect wr = s->get_segment_rect();
          require(!rectsViolateSpacing(wr, obs_rect, 1000), "M1 stub violated spacing against foreign M1 obstacle");
        }
      }
    }

    verifyRoundTripClean(case_dir / "repaired.def", (case_dir / "mj_recheck").string());
  }

  void testViaBridgeOverBlockedM1()
  {
    // GATE_M2 has a violating M2 antenna, and a full-height foreign M1 wall spans x=16000, y=[500..11500].
    // The diode pin is on M1 at x<=14000, so the stub must insert V12 vias and bridge across on M2.
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN repair_via_bridge ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 12 BY 1 STEP 2000 0 ;
COMPONENTS 8 ;
- u_block_0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_6 FILL_CELL + PLACED ( 14000 0 ) N ;
- u_block_7 FILL_CELL + PLACED ( 16000 0 ) N ;
- u_gate GATE_M2 + PLACED ( 18000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_m2 ( u_gate A )
  + USE SIGNAL
  + ROUTED M2 ( 19000 3000 ) ( 19000 11000 ) ;
- m1_wall
  + USE SIGNAL
  + ROUTED M1 ( 16000 500 ) ( 16000 11500 ) ;
END NETS
END DESIGN
)";

    loadCase("via_bridge", def);
    std::filesystem::path case_dir = _root / "via_bridge";
    std::filesystem::path step_json = case_dir / "repair.step.json";

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                       {"-max_iter", 2},
                       {"-search_radius_um", 30.0},
                       {"-step_json", step_json.string()}});
    MJI.destroyMJ();

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    const auto& ar = payload.at("antenna_repair");
    require(ar.at("violations_before").get<int>() > 0, "expected violations before via-bridge repair");
    require(ar.at("violations_after").get<int>() == 0, "expected 0 violations after via-bridge repair: " + payload.dump());
    require(ar.at("diodes_inserted").get<int>() == 1, "expected 1 diode inserted");
    require(payload.at("placed_diodes").at(0).at("vias_added").get<int>() >= 1, "expected via bridge across blocked M1");

    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_m2");
    idb::IdbNet* wall_net = design->get_net_list()->find_net("m1_wall");
    idb::IdbRect wall_rect = wall_net->get_wire_list()->get_wire_list()[0]->get_segment_list()[0]->get_segment_rect();
    for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
        if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M1") {
          require(!rectsViolateSpacing(s->get_segment_rect(), wall_rect, 1000),
                  "M1 segment shorted or violated spacing against full-height M1 wall");
        }
      }
    }

    verifyRoundTripClean(case_dir / "repaired.def", (case_dir / "mj_recheck").string());
  }

  void testLayerQualifiedDiodeDiscovery()
  {
    const std::string layer_cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_OX1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAMODEL OXIDE1 ;
      ANTENNAGATEAREA 1 LAYER M1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_OX1
MACRO ANTENNA_M1_DIO
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 LAYER M1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END ANTENNA_M1_DIO
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN repair_layer_diode ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 6000 ) ;
ROW ROW_0 CORE 0 0 N DO 8 BY 1 STEP 2000 0 ;
COMPONENTS 1 ;
- u_gate GATE_OX1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 6000 ) ;
END NETS
END DESIGN
)";
    std::filesystem::path case_dir = _root / "layer_diode";
    std::filesystem::path lcell = case_dir / "layer_cells.lef";
    std::filesystem::path ldef = case_dir / "input.def";
    std::filesystem::path step_json = case_dir / "repair.step.json";
    writeText(lcell, layer_cell_lef);
    writeText(ldef, def);

    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
    require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read layer-qualified cell LEF");
    require(dmInst->readDef(ldef.string()), "read DEF");

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    MJI.repairAntenna({{"-max_iter", 2},
                       {"-search_radius_um", 20.0},
                       {"-step_json", step_json.string()}});
    MJI.destroyMJ();

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    const auto& ar = payload.at("antenna_repair");
    require(ar.at("violations_before").get<int>() > 0, "expected violation before layer-qualified diode repair");
    require(ar.at("violations_after").get<int>() == 0, "expected 0 violations after layer-qualified diode repair: " + payload.dump());
    require(ar.at("diodes_inserted").get<int>() == 1, "expected 1 auto-discovered ANTENNA_M1_DIO diode");
  }

  void testCongestedNoSiteRollback()
  {
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN repair_full_row ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 8000 6000 ) ;
ROW ROW_0 CORE 0 0 N DO 4 BY 1 STEP 2000 0 ;
COMPONENTS 4 ;
- u_fill0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_fill1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 4000 0 ) N ;
- u_fill3 FILL_CELL + PLACED ( 6000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 5000 3000 ) ( 5000 6000 ) ;
END NETS
END DESIGN
)";

    loadCase("congested", def);
    std::filesystem::path case_dir = _root / "congested";
    std::filesystem::path before_def = case_dir / "before.def";
    std::filesystem::path after_def = case_dir / "after.def";
    std::filesystem::path step_json = case_dir / "repair.step.json";

    require(dmInst->saveDef(before_def.string()), "save baseline DEF");

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    MJI.repairAntenna({{"-step_json", step_json.string()}});
    MJI.destroyMJ();

    require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
    require(readText(before_def) == readText(after_def), "unrepaired congested DEF must remain unchanged");

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    const auto& ar = payload.at("antenna_repair");
    require(ar.at("diodes_inserted").get<int>() == 0, "expected 0 diodes when row is full");
    require(ar.at("nets_unrepaired").get<int>() == 1, "expected 1 unrepaired net when row is full");
    require(ar.at("violations_before").get<int>() == ar.at("violations_after").get<int>(),
            "expected identical violation count");
  }

  void testM1ViolationDoesNotBridgeViaM2()
  {
    // GATE_M1 violates on M1, and every free site lies left of a full-height M1 wall while M2 is empty.
    // Physically, climbing M1->V12->M2 cannot repair an M1 antenna violation, so M2 bridging must be forbidden.
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN m1_no_m2_bridge ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 12 BY 1 STEP 2000 0 ;
COMPONENTS 9 ;
- u_block_0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_6 FILL_CELL + PLACED ( 14000 0 ) N ;
- u_block_7 FILL_CELL + PLACED ( 16000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 18000 0 ) N ;
- u_block_10 FILL_CELL + PLACED ( 20000 0 ) N ;
- u_block_11 FILL_CELL + PLACED ( 22000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 19000 3000 ) ( 19000 10000 ) ;
- m1_wall
  + USE SIGNAL
  + ROUTED M1 ( 16000 -50000 ) ( 16000 50000 ) ;
END NETS
END DESIGN
)";

    loadCase("m1_no_m2_bridge", def);
    std::filesystem::path case_dir = _root / "m1_no_m2_bridge";
    std::filesystem::path before_def = case_dir / "before.def";
    std::filesystem::path after_def = case_dir / "after.def";
    std::filesystem::path step_json = case_dir / "repair.step.json";

    require(dmInst->saveDef(before_def.string()), "save baseline DEF");
    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                       {"-max_iter", 2},
                       {"-search_radius_um", 30.0},
                       {"-step_json", step_json.string()}});
    MJI.destroyMJ();
    require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
    require(readText(before_def) == readText(after_def),
            "M1 violation must not bridge via M2 over a blocking M1 wall");

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    const auto& ar = payload.at("antenna_repair");
    require(ar.at("violations_before").get<int>() > 0, "expected M1 violations before");
    require(ar.at("diodes_inserted").get<int>() == 0, "expected 0 diodes when only upper-layer bridge exists");
    require(ar.at("violations_after").get<int>() == ar.at("violations_before").get<int>(),
            "expected violation count unchanged");
  }

  void testTieCellNotSelectedAsDiode()
  {
    const std::string tie_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO TIE_HIGH
  CLASS CORE TIEHIGH ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 10 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END Y
END TIE_HIGH
MACRO FAKE_OUT_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 10 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END Y
END FAKE_OUT_DIODE
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN reject_tie_cell ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 6000 ) ;
ROW ROW_0 CORE 0 0 N DO 8 BY 1 STEP 2000 0 ;
COMPONENTS 1 ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 6000 ) ;
END NETS
END DESIGN
)";
    std::filesystem::path case_dir = _root / "reject_tie";
    std::filesystem::path lcell = case_dir / "tie_cells.lef";
    std::filesystem::path ldef = case_dir / "input.def";
    std::filesystem::path before_def = case_dir / "before.def";
    std::filesystem::path after_def = case_dir / "after.def";
    std::filesystem::path step_json = case_dir / "repair.step.json";
    writeText(lcell, tie_lef);
    writeText(ldef, def);

    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
    require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read tie LEF");
    require(dmInst->readDef(ldef.string()), "read DEF");
    require(dmInst->saveDef(before_def.string()), "save baseline DEF");

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    MJI.repairAntenna({{"-step_json", step_json.string()}});
    MJI.destroyMJ();

    require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
    require(readText(before_def) == readText(after_def), "tie/output cells must not be inserted as diodes");
    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
            "expected 0 diodes when only TIEHIGH/OUTPUT cells exist");
  }

  void testWrongLayerDiodeRejected()
  {
    const std::string wrong_layer_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO DIODE_M2_ONLY
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 LAYER M2 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END DIODE_M2_ONLY
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN wrong_layer_diode ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 6000 ) ;
ROW ROW_0 CORE 0 0 N DO 8 BY 1 STEP 2000 0 ;
COMPONENTS 1 ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 6000 ) ;
END NETS
END DESIGN
)";
    std::filesystem::path case_dir = _root / "wrong_layer_diode";
    std::filesystem::path lcell = case_dir / "m2_diode.lef";
    std::filesystem::path ldef = case_dir / "input.def";
    std::filesystem::path before_def = case_dir / "before.def";
    std::filesystem::path after_def = case_dir / "after.def";
    std::filesystem::path step_json = case_dir / "repair.step.json";
    writeText(lcell, wrong_layer_lef);
    writeText(ldef, def);

    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
    require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read m2 diode LEF");
    require(dmInst->readDef(ldef.string()), "read DEF");
    require(dmInst->saveDef(before_def.string()), "save baseline DEF");

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    MJI.repairAntenna({{"-step_json", step_json.string()}});
    MJI.destroyMJ();

    require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
    require(readText(before_def) == readText(after_def), "M2-only diode must not be accepted for M1 violation");
    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
            "expected 0 diodes when diode only protects M2 while violation is M1");
  }

  void testSpecialNetRectObstacleNotDoubleShifted()
  {
    // SPECIALNETS RECT with non-zero path origin (16000 3000) is stored in iDB as absolute delta_rect
    // (15500, 2500)-(16500, 3500). Double-shifting it would leave (16000, 3000) unblocked and short VDD.
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN specialnet_rect_obs ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 12 BY 1 STEP 2000 0 ;
COMPONENTS 7 ;
- u_block_0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_6 FILL_CELL + PLACED ( 14000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 18000 0 ) N ;
END COMPONENTS
SPECIALNETS 1 ;
- VDD
  + USE POWER
  + ROUTED M1 0 ( 16000 3000 ) RECT ( -500 -500 500 500 ) ;
END SPECIALNETS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 19000 3000 ) ( 19000 10000 ) ;
END NETS
END DESIGN
)";

    loadCase("specialnet_rect", def);
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbSpecialNet* vdd = design->get_special_net_list()->find_net("VDD");
    require(vdd && !vdd->get_wire_list()->get_wire_list().empty(), "expected VDD special net");
    idb::IdbSpecialWireSegment* vdd_seg = vdd->get_wire_list()->get_wire_list()[0]->get_segment_list()[0];
    require(vdd_seg && vdd_seg->is_rect() && vdd_seg->get_delta_rect(), "expected VDD SPECIALNETS RECT segment");
    const idb::IdbRect vdd_rect = *vdd_seg->get_delta_rect();
    require(vdd_rect.get_low_x() == 15500 && vdd_rect.get_low_y() == 2500 &&
                vdd_rect.get_high_x() == 16500 && vdd_rect.get_high_y() == 3500,
            "unexpected parsed SPECIALNETS RECT bounds");

    std::filesystem::path case_dir = _root / "specialnet_rect";
    std::filesystem::path step_json = case_dir / "repair.step.json";
    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    MJI.repairAntenna({{"-max_iter", 2},
                       {"-search_radius_um", 30.0},
                       {"-step_json", step_json.string()}});
    MJI.destroyMJ();

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    const auto& ar = payload.at("antenna_repair");
    require(ar.at("violations_before").get<int>() > 0, "expected violations before");
    require(ar.at("violations_after").get<int>() == 0, "expected 0 violations after: " + payload.dump());
    require(ar.at("diodes_inserted").get<int>() == 1, "expected 1 diode inserted");

    idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
    require(viol_net != nullptr, "expected viol_net");
    for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
        if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M1") {
          require(!rectsViolateSpacing(s->get_segment_rect(), vdd_rect, 1000),
                  "M1 stub violated spacing against SPECIALNETS RECT obstacle VDD");
        }
      }
    }

    verifyRoundTripClean(case_dir / "repaired.def", (case_dir / "mj_recheck").string());
  }

  void testDiodePGPinShortRejected()
  {
    const std::string pg_diode_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
  PIN VSS
    DIRECTION INOUT ;
    USE GROUND ;
    SHAPE ABUTMENT ;
    PORT
      LAYER M1 ;
        RECT 0 0.5 2 1.5 ;
    END
  END VSS
END ANTENNA_DIODE
END LIBRARY
)";

    {
      const std::string def_all_blocked = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN pg_pin_short_blocked ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 8 BY 1 STEP 2000 0 ;
COMPONENTS 1 ;
- u_gate GATE_M1 + PLACED ( 14000 0 ) N ;
END COMPONENTS
SPECIALNETS 1 ;
- VSS + USE GROUND ;
END SPECIALNETS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 15000 3000 ) ( 15000 10000 ) ;
- blocker
  + USE SIGNAL
  + ROUTED M1 ( 500 1000 ) ( 15500 1000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "pg_pin_short_blocked";
      std::filesystem::path lcell = case_dir / "pg_cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(lcell, pg_diode_lef);
      writeText(ldef, def_all_blocked);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read PG diode LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"}, {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "diode whose VSS PG pin shorts foreign signal wire must be rejected");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when all sites short candidate PG pin: " + payload.dump());
    }

    {
      const std::string def_skip_short_sites = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN pg_pin_short_skip ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 8 BY 1 STEP 2000 0 ;
COMPONENTS 2 ;
- u_block_0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_gate GATE_M1 + PLACED ( 2000 0 ) N ;
END COMPONENTS
SPECIALNETS 1 ;
- VSS + USE GROUND ;
END SPECIALNETS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 3000 3000 ) ( 3000 10000 ) ;
- blocker
  + USE SIGNAL
  + ROUTED M1 ( 4500 1000 ) ( 7500 1000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "pg_pin_short_skip";
      std::filesystem::path lcell = case_dir / "pg_cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(lcell, pg_diode_lef);
      writeText(ldef, def_skip_short_sites);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read PG diode LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1, "expected 1 clean diode after skipping PG-short sites");
      require(ar.at("violations_after").get<int>() == 0, "expected 0 violations after skipping PG-short sites");

      idb::IdbDesign* design = dmInst->get_idb_design();
      const idb::IdbRect blocker_rect(4000, 500, 8000, 1500);
      int diode_count = 0;
      for (idb::IdbInstance* inst : design->get_instance_list()->get_instance_list()) {
        if (inst->get_cell_master() && inst->get_cell_master()->get_name() == "ANTENNA_DIODE") {
          ++diode_count;
          const int32_t x = inst->get_coordinate()->get_x();
          require(x >= 10000, "inserted diode must skip PG-shorting sites (x=" + std::to_string(x) + ")");
          const idb::IdbRect vss_rect(x, 500, x + 2000, 1500);
          require(!rectsViolateSpacing(vss_rect, blocker_rect, 1000),
                  "inserted diode VSS pin violated spacing against foreign signal wire");
        }
      }
      require(diode_count == 1, "expected exactly 1 ANTENNA_DIODE instance");

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "reload tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "reload PG diode LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after round-trip: " + recheck.dump());
    }
  }

  void testCoalescedPRLSpacingReroutesCleanly()
  {
    const std::string prl_tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  SPACINGTABLE
    PARALLELRUNLENGTH 0 3
    WIDTH 0 1 2.5 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 100 ;
  ANTENNACUMDIFFAREARATIO 100 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";

    {
      const std::string def_reroute = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN coalesced_prl_reroute ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 13000 3000 ) ( 13000 10000 ) ;
- foreign_net
  + USE SIGNAL
  + ROUTED M1 ( 500 1000 ) ( 10000 1000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "coalesced_prl_reroute";
      std::filesystem::path ltech = case_dir / "prl_tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, prl_tech_lef);
      writeText(ldef, def_reroute);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read PRL tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1, "expected 1 diode on PRL detour: " + payload.dump());
      require(ar.at("violations_after").get<int>() == 0, "expected 0 violations on PRL detour: " + payload.dump());

      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
      require(viol_net != nullptr, "expected viol_net");
      const idb::IdbRect foreign_rect(0, 500, 10500, 1500);
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M1") {
            const idb::IdbRect r = s->get_segment_rect();
            const int32_t x_prl = std::min(r.get_high_x(), foreign_rect.get_high_x()) -
                                std::max(r.get_low_x(), foreign_rect.get_low_x());
            const int32_t y_prl = std::min(r.get_high_y(), foreign_rect.get_high_y()) -
                                std::max(r.get_low_y(), foreign_rect.get_low_y());
            const int32_t req = (std::max(x_prl, y_prl) > 3000) ? 2500 : 1000;
            require(!rectsViolateSpacing(r, foreign_rect, req),
                    "M1 segment violated PRL spacing against foreign_net");
          }
        }
      }

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload PRL tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after PRL round-trip: " + recheck.dump());
    }

    {
      const std::string def_prl_corridor_blocked = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN coalesced_prl_blocked ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 7000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 3 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 13000 3000 ) ( 13000 6000 ) ;
- foreign_low
  + USE SIGNAL
  + ROUTED M1 ( 500 1000 ) ( 10000 1000 ) ;
- foreign_high
  + USE SIGNAL
  + ROUTED M1 ( 500 5000 ) ( 10000 5000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "coalesced_prl_blocked";
      std::filesystem::path ltech = case_dir / "prl_tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, prl_tech_lef);
      writeText(ldef, def_prl_corridor_blocked);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read PRL tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "corridor violating PRL spacing must not accept illegal stub");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when PRL corridor has insufficient spacing: " + payload.dump());
    }
  }

  void testRepairRoutingBoundedByDieArea()
  {
    {
      const std::string def_wall_in_die = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN diearea_route_bound ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 12 BY 1 STEP 2000 0 ;
COMPONENTS 9 ;
- u_block_0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_6 FILL_CELL + PLACED ( 14000 0 ) N ;
- u_block_7 FILL_CELL + PLACED ( 16000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 18000 0 ) N ;
- u_block_10 FILL_CELL + PLACED ( 20000 0 ) N ;
- u_block_11 FILL_CELL + PLACED ( 22000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 19000 3000 ) ( 19000 10000 ) ;
- m1_wall
  + USE SIGNAL
  + ROUTED M1 ( 16000 500 ) ( 16000 11500 ) ;
END NETS
END DESIGN
)";
      loadCase("diearea_route_bound", def_wall_in_die);
      std::filesystem::path case_dir = _root / "diearea_route_bound";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "routing must stay inside DIEAREA and not detour outside chip boundary");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when only detour escapes DIEAREA: " + payload.dump());
    }

    {
      const std::string def_site_outside_die = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN diearea_cell_bound ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 8000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 4 ;
- u_block_0 FILL_CELL + PLACED ( 0 0 ) N ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 6000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 7000 3000 ) ( 7000 10000 ) ;
END NETS
END DESIGN
)";
      loadCase("diearea_cell_bound", def_site_outside_die);
      std::filesystem::path case_dir = _root / "diearea_cell_bound";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "candidate diode sites extending outside DIEAREA must be rejected");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when free sites lie outside DIEAREA: " + payload.dump());
    }
  }

  void testSegmentedTargetWirePRLInvariant()
  {
    const std::string prl_tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACINGTABLE
    PARALLELRUNLENGTH 0 8
    WIDTH 0 1 2.5 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";

    auto makeSegmentedWire = [](int32_t x_start, int32_t x_end, int32_t y, int segs) {
      std::ostringstream ss;
      const int32_t step = (x_end - x_start) / segs;
      require(step > 0 && (x_end - x_start) == step * segs, "wire length must divide evenly by segment count");
      for (int i = 0; i < segs; ++i) {
        const int32_t sx = x_start + i * step;
        const int32_t ex = sx + step;
        if (i == 0) {
          ss << "  + ROUTED M1 ( " << sx << " " << y << " ) ( " << ex << " " << y << " )\n";
        } else {
          ss << "    NEW M1 ( " << sx << " " << y << " ) ( " << ex << " " << y << " )\n";
        }
      }
      return ss.str();
    };

    using RectTuple = std::tuple<int32_t, int32_t, int32_t, int32_t>;
    std::vector<RectTuple> baseline_added_stubs;
    const std::vector<std::pair<int, int>> seg_cases = {
        {1, 1}, {200, 1}, {400, 1}, {1000, 1}, {1000, 500},
    };

    for (const auto& [target_segs, foreign_segs] : seg_cases) {
      std::ostringstream def_ss;
      def_ss << "VERSION 5.8 ;\n"
             << "DIVIDERCHAR \"/\" ;\n"
             << "BUSBITCHARS \"[]\" ;\n"
             << "DESIGN prl_seg_" << target_segs << "_" << foreign_segs << " ;\n"
             << "UNITS DISTANCE MICRONS 1000 ;\n"
             << "DIEAREA ( 0 0 ) ( 14000 12000 ) ;\n"
             << "ROW ROW_0 CORE 0 0 N DO 5 BY 1 STEP 2000 0 ;\n"
             << "COMPONENTS 4 ;\n"
             << "- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;\n"
             << "- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;\n"
             << "- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;\n"
             << "- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;\n"
             << "END COMPONENTS\n"
             << "NETS 2 ;\n"
             << "- viol_net ( u_gate A )\n"
             << "  + USE SIGNAL\n"
             << makeSegmentedWire(5000, 9000, 3000, target_segs) << "  ;\n"
             << "- foreign_net\n"
             << "  + USE SIGNAL\n"
             << makeSegmentedWire(500, 9000, 1000, foreign_segs) << "  ;\n"
             << "END NETS\n"
             << "END DESIGN\n";

      const std::string tag =
          "prl_seg_" + std::to_string(target_segs) + "_" + std::to_string(foreign_segs);
      std::filesystem::path case_dir = _root / tag;
      std::filesystem::path ltech = case_dir / "prl_tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, prl_tech_lef);
      writeText(ldef, def_ss.str());

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read PRL tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF for " + tag);

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1,
              "expected 1 diode inserted for " + tag + ": " + payload.dump());
      require(ar.at("violations_after").get<int>() == 0,
              "expected 0 violations after for " + tag + ": " + payload.dump());

      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
      require(viol_net != nullptr, "expected viol_net for " + tag);

      const idb::IdbRect foreign_rect(0, 500, 9500, 1500);
      std::vector<RectTuple> added_stubs;
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M1") {
            const idb::IdbRect r = s->get_segment_rect();
            const int32_t lx = std::min(r.get_low_x(), r.get_high_x());
            const int32_t ly = std::min(r.get_low_y(), r.get_high_y());
            const int32_t hx = std::max(r.get_low_x(), r.get_high_x());
            const int32_t hy = std::max(r.get_low_y(), r.get_high_y());
            const bool inside_initial = (lx >= 4500 && hx <= 9500 && ly >= 2500 && hy <= 3500);
            if (!inside_initial) {
              added_stubs.emplace_back(lx, ly, hx, hy);
              require(!(ly == 2500 && hy == 3500 && lx < 4000),
                      "illegal direct y=3000 stub committed for " + tag);
              const int32_t x_prl = std::min(hx, foreign_rect.get_high_x()) - std::max(lx, foreign_rect.get_low_x());
              if (x_prl > 1000) {
                require(ly >= 4000, "horizontal detour must stay at y>=4000 (2.5 um PRL clearance) for " + tag);
              }
            }
          }
        }
      }
      std::sort(added_stubs.begin(), added_stubs.end());
      require(added_stubs.size() >= 3, "expected 3-segment detour around y=4500 for " + tag);
      if (baseline_added_stubs.empty()) {
        baseline_added_stubs = added_stubs;
      } else {
        require(added_stubs == baseline_added_stubs,
                "route changed under equivalent-geometry DEF segmentation for " + tag);
      }

      require(dmInst->saveDef(out_def.string()), "save repaired DEF for " + tag);
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload PRL tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after round-trip for " + tag + ": " + recheck.dump());
    }
  }

  void testPendingDiodeSignalCutSpacingValidated()
  {
    auto makeTechLef = [](const std::string& v1_spacing_block) {
      return std::string(R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
)") + v1_spacing_block +
             R"(  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 50 ;
  ANTENNACUMDIFFAREARATIO 100 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
    };

    const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M2
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 LAYER M1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.9 3.5 ;
      LAYER V1 ;
        RECT 0.7 2.9 0.9 3.1 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";

    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN pending_diode_cut_spacing ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M2 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M2 ( 13000 3000 ) ( 13000 10000 ) ;
- m1_wall
  + USE SIGNAL
  + ROUTED M1 ( 3500 500 ) ( 3500 11500 ) ;
END NETS
END DESIGN
)";

    const idb::IdbRect diode_v1_cut(700, 2900, 900, 3100);

    for (const auto& [name, sp_str, min_req_dbu] :
         std::vector<std::tuple<std::string, std::string, int32_t>>{
             {"cut_sig_600", "  SPACING 0.6 ;\n", 600},
             {"cut_sig_300", "  SPACING 0.3 ;\n", 300},
         }) {
      std::filesystem::path case_dir = _root / name;
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef(sp_str));
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF for " + name);

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1,
              "expected 1 diode inserted for " + name + ": " + payload.dump());
      require(ar.at("violations_after").get<int>() == 0,
              "expected 0 violations after for " + name + ": " + payload.dump());

      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
      require(viol_net != nullptr, "expected viol_net");
      int new_via_cuts = 0;
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (!s->is_via()) {
            continue;
          }
          for (idb::IdbVia* via : s->get_via_list()) {
            idb::IdbLayerShape csh = via->get_cut_layer_shape();
            for (idb::IdbRect* r : csh.get_rect_list()) {
              if (r) {
                ++new_via_cuts;
                require(!rectsViolateSpacing(*r, diode_v1_cut, min_req_dbu),
                        "new V12 cut violated spacing against diode signal-pin cut in " + name);
              }
            }
          }
        }
      }
      require(new_via_cuts >= 1, "expected at least 1 new V12 cut in " + name);

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after round-trip for " + name);
    }

    {
      std::filesystem::path case_dir = _root / "cut_sig_blocked_20um";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef("  SPACING 20 ;\n"));
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "20 um V1 spacing conflict against diode signal cut must reject insertion");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when diode signal cut blocks all V12 sites: " + payload.dump());
    }
  }

  void testPendingMultiNewViaCutSpacingValidated()
  {
    auto makeTechLef = [](const std::string& v1_spacing_block) {
      return std::string(R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 50 ;
  ANTENNACUMDIFFAREARATIO 100 ;
END M1
LAYER V1
  TYPE CUT ;
)") + v1_spacing_block +
             R"(  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 50 ;
  ANTENNACUMDIFFAREARATIO 100 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
    };

    const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M2
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 LAYER M2 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";

    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN pending_multi_via_cut ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 11 BY 1 STEP 2000 0 ;
COMPONENTS 10 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_block_6 FILL_CELL + PLACED ( 12000 0 ) N ;
- u_block_7 FILL_CELL + PLACED ( 14000 0 ) N ;
- u_block_8 FILL_CELL + PLACED ( 16000 0 ) N ;
- u_block_9 FILL_CELL + PLACED ( 18000 0 ) N ;
- u_gate GATE_M2 + PLACED ( 20000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M2 ( 21000 3000 ) ( 21000 10000 ) ;
- m2_wall
  + USE SIGNAL
  + ROUTED M2 ( 10000 500 ) ( 10000 11500 ) ;
END NETS
END DESIGN
)";

    {
      std::filesystem::path case_dir = _root / "multi_via_blocked_25um";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef("  SPACING 25 ;\n"));
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 40.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "two new V12 vias violating 25 um cut spacing must be rejected");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when two pending new V12 cuts cannot satisfy 25 um spacing");
    }

    for (const auto& [name, sp_str] : std::vector<std::pair<std::string, std::string>>{
             {"multi_via_pass_10um", "  SPACING 10 ;\n"},
             {"multi_via_pass_samenet_10um", "  SPACING 25 ;\n  SPACING 10 SAMENET ;\n"},
         }) {
      std::filesystem::path case_dir = _root / name;
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef(sp_str));
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF for " + name);

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 40.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1,
              "expected 1 diode inserted for " + name + ": " + payload.dump());
      require(ar.at("violations_after").get<int>() == 0,
              "expected 0 violations after for " + name + ": " + payload.dump());

      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
      require(viol_net != nullptr, "expected viol_net");
      std::vector<idb::IdbRect> cuts;
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (!s->is_via()) {
            continue;
          }
          for (idb::IdbVia* via : s->get_via_list()) {
            idb::IdbLayerShape csh = via->get_cut_layer_shape();
            for (idb::IdbRect* r : csh.get_rect_list()) {
              if (r) {
                cuts.push_back(*r);
              }
            }
          }
        }
      }
      require(cuts.size() >= 2, "expected at least 2 V12 cuts for M2->M1->M2 bridge in " + name);
      for (size_t i = 0; i < cuts.size(); ++i) {
        for (size_t j = i + 1; j < cuts.size(); ++j) {
          require(!rectsViolateSpacing(cuts[i], cuts[j], 10000),
                  "pair of new V12 cuts violated 10 um spacing in " + name);
        }
      }

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after round-trip for " + name);
    }
  }

  void testMultiDiodeRepairProgress()
  {
    const std::string tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 2 ;
  ANTENNADIFFAREARATIO 2 ;
  ANTENNACUMDIFFAREARATIO 2 ;
  ANTENNAGATEPLUSDIFF 1 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";

    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN multi_diode_progress ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 24000 ) ;
ROW ROW_0 CORE 0 0 N DO 12 BY 1 STEP 2000 0 ;
COMPONENTS 1 ;
- u_gate GATE_M1 + PLACED ( 18000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 19000 3000 ) ( 19000 15000 ) ;
END NETS
END DESIGN
)";

    {
      std::filesystem::path case_dir = _root / "multi_diode_two_needed";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 10},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("violations_before").get<int>() == 2,
              "expected 2 initial M1 violations (PAR+CAR): " + payload.dump());
      require(ar.at("violations_after").get<int>() == 0,
              "expected 0 violations after inserting two diodes: " + payload.dump());
      require(ar.at("diodes_inserted").get<int>() == 2,
              "expected 2 diodes inserted across multi-diode repair: " + payload.dump());
      require(ar.at("nets_repaired").get<int>() == 1, "expected 1 repaired net");
      require(ar.at("nets_unrepaired").get<int>() == 0, "expected 0 unrepaired nets");

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after round-trip: " + recheck.dump());
    }

    {
      const std::string weak_cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO WEAK_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 0.01 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END WEAK_DIODE
END LIBRARY
)";
      std::filesystem::path case_dir = _root / "multi_diode_non_improving_rollback";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "weak_cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(lcell, weak_cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read weak diode LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "WEAK_DIODE"},
                         {"-max_iter", 5},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "non-improving weak diode must still roll back");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when candidate increases antenna ratio: " + payload.dump());
      require(ar.at("violations_after").get<int>() == ar.at("violations_before").get<int>(),
              "expected violation count unchanged for weak diode");
    }
  }

  void testManufacturingGridAlignment()
  {
    const std::string grid_tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.01 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";

    const std::string off_midpoint_cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.51 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";

    {
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN mfg_grid_off_midpoint ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 13000 3000 ) ( 13000 10000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "mfg_grid_off_midpoint";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, grid_tech_lef);
      writeText(lcell, off_midpoint_cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 3},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1,
              "expected 1 diode on mfg grid repair: " + payload.dump());
      require(ar.at("violations_after").get<int>() < ar.at("violations_before").get<int>(),
              "expected violation reduction on mfg grid repair: " + payload.dump());

      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
      require(viol_net != nullptr, "expected viol_net");
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M1") {
            const idb::IdbRect r = s->get_segment_rect();
            require(r.get_low_x() % 10 == 0 && r.get_low_y() % 10 == 0 &&
                        r.get_high_x() % 10 == 0 && r.get_high_y() % 10 == 0,
                    "M1 wire edge violated 10-DBU MANUFACTURINGGRID");
          }
        }
      }

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == ar.at("violations_after").get<int>(),
              "round-trip violation count mismatch");
    }

    {
      const std::string odd_width_tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.01 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 0.15 ;
  SPACING 0.2 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 0.5 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 0.2 ;
  SPACING 0.2 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.15 -0.15 0.15 0.15 ;
  LAYER V1 ;
    RECT -0.05 -0.05 0.05 0.05 ;
  LAYER M2 ;
    RECT -0.15 -0.15 0.15 0.15 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN mfg_grid_odd_width ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 13005 3005 ) ( 13005 10005 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "mfg_grid_odd_width";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, odd_width_tech_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read odd-width tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1,
              "expected 1 diode for odd half-width on grid: " + payload.dump());
      require(ar.at("violations_after").get<int>() == 0,
              "expected 0 violations after for odd half-width: " + payload.dump());

      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
      require(viol_net != nullptr, "expected viol_net");
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M1") {
            const idb::IdbRect r = s->get_segment_rect();
            require(r.get_low_x() % 10 == 0 && r.get_low_y() % 10 == 0 &&
                        r.get_high_x() % 10 == 0 && r.get_high_y() % 10 == 0,
                    "odd-width M1 wire edge violated 10-DBU MANUFACTURINGGRID");
          }
        }
      }

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload odd-width tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after odd-width round-trip");
    }

    {
      const std::string unaligned_grid_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.3 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN mfg_grid_fail_closed ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 13000 3000 ) ( 13000 10000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "mfg_grid_fail_closed";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, unaligned_grid_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read unaligned grid LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "impossible manufacturing grid alignment must fail closed without DEF changes");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when layer width cannot align to manufacturing grid");
    }
  }

  void testIntermediateViaStackMinArea()
  {
    auto make3LayerTechLef = [](const std::string& m2_area_line) {
      return std::string(R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
)") + m2_area_line +
             R"(  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
LAYER V2
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V2
LAYER M3
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M3
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
VIA V23 DEFAULT
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V2 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M3 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V23
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
    };

    const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M3
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M3 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M3
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";

    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN via_stack_min_area ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M3 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M3 ( 13000 3000 ) ( 13000 10000 ) ;
END NETS
END DESIGN
)";

    for (const auto& [name, area_line, min_area_dbu2] :
         std::vector<std::tuple<std::string, std::string, int64_t>>{
             {"via_stack_min_area_2um2", "  AREA 2 ;\n", 2000000},
             {"via_stack_min_area_ctrl_05um2", "  AREA 0.5 ;\n", 500000},
         }) {
      std::filesystem::path case_dir = _root / name;
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, make3LayerTechLef(area_line));
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read 3-layer tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF for " + name);

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      const auto& ar = payload.at("antenna_repair");
      require(ar.at("diodes_inserted").get<int>() == 1,
              "expected 1 diode inserted for " + name + ": " + payload.dump());
      require(ar.at("violations_after").get<int>() == 0,
              "expected 0 violations after for " + name + ": " + payload.dump());

      namespace gtl = boost::polygon;
      gtl::polygon_90_set_data<int32_t> m2_set;
      idb::IdbDesign* design = dmInst->get_idb_design();
      idb::IdbNet* viol_net = design->get_net_list()->find_net("viol_net");
      require(viol_net != nullptr, "expected viol_net");
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_wire() && s->get_layer() && s->get_layer()->get_name() == "M2") {
            const idb::IdbRect r = s->get_segment_rect();
            m2_set.insert(gtl::rectangle_data<int32_t>(
                std::min(r.get_low_x(), r.get_high_x()), std::min(r.get_low_y(), r.get_high_y()),
                std::max(r.get_low_x(), r.get_high_x()), std::max(r.get_low_y(), r.get_high_y())));
          } else if (s->is_via()) {
            for (idb::IdbVia* via : s->get_via_list()) {
              for (idb::IdbLayerShape sh :
                   std::vector<idb::IdbLayerShape>{via->get_bottom_layer_shape(), via->get_top_layer_shape()}) {
                if (sh.get_layer() && sh.get_layer()->get_name() == "M2") {
                  for (idb::IdbRect* r : sh.get_rect_list()) {
                    if (r) {
                      m2_set.insert(gtl::rectangle_data<int32_t>(
                          std::min(r->get_low_x(), r->get_high_x()), std::min(r->get_low_y(), r->get_high_y()),
                          std::max(r->get_low_x(), r->get_high_x()), std::max(r->get_low_y(), r->get_high_y())));
                    }
                  }
                }
              }
            }
          }
        }
      }
      std::vector<gtl::polygon_90_with_holes_data<int32_t>> m2_polys;
      m2_set.get_polygons(m2_polys);
      require(!m2_polys.empty(), "expected M2 polygons in via stack for " + name);
      for (const auto& poly : m2_polys) {
        const int64_t area_dbu2 = static_cast<int64_t>(gtl::area(poly));
        require(area_dbu2 >= min_area_dbu2,
                "M2 polygon union area below minimum LEF AREA for " + name);
      }

      require(dmInst->saveDef(out_def.string()), "save repaired DEF");
      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "reload tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "reload cell LEF");
      require(dmInst->readDef(out_def.string()), "reload repaired DEF");
      std::filesystem::path recheck_json = case_dir / "recheck.json";
      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_recheck").string()}});
      MJI.checkAntenna({{"-step_json", recheck_json.string()}});
      MJI.destroyMJ();
      nlohmann::json recheck = nlohmann::json::parse(readText(recheck_json));
      require(recheck.at("antenna_check").at("violation_count").get<int>() == 0,
              "expected 0 violations after round-trip for " + name);
    }

    {
      std::filesystem::path case_dir = _root / "via_stack_min_area_impossible_500um2";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, make3LayerTechLef("  AREA 500 ;\n"));
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post-repair DEF");
      require(readText(before_def) == readText(after_def),
              "unpatchable 500 um^2 M2 AREA requirement must fail closed without modifying DEF");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes when M2 AREA cannot be met inside DIEAREA");
    }
  }

  void testMaxViaStackEnforcedAndStaggered()
  {
    const std::string tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  AREA 2 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
LAYER V2
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V2
LAYER M3
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M3
MAXVIASTACK 1 RANGE M1 M3 ;
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
VIA V23 DEFAULT
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V2 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M3 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V23
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
    const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M3
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M3 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M3
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 10 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";

    {
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN max_via_stack_stagger ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M3 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M3 ( 13000 3000 ) ( 13000 10000 ) ;
- m1_wall
  + USE SIGNAL
  + ROUTED M1 ( 4000 500 ) ( 4000 11500 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "max_via_stack_stagger";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      require(MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                                 {"-max_iter", 2},
                                 {"-search_radius_um", 40.0},
                                 {"-step_json", step_json.string()}}),
              "staggered via stack should succeed under MAXVIASTACK 1");
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 1,
              "expected 1 diode in max_via_stack_stagger");
      require(payload.at("antenna_repair").at("violations_after").get<int>() == 0,
              "expected 0 violations after max_via_stack_stagger");

      idb::IdbNet* viol_net = dmInst->get_idb_design()->get_net_list()->find_net("viol_net");
      std::vector<idb::IdbRect> v1_cuts;
      std::vector<idb::IdbRect> v2_cuts;
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (!s->is_via()) {
            continue;
          }
          for (idb::IdbVia* via : s->get_via_list()) {
            idb::IdbLayerShape csh = via->get_cut_layer_shape();
            if (!csh.get_layer()) {
              continue;
            }
            for (idb::IdbRect* r : csh.get_rect_list()) {
              if (!r) {
                continue;
              }
              if (csh.get_layer()->get_name() == "V1") {
                v1_cuts.push_back(*r);
              } else if (csh.get_layer()->get_name() == "V2") {
                v2_cuts.push_back(*r);
              }
            }
          }
        }
      }
      require(!v1_cuts.empty() && !v2_cuts.empty(), "expected both V1 and V2 cuts on staggered route");
      for (const auto& c1 : v1_cuts) {
        for (const auto& c2 : v2_cuts) {
          const bool ov = c1.get_low_x() < c2.get_high_x() && c1.get_high_x() > c2.get_low_x() &&
                          c1.get_low_y() < c2.get_high_y() && c1.get_high_y() > c2.get_low_y();
          require(!ov, "MAXVIASTACK 1 forbids V1 and V2 cuts stacking directly");
        }
      }
      verifyCustomRoundTripClean(ltech, lcell, out_def, (case_dir / "mj_recheck").string());
    }

    {
      // Box M2 cleanly around (1000, 3000) so V23 cannot stagger onto a distinct location.
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN max_via_stack_blocked ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M3 + PLACED ( 12000 0 ) N ;
END COMPONENTS
BLOCKAGES 4 ;
- LAYER M2 RECT ( 2500 0 ) ( 12000 12000 ) ;
- LAYER M2 RECT ( 0 4600 ) ( 2500 12000 ) ;
- LAYER M2 RECT ( 0 0 ) ( 2500 1400 ) ;
- LAYER M2 RECT ( 0 1400 ) ( 100 4600 ) ;
END BLOCKAGES
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M3 ( 13000 3000 ) ( 13000 10000 ) ;
- m1_wall
  + USE SIGNAL
  + ROUTED M1 ( 4000 500 ) ( 4000 11500 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "max_via_stack_blocked";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 40.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post DEF");
      require(readText(before_def) == readText(after_def),
              "impossible MAXVIASTACK must fail closed without modifying DEF");
    }
  }

  void testMacroSymmetryFenceAndHaloPlacement()
  {
    const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  SYMMETRY X Y ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO BLOCKER
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  SYMMETRY X Y ;
END BLOCKER
MACRO DIODE_Y_ONLY
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  SYMMETRY Y ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END DIODE_Y_ONLY
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN sym_fence_halo ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 18000 ) ;
ROW ROW_FS CORE 0 0 FS DO 6 BY 1 STEP 2000 0 ;
ROW ROW_N CORE 0 12000 N DO 6 BY 1 STEP 2000 0 ;
REGIONS 1 ;
- fence_zone ( 0 12000 ) ( 4000 18000 ) + TYPE FENCE ;
END REGIONS
COMPONENTS 2 ;
- u_halo_host BLOCKER + PLACED ( 6000 12000 ) N + HALO 2000 0 2000 0 ;
- u_gate GATE_M1 + PLACED ( 10000 0 ) FS ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 11000 3000 ) ( 11000 11000 ) ;
END NETS
END DESIGN
)";
    std::filesystem::path case_dir = _root / "sym_fence_halo";
    std::filesystem::path lcell = case_dir / "cells.lef";
    std::filesystem::path ldef = case_dir / "input.def";
    std::filesystem::path out_def = case_dir / "repaired.def";
    std::filesystem::path step_json = case_dir / "repair.step.json";
    writeText(lcell, cell_lef);
    writeText(ldef, def);

    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
    require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
    require(dmInst->readDef(ldef.string()), "read DEF");

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    require(MJI.repairAntenna({{"-diode", "DIODE_Y_ONLY"},
                               {"-max_iter", 2},
                               {"-search_radius_um", 40.0},
                               {"-step_json", step_json.string()}}),
            "repair should succeed using the unique legal N site outside FENCE and HALO");
    MJI.destroyMJ();

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 1, "expected 1 diode");
    require(payload.at("antenna_repair").at("violations_after").get<int>() == 0, "expected 0 violations after");

    idb::IdbInstance* inserted = nullptr;
    for (idb::IdbInstance* inst : dmInst->get_idb_design()->get_instance_list()->get_instance_list()) {
      if (inst && inst->get_cell_master() && inst->get_cell_master()->get_name() == "DIODE_Y_ONLY") {
        inserted = inst;
        break;
      }
    }
    require(inserted != nullptr, "expected inserted DIODE_Y_ONLY instance");
    auto* xy = inserted->get_coordinate();
    require(xy != nullptr && xy->get_x() == 10000 && xy->get_y() == 12000 &&
                inserted->get_orient() == idb::IdbOrient::kN_R0,
            "diode must avoid FS row (SYMMETRY Y), FENCE [0..4000], and HALO [4000..10000]");
    verifyCustomRoundTripClean(_tech_lef, lcell, out_def, (case_dir / "mj_recheck").string());
  }

  void testEndOfLineSpacingAndCombinedSpacingTable()
  {
    {
      const std::string tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
  SPACING 2.0 ENDOFLINE 1.5 WITHIN 0.5 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
END M2
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN eol_detour ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 10000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 11000 3000 ) ( 11000 10000 ) ;
- eol_stub
  + USE SIGNAL
  + ROUTED M1 ( 5000 5000 ) ( 5000 7000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "eol_detour";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      require(MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                                 {"-max_iter", 2},
                                 {"-search_radius_um", 30.0},
                                 {"-step_json", step_json.string()}}),
              "EOL detour repair should succeed");
      MJI.destroyMJ();

      // eol_stub bottom EOL edge is at y=4500, x=[4500..5500].
      // An M1 segment at y=3000 (top edge 3500) clears broadside 0.5 (gap 1000) but violates 2.0 EOL (y in 2500..4500, x in 4000..6000).
      const idb::IdbRect eol_zone(4000, 2501, 6000, 4499);
      idb::IdbNet* viol_net = dmInst->get_idb_design()->get_net_list()->find_net("viol_net");
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_via()) {
            continue;
          }
          const idb::IdbRect sr = s->get_segment_rect();
          const bool ov = sr.get_low_x() < eol_zone.get_high_x() && sr.get_high_x() > eol_zone.get_low_x() &&
                          sr.get_low_y() < eol_zone.get_high_y() && sr.get_high_y() > eol_zone.get_low_y();
          require(!ov, "viol_net route entered 2.0 um EOL exclusion zone below eol_stub");
        }
      }
      verifyCustomRoundTripClean(ltech, _cell_lef, out_def, (case_dir / "mj_recheck").string());
    }

    {
      // Combined SPACINGTABLE + SPACING RANGE (table 0.5, range 1.5..5.0 -> 3.0) against a 2 um wide block.
      const std::string tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
  SPACING 3.0 RANGE 1.5 5.0 ;
  SPACINGTABLE
    PARALLELRUNLENGTH 0
    WIDTH 0 0.5 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
END M2
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN combined_sp_range ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 10000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 11000 3000 ) ( 11000 10000 ) ;
- wide_obs
  + USE SIGNAL
  + ROUTED M1 ( 5000 4000 ) RECT ( -1000 -1000 1000 1000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "combined_sp_range";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      require(MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                                 {"-max_iter", 2},
                                 {"-search_radius_um", 30.0},
                                 {"-step_json", step_json.string()}}),
              "combined spacing table + range detour should succeed");
      MJI.destroyMJ();

      const idb::IdbRect wide_rect(4000, 3000, 6000, 5000);
      idb::IdbNet* viol_net = dmInst->get_idb_design()->get_net_list()->find_net("viol_net");
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_via()) {
            continue;
          }
          const idb::IdbRect sr = s->get_segment_rect();
          if (sr.get_high_x() <= 1600) {
            continue;
          }
          require(!rectsViolateSpacing(sr, wide_rect, 3000),
                  "combined SPACINGTABLE + SPACING RANGE 3.0 must be enforced against wide_obs");
        }
      }
      verifyCustomRoundTripClean(ltech, _cell_lef, out_def, (case_dir / "mj_recheck").string());
    }
  }

  void testSameNetSpacingNotchAndMinEnclosedArea()
  {
    auto makeTechLef = [](const std::string& extra_m1) {
      return std::string(R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
)") + extra_m1 +
             R"(  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
END M2
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
    };

    {
      // Non-touching M1 spur of viol_net along [4000..10000] x [4500..5500] sits 1.0 um from y=3000.
      // Same-net SPACING 2.0 rejects Pass 1 near the spur and Pass 2 detours below/around it.
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN same_net_pass2 ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 12000 0 ) N ;
END COMPONENTS
BLOCKAGES 1 ;
- LAYER M2 RECT ( 0 0 ) ( 16000 12000 ) ;
END BLOCKAGES
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 13000 3000 ) ( 13000 10000 )
  NEW M1 ( 11000 5000 ) ( 13000 5000 ) ;
- obs_pin
  + USE SIGNAL
  + ROUTED M1 ( 3500 2500 ) ( 11000 2500 ) ;
END NETS
END DESIGN
)";
      // Make gap on M1: obs_pin blocks straight y=3000, spur is at y=5000 (gap to y=8000?).
      // Let's keep it crystal clean: block y=3000 via obs_pin at (3500, 3000), force M1 to y=7000
      // while viol_net has a parallel M1 spur at y=9000 (2.5 um spacing, wait: y=9000 -> [8500..9500], y=7000 -> [6500..7500] = 1.0 um < 2.0!).
      (void)def;
    }

    {
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN same_net_reroute ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 16000 14000 ) ;
ROW ROW_0 CORE 0 0 N DO 7 BY 1 STEP 2000 0 ;
COMPONENTS 6 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 12000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 13000 3000 ) ( 13000 12000 )
  NEW M1 ( 4000 5000 ) ( 10500 5000 ) ;
- wall
  + USE SIGNAL
  + ROUTED M1 ( 7000 2500 ) ( 7000 3500 ) ;
END NETS
END DESIGN
)";
      // Wait: if NEW M1 (4000 5000)-(10500 5000) isn't reachable goal-wise unless unconnected, wait: collectGoalShapesForNet puts ALL net wire segments into goals!
      // Instead, test MINENCLOSEDAREA and SPACINGNOTCHLENGTH directly with closed-fail & clean variants!
      (void)def;
    }

    {
      // Case 1: MINENCLOSEDAREA 20 encloses a 2x2 (4 um^2 < 20) hole when connecting across the mouth -> must fail closed.
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN min_enclosed_hole ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 12000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 5 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
BLOCKAGES 2 ;
- LAYER M2 RECT ( 0 0 ) ( 12000 12000 ) ;
- LAYER M1 RECT ( 0 0 ) ( 2000 12000 ) ;
END BLOCKAGES
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 10000 ) ;
END NETS
END DESIGN
)";
      (void)def;
    }

    {
      // 1) SPACING 3.0 NOTCHLENGTH 5.0: an M1 spur of viol_net near (1000,3000) where connecting only along M1 forms a 1.0 um U-notch (<3.0) with <5.0 sides must fail closed,
      // while MINENCLOSEDAREA 20 prevents bridging a 2x2 enclosed hole on M1.
      const std::string cell_hole = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO DIODE_C
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 4 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 5.5 ;
        RECT 0.5 5.5 3.5 6.5 ;
        RECT 2.5 2.5 3.5 5.5 ;
    END
  END DIODE
END DIODE_C
END LIBRARY
)";
      // DIODE_C has an open bottom U-pin [0.5..3.5] x [2.5..6.5] with inner hole x=[1.5..2.5], y=[2.5..5.5] (area ~3-6 um^2).
      // Any horizontal M1 wire connecting across [1.5..2.5] closes a < 20 um^2 hole!
      const std::string def_hole = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN min_enclosed_fail ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 10000 0 ) N ;
END COMPONENTS
BLOCKAGES 2 ;
- LAYER M2 RECT ( 0 0 ) ( 14000 12000 ) ;
- LAYER M1 RECT ( 0 3800 ) ( 14000 12000 ) ;
END BLOCKAGES
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 11000 3000 ) ( 11000 10000 ) ;
END NETS
END DESIGN
)";
      // Also block below y=2200 so DIODE_C pin legs at y=3000 MUST join horizontally and close the enclosed hole!
      const std::string def_hole_forced = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN min_enclosed_forced ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_block_5 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 10000 0 ) N ;
END COMPONENTS
BLOCKAGES 3 ;
- LAYER M2 RECT ( 0 0 ) ( 14000 12000 ) ;
- LAYER M1 RECT ( 0 3800 ) ( 10000 12000 ) ;
- LAYER M1 RECT ( 0 0 ) ( 10000 2200 ) ;
END BLOCKAGES
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 11000 3000 ) ( 11000 10000 ) ;
END NETS
END DESIGN
)";
      (void)def_hole;
      std::filesystem::path case_dir = _root / "min_enclosed_forced";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef("  SPACING 0.5 ;\n  MINENCLOSEDAREA 20 ;\n"));
      writeText(lcell, cell_hole);
      writeText(ldef, def_hole_forced);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "DIODE_C"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post DEF");
      require(readText(before_def) == readText(after_def),
              "route closing a sub-20um^2 MINENCLOSEDAREA hole must fail closed");
    }

    {
      // 2) Same-net SPACING + NOTCHLENGTH: check narrow U-notch (width 1.0 < 3.0, side < 5.0) is rejected.
      const std::string cell_notch = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO DIODE_U
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 4 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 4.5 ;
    END
  END DIODE
END DIODE_U
END LIBRARY
)";
      // viol_net M1 sits at x=[2500..3500], y=[2500..4500] (forming a 1.0 um air notch [1500..2500] of depth 1.0 um < 5.0 um once joined at bottom!),
      // Wait: joining at y=3000 joins the RIGHT leg! What creates a notch on M1?
      // Place M1 wire [1500..4000] joining two upward teeth [500..1500] x [2500..4500] and [2500..3500] x [2500..4500]: gap is [1500..2500] (w=1 < 3) and tooth depth 1.0 < 5!
      const std::string def_notch = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN notch_len_fail ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 4 ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
BLOCKAGES 3 ;
- LAYER M2 RECT ( 0 0 ) ( 14000 12000 ) ;
- LAYER M1 RECT ( 0 3800 ) ( 8000 12000 ) ;
- LAYER M1 RECT ( 0 0 ) ( 8000 2200 ) ;
END BLOCKAGES
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 10000 )
  NEW M1 ( 3000 3000 ) ( 9000 3000 )
  NEW M1 ( 3000 3000 ) ( 3000 4000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "notch_len_fail";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef("  SPACING 3.0 NOTCHLENGTH 5.0 ;\n  SPACING 0.5 ;\n"));
      writeText(lcell, cell_notch);
      writeText(ldef, def_notch);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "DIODE_U"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post DEF");
      require(readText(before_def) == readText(after_def),
              "1.0 um U-notch with depth < 5.0 um must be rejected under SPACING 3.0 NOTCHLENGTH 5.0");
    }

    {
      // 3) Verify clean non-touching same-net spacing Pass 2 reroute:
      // viol_net has trunk at x=9000 y=[3000..11500] and a same-net spur at y=8500 x=[7500..9000].
      // Foreign net m1_blk at (4000, 2000..4000) forces Pass 1 above y=7000 (within 0.5 um < 2.0 of spur),
      // so same-net conflict collection forces Pass 2 to detour to y=11500 (>= 2.0 um clear of spur).
      const std::string tech_reroute = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 2.0 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 50 ;
  ANTENNACUMDIFFAREARATIO 60 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
END M2
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
      const std::string def_reroute = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN same_net_reroute_clean ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 13000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 10000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 11500 )
  NEW M1 ( 7500 8500 ) ( 9000 8500 ) ;
- m1_blk
  + USE SIGNAL
  + ROUTED M1 ( 4000 2000 ) ( 4000 4000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "same_net_reroute_clean";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_reroute);
      writeText(ldef, def_reroute);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      require(MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                                 {"-max_iter", 2},
                                 {"-search_radius_um", 30.0},
                                 {"-step_json", step_json.string()}}),
              "same-net spacing detour should succeed");
      MJI.destroyMJ();
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 1,
              "expected 1 diode in same_net_reroute_clean");
      require(payload.at("antenna_repair").at("violations_after").get<int>() == 0,
              "expected 0 violations after same_net_reroute_clean");
      verifyCustomRoundTripClean(ltech, _cell_lef, out_def, (case_dir / "mj_recheck").string());
    }
  }

  void testMaxWidthMinStepAndWireExtension()
  {
    auto makeTechLef = [](const std::string& extra_m1) {
      return std::string(R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
)") + extra_m1 +
             R"(  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 0.5 ;
END M2
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
    };

    {
      // MAXWIDTH 1.5: overlapping DIODE pin [0.5..1.5] x [2.5..4.0] (1.5 wide) with an offset M1 bar creating a > 1.5x1.5 square fails closed.
      const std::string cell_wide = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO DIODE_WIDE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 4.0 ;
    END
  END DIODE
END DIODE_WIDE
END LIBRARY
)";
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN maxwidth_fail ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 12000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 5 BY 1 STEP 2000 0 ;
COMPONENTS 4 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
BLOCKAGES 3 ;
- LAYER M2 RECT ( 0 0 ) ( 12000 12000 ) ;
- LAYER M1 RECT ( 0 4300 ) ( 8000 12000 ) ;
- LAYER M1 RECT ( 0 0 ) ( 8000 2200 ) ;
END BLOCKAGES
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 10000 )
  NEW M1 ( 1500 3600 ) RECT ( -500 -600 1500 1000 ) ;
END NETS
END DESIGN
)";
      // Merged shape around (1500, 3000) covers >= 2.0x2.0 > MAXWIDTH 1.5 -> must fail closed!
      std::filesystem::path case_dir = _root / "maxwidth_fail";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef("  MAXWIDTH 1.5 ;\n"));
      writeText(lcell, cell_wide);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "DIODE_WIDE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post DEF");
      require(readText(before_def) == readText(after_def),
              "merged polygon exceeding MAXWIDTH 1.5 must fail closed");
    }

    {
      // MINSTEP 0.6: forced 0.2 um y-offset step edge between (1000,3000) and [2500..8000] x [2700..3700] fails closed.
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN minstep_fail ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 12000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 5 BY 1 STEP 2000 0 ;
COMPONENTS 4 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
BLOCKAGES 3 ;
- LAYER M2 RECT ( 0 0 ) ( 12000 12000 ) ;
- LAYER M1 RECT ( 0 4300 ) ( 8000 12000 ) ;
- LAYER M1 RECT ( 0 0 ) ( 8000 2200 ) ;
END BLOCKAGES
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 10000 )
  NEW M1 ( 3000 3200 ) RECT ( -500 -500 5000 500 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "minstep_fail";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef("  MINSTEP 0.6 ;\n"));
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post DEF");
      require(readText(before_def) == readText(after_def),
              "0.2 um jog edge must be rejected under MINSTEP 0.6");
    }

    {
      // WIREEXTENSION 0.8 on M1 (WIDTH 1.0): flush extension 800 emitted onto IdbRegularWireSegment and round-tripped cleanly.
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN wire_ext_800 ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 10000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 11000 3000 ) ( 11000 10000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "wire_ext_800";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, makeTechLef("  WIREEXTENSION 0.8 ;\n"));
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{_cell_lef.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      require(MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                                 {"-max_iter", 2},
                                 {"-search_radius_um", 30.0},
                                 {"-step_json", step_json.string()}}),
              "WIREEXTENSION repair should succeed");
      MJI.destroyMJ();

      idb::IdbNet* viol_net = dmInst->get_idb_design()->get_net_list()->find_net("viol_net");
      bool saw_flush_800 = false;
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (s->is_wire()) {
            for (auto* pt : s->get_point_list()) {
              if (auto ext = s->get_point_ext(pt); ext.has_value() && *ext == 800) {
                saw_flush_800 = true;
              }
            }
          }
        }
      }
      require(saw_flush_800, "expected flush point extension 800 matching WIREEXTENSION 0.8");
      verifyCustomRoundTripClean(ltech, _cell_lef, out_def, (case_dir / "mj_recheck").string());
    }
  }

  void testDefRoutingBlockageAndMacroObsSpacing()
  {
    const std::string tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  SPACINGTABLE
    PARALLELRUNLENGTH 0
    WIDTH 0 1.0
    WIDTH 3.5 3.0 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 50 ;
  ANTENNACUMDIFFAREARATIO 60 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
END M2
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
    const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO OBS_MACRO
  CLASS BLOCK ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  OBS
    LAYER M1 SPACING 3.5 ;
      RECT 0.5 1.0 1.5 5.0 ;
  END
END OBS_MACRO
MACRO DIODE_PG_OBS
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
  PIN VSS
    DIRECTION INOUT ;
    USE GROUND ;
    PORT
      LAYER M1 ;
        RECT 0.0 0.0 2.0 1.0 ;
    END
  END VSS
END DIODE_PG_OBS
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN blk_obs_sp ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 24000 18000 ) ;
ROW ROW_0 CORE 0 0 N DO 10 BY 1 STEP 2000 0 ;
COMPONENTS 7 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_obs_macro OBS_MACRO + PLACED ( 12000 0 ) N ;
- u_block_7 FILL_CELL + PLACED ( 14000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 18000 0 ) N ;
END COMPONENTS
BLOCKAGES 4 ;
- LAYER M1 + SPACING 2500 RECT ( 5000 2000 ) ( 6000 4000 ) ;
- LAYER M1 + DESIGNRULEWIDTH 4000 RECT ( 8000 2000 ) ( 9000 4000 ) ;
- LAYER M1 + SLOTS RECT ( 15000 0 ) ( 17000 18000 ) ;
- LAYER M1 + EXCEPTPGNET RECT ( 0 0 ) ( 2000 1000 ) ;
END BLOCKAGES
SPECIALNETS 1 ;
- VSS ( * VSS ) + USE GROUND ;
END SPECIALNETS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 19000 3000 ) ( 19000 12000 ) ;
END NETS
END DESIGN
)";
    std::filesystem::path case_dir = _root / "blk_obs_sp";
    std::filesystem::path ltech = case_dir / "tech.lef";
    std::filesystem::path lcell = case_dir / "cells.lef";
    std::filesystem::path ldef = case_dir / "input.def";
    std::filesystem::path out_def = case_dir / "repaired.def";
    std::filesystem::path step_json = case_dir / "repair.step.json";
    writeText(ltech, tech_lef);
    writeText(lcell, cell_lef);
    writeText(ldef, def);

    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
    require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
    require(dmInst->readDef(ldef.string()), "read DEF");

    MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
    require(MJI.repairAntenna({{"-diode", "DIODE_PG_OBS"},
                               {"-max_iter", 2},
                               {"-search_radius_um", 40.0},
                               {"-step_json", step_json.string()}}),
            "repair should navigate around blockage SPACING/DRW and macro OBS SPACING while ignoring SLOTS and PG EXCEPTPGNET");
    MJI.destroyMJ();

    nlohmann::json payload = nlohmann::json::parse(readText(step_json));
    require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 1, "expected 1 diode");
    require(payload.at("antenna_repair").at("violations_after").get<int>() == 0, "expected 0 violations");

    const idb::IdbRect sp_blk(5000, 2000, 6000, 4000);
    const idb::IdbRect drw_blk(8000, 2000, 9000, 4000);
    const idb::IdbRect obs_rect(12500, 1000, 13500, 5000);
    idb::IdbNet* viol_net = dmInst->get_idb_design()->get_net_list()->find_net("viol_net");
    for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
        if (s->is_via()) {
          continue;
        }
        const idb::IdbRect sr = s->get_segment_rect();
        if (sr.get_high_x() <= 1600) {
          continue;
        }
        require(!rectsViolateSpacing(sr, sp_blk, 2500), "M1 route violated DEF BLOCKAGE + SPACING 2500");
        require(!rectsViolateSpacing(sr, drw_blk, 3000), "M1 route violated DEF BLOCKAGE + DESIGNRULEWIDTH 4000");
        require(!rectsViolateSpacing(sr, obs_rect, 3500), "M1 route violated macro OBS SPACING 3.5");
      }
    }
    verifyCustomRoundTripClean(ltech, lcell, out_def, (case_dir / "mj_recheck").string());
  }

  void testAdjacentCutsAndGridCompatibleViaSelection()
  {
    {
      // ADJACENTCUTS 2 WITHIN 2.5 SPACING 1.5 on V1:
      // Two existing V1 cuts near (1000, 3000) spaced 1.0 um apart (< 1.5, 1 neighbor each -> valid alone).
      // Adding a third V12 cut at (1000, 3000) triggers ADJACENTCUTS >= 2 -> must fail closed.
      const std::string tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.5 ;
  SPACING 1.5 ADJACENTCUTS 2 WITHIN 2.5 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12 DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
      const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M2
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";
      // Keep only site (0, 0) open; restrict M1 corridor strictly to x=[0..2000], y=[2000..4000] via M1 wall at x=3000;
      // place two neighbor V1 cuts at (1000, 4200) and (2000, 4200): distance between them is 800 < 1500 (< 2500),
      // and new V12 at (1000, 3000) is 1000 < 1500 from (1000, 4200) and ~1118 < 2500 from (2000, 4200)!
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN adj_cuts_fail ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M2 + PLACED ( 10000 0 ) N ;
END COMPONENTS
BLOCKAGES 1 ;
- LAYER M1 RECT ( 2500 0 ) ( 4500 12000 ) ;
END BLOCKAGES
NETS 3 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M2 ( 11000 3000 ) ( 11000 10000 ) ;
- c1
  + USE SIGNAL
  + ROUTED M1 ( 1000 4200 ) V12 ;
- c2
  + USE SIGNAL
  + ROUTED M1 ( 1800 4200 ) V12 ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "adj_cuts_fail";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post DEF");
      require(readText(before_def) == readText(after_def),
              "cut violating ADJACENTCUTS 2 WITHIN 2.5 must be rejected cleanly");
    }

    {
      // MANUFACTURINGGRID 0.05 (50 DBU): V12_BAD is DEFAULT with 120 DBU cut width (not multiple of 50);
      // V12_GOOD is non-default with 200 DBU cut width (multiple of 50) -> router must choose V12_GOOD.
      const std::string tech_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
UNITS
  DATABASE MICRONS 1000 ;
END UNITS
MANUFACTURINGGRID 0.05 ;
LAYER M1
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 3 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M1
LAYER V1
  TYPE CUT ;
  SPACING 0.1 ;
  ANTENNAAREARATIO 0.05 ;
  ANTENNADIFFAREARATIO 2 ;
END V1
LAYER M2
  TYPE ROUTING ;
  DIRECTION VERTICAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
  ANTENNADIFFAREARATIO 20 ;
  ANTENNACUMDIFFAREARATIO 30 ;
END M2
VIA V12_BAD DEFAULT
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.06 -0.06 0.06 0.06 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12_BAD
VIA V12_GOOD
  LAYER M1 ;
    RECT -0.5 -0.5 0.5 0.5 ;
  LAYER V1 ;
    RECT -0.1 -0.1 0.1 0.1 ;
  LAYER M2 ;
    RECT -0.5 -0.5 0.5 0.5 ;
END V12_GOOD
SITE CORE
  CLASS CORE ;
  SIZE 2 BY 6 ;
  SYMMETRY X Y ;
END CORE
END LIBRARY
)";
      const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M2
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO ANTENNA_DIODE
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END ANTENNA_DIODE
END LIBRARY
)";
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN grid_via_select ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M2 + PLACED ( 10000 0 ) N ;
END COMPONENTS
NETS 2 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M2 ( 11000 3000 ) ( 11000 10000 ) ;
- m1_wall
  + USE SIGNAL
  + ROUTED M1 ( 3500 500 ) ( 3500 11500 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "grid_via_select";
      std::filesystem::path ltech = case_dir / "tech.lef";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path out_def = case_dir / "repaired.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(ltech, tech_lef);
      writeText(lcell, cell_lef);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{ltech.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      require(MJI.repairAntenna({{"-diode", "ANTENNA_DIODE"},
                                 {"-max_iter", 2},
                                 {"-search_radius_um", 30.0},
                                 {"-step_json", step_json.string()}}),
              "grid-compatible V12_GOOD selection should repair cleanly");
      MJI.destroyMJ();

      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 1, "expected 1 diode");
      require(payload.at("antenna_repair").at("violations_after").get<int>() == 0, "expected 0 violations after");

      idb::IdbNet* viol_net = dmInst->get_idb_design()->get_net_list()->find_net("viol_net");
      bool saw_v12_good = false;
      for (idb::IdbRegularWire* w : viol_net->get_wire_list()->get_wire_list()) {
        for (idb::IdbRegularWireSegment* s : w->get_segment_list()) {
          if (!s->is_via()) {
            continue;
          }
          for (idb::IdbVia* via : s->get_via_list()) {
            require(via->get_instance() && via->get_instance()->get_name() == "V12_GOOD",
                    "buildVias must choose grid-compatible V12_GOOD over off-grid DEFAULT V12_BAD");
            saw_v12_good = true;
          }
        }
      }
      require(saw_v12_good, "expected V12_GOOD via in grid_via_select");
      verifyCustomRoundTripClean(ltech, lcell, out_def, (case_dir / "mj_recheck").string());
    }
  }

  void testCrossLayerAntennaDiffAreaOriginAndDefRect()
  {
    {
      // Non-zero ORIGIN diode must be rejected.
      const std::string cell_bad_origin = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO DIODE_BAD_ORIGIN
  CLASS CORE ;
  ORIGIN 0.5 0.5 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END DIODE_BAD_ORIGIN
END LIBRARY
)";
      const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN nonzero_origin_reject ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 12000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 5 BY 1 STEP 2000 0 ;
COMPONENTS 4 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_gate GATE_M1 + PLACED ( 8000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M1 ( 9000 3000 ) ( 9000 10000 ) ;
END NETS
END DESIGN
)";
      std::filesystem::path case_dir = _root / "nonzero_origin_reject";
      std::filesystem::path lcell = case_dir / "cells.lef";
      std::filesystem::path ldef = case_dir / "input.def";
      std::filesystem::path before_def = case_dir / "before.def";
      std::filesystem::path after_def = case_dir / "after.def";
      std::filesystem::path step_json = case_dir / "repair.step.json";
      writeText(lcell, cell_bad_origin);
      writeText(ldef, def);

      dmInst->reset();
      require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
      require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
      require(dmInst->readDef(ldef.string()), "read DEF");
      require(dmInst->saveDef(before_def.string()), "save baseline DEF");

      MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
      MJI.repairAntenna({{"-diode", "DIODE_BAD_ORIGIN"},
                         {"-max_iter", 2},
                         {"-search_radius_um", 30.0},
                         {"-step_json", step_json.string()}});
      MJI.destroyMJ();

      require(dmInst->saveDef(after_def.string()), "save post DEF");
      require(readText(before_def) == readText(after_def),
              "diode master with non-zero ORIGIN must be rejected");
      nlohmann::json payload = nlohmann::json::parse(readText(step_json));
      require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 0,
              "expected 0 diodes inserted for non-zero ORIGIN macro");
    }

    {
      // Cross-layer ANTENNADIFFAREA 5 LAYER M1 with PORT ONLY on M2, plus DEF + RECT net segment modeling & repair.
      const std::string cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M1
MACRO GATE_M2
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END A
END GATE_M2
MACRO FILL_CELL
  CLASS CORE SPACER ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
END FILL_CELL
MACRO DIODE_X_LAYER
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 LAYER M1 ;
    PORT
      LAYER M2 ;
        RECT 0.5 2.5 1.5 3.5 ;
    END
  END DIODE
END DIODE_X_LAYER
MACRO DIODE_PORT_VIA
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 2 BY 6 ;
  SITE CORE ;
  PIN DIODE
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 5 LAYER M1 ;
    PORT
      LAYER M1 ;
        RECT 0.5 2.5 1.5 3.5 ;
        VIA 1.0 3.0 V12 ;
    END
  END DIODE
END DIODE_PORT_VIA
END LIBRARY
)";
      {
        const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN cross_layer_diff_and_def_rect ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M2 + PLACED ( 10000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M2 ( 11000 3000 ) RECT ( -500 -500 500 8500 ) ;
END NETS
END DESIGN
)";
        std::filesystem::path case_dir = _root / "cross_layer_diff_and_def_rect";
        std::filesystem::path lcell = case_dir / "cells.lef";
        std::filesystem::path ldef = case_dir / "input.def";
        std::filesystem::path out_def = case_dir / "repaired.def";
        std::filesystem::path chk_json = case_dir / "check.json";
        std::filesystem::path step_json = case_dir / "repair.step.json";
        writeText(lcell, cell_lef);
        writeText(ldef, def);

        dmInst->reset();
        require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
        require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
        require(dmInst->readDef(ldef.string()), "read DEF");

        MJI.initMJ({{"-temp_directory_path", (case_dir / "mj_chk").string()}});
        MJI.checkAntenna({{"-step_json", chk_json.string()}});
        MJI.destroyMJ();
        nlohmann::json chk = nlohmann::json::parse(readText(chk_json));
        require(chk.at("antenna_check").at("violation_count").get<int>() > 0,
                "DEF + RECT M2 patch must be modeled by AntennaChecker");

        MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
        require(MJI.repairAntenna({{"-diode", "DIODE_X_LAYER"},
                                   {"-max_iter", 2},
                                   {"-search_radius_um", 30.0},
                                   {"-step_json", step_json.string()}}),
                "DIODE_X_LAYER (ANTENNADIFFAREA LAYER M1 with M2 PORT) should repair M2 DEF + RECT violation");
        MJI.destroyMJ();
        nlohmann::json payload = nlohmann::json::parse(readText(step_json));
        require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 1, "expected 1 diode");
        require(payload.at("antenna_repair").at("violations_after").get<int>() == 0, "expected 0 violations after");
        verifyCustomRoundTripClean(_tech_lef, lcell, out_def, (case_dir / "mj_recheck").string());
      }

      {
        // Also verify DIODE_PORT_VIA (PORT containing VIA V12) connects on M2 directly and passes round-trip.
        const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN port_via_diff ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 14000 12000 ) ;
ROW ROW_0 CORE 0 0 N DO 6 BY 1 STEP 2000 0 ;
COMPONENTS 5 ;
- u_block_1 FILL_CELL + PLACED ( 2000 0 ) N ;
- u_block_2 FILL_CELL + PLACED ( 4000 0 ) N ;
- u_block_3 FILL_CELL + PLACED ( 6000 0 ) N ;
- u_block_4 FILL_CELL + PLACED ( 8000 0 ) N ;
- u_gate GATE_M2 + PLACED ( 10000 0 ) N ;
END COMPONENTS
NETS 1 ;
- viol_net ( u_gate A )
  + USE SIGNAL
  + ROUTED M2 ( 11000 3000 ) ( 11000 10000 ) ;
END NETS
END DESIGN
)";
        std::filesystem::path case_dir = _root / "port_via_diff";
        std::filesystem::path lcell = case_dir / "cells.lef";
        std::filesystem::path ldef = case_dir / "input.def";
        std::filesystem::path out_def = case_dir / "repaired.def";
        std::filesystem::path step_json = case_dir / "repair.step.json";
        writeText(lcell, cell_lef);
        writeText(ldef, def);

        dmInst->reset();
        require(dmInst->readLef(std::vector<std::string>{_tech_lef.string()}, true), "read tech LEF");
        require(dmInst->readLef(std::vector<std::string>{lcell.string()}), "read cell LEF");
        require(dmInst->readDef(ldef.string()), "read DEF");

        MJI.initMJ({{"-temp_directory_path", (case_dir / "mj").string()}});
        require(MJI.repairAntenna({{"-diode", "DIODE_PORT_VIA"},
                                   {"-max_iter", 2},
                                   {"-search_radius_um", 30.0},
                                   {"-step_json", step_json.string()}}),
                "DIODE_PORT_VIA should repair M2 violation cleanly");
        MJI.destroyMJ();
        nlohmann::json payload = nlohmann::json::parse(readText(step_json));
        require(payload.at("antenna_repair").at("diodes_inserted").get<int>() == 1, "expected 1 diode");
        require(payload.at("antenna_repair").at("violations_after").get<int>() == 0, "expected 0 violations");
        verifyCustomRoundTripClean(_tech_lef, lcell, out_def, (case_dir / "mj_recheck").string());
      }
    }
  }
};

}  // namespace

int main()
{
  try {
    AntennaRepairTest test;
    test.run();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "test_antenna_repair failed: " << ex.what() << std::endl;
    return 1;
  }
}
