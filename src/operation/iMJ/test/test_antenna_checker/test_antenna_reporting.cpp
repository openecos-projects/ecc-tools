// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#include "IdbDesign.h"
#include "IdbLayout.h"
#include "Logger.hpp"
#include "MJInterface.hpp"
#include "Utility.hpp"
#include "idm.h"

class AntennaReportingTest
{
 public:
  void run()
  {
    setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
    loadDesign();
    MJI.initMJ({{"-temp_directory_path", _output.string()}});
    testCheckOnly();
    testClockNetsAreChecked();
    testUnspecifiedUseNetIsChecked();
    testDiffusionThreshold();
    testCumRoutingPlusCut();
    testZeroDiffusionOutputIsNotProtected();
    testLayerQualifiedPinAreas();
    testMultilayerPinAreasDoNotSum();
    testSamePortCrossLayerConnectivityWithoutExternalVia();
    testPartialAreaOnLayerQualifiedPinReachedViaUpperLayer();
    testCumulativeAreaSurvivesGatelessLayer();
    testDriverOnlyLowerIslandAccumulates();
    MJI.destroyMJ();
    dmInst->reset();
  }

 private:
  std::filesystem::path _data = std::filesystem::path(__FILE__).parent_path() / "data";
  std::filesystem::path _output = std::filesystem::temp_directory_path() / ("test_imj_antenna_" + std::to_string(getpid()));

  void loadDesign(const std::string& def = "design.def.in")
  {
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{(_data / "tech.lef").string()}, true), "read technology LEF");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell.lef").string()}), "read cell LEF");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell_layer_qualified.lef").string()}), "read layer-qualified cell LEF");
    require(dmInst->readDef((_data / def).string()), "read routed DEF");
  }

  void require(bool condition, const std::string& message)
  {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  void testCheckOnly()
  {
    idb::IdbDesign* design = dmInst->get_idb_design();
    idb::IdbNet* net = design->get_net_list()->find_net("violating");
    require(dmInst->saveDef((_output / "before.def").string()), "save input design");
    MJI.checkAntenna({{"-step_json", (_output / "antennaCheck.step.json").string()}});
    require(dmInst->get_idb_design() == design, "checker replaced the design");
    require(design->get_net_list()->find_net("violating") == net, "checker replaced a net");
    require(dmInst->saveDef((_output / "after.def").string()), "save checked design");
    require(readText(_output / "before.def") == readText(_output / "after.def"), "checker modified routed DEF");

    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    require(report.find("Total Violations: 10\n") != std::string::npos, "wrong total violation count");
    require(report.find("Net: clean ") == std::string::npos, "clean component reported");
    require(report.find("Net: missing ") == std::string::npos, "missing gate data used as a gate");
    require(report.find("Net: power ") == std::string::npos, "power net checked as a signal");
    std::string step_json = readText(_output / "antennaCheck.step.json");
    require(step_json.find("\"schema_version\": 1") != std::string::npos, "missing antenna step schema");
    require(step_json.find("\"step\": \"antenna_check\"") != std::string::npos, "missing antenna step name");
    require(step_json.find("\"violation_count\": 10") != std::string::npos, "wrong structured violation count");
    require(step_json.find("\"net\": \"violating\"") != std::string::npos, "missing structured violation");
    require(report.find("Gate pin: driver/Y") == std::string::npos, "diffusion-only output reported as a gate");
    require(report.find("Gate pin: isolated/A") == std::string::npos, "disconnected gate selected");

    checkViolation(report, "M1", "PAR", 9.0, 2.0, 1.0);
    checkViolation(report, "M1", "CAR", 9.0, 3.0, 1.0);
    checkViolation(report, "M1", "PSR", 2.0, 1.0, 1.0);
    checkViolation(report, "M1", "CSR", 2.0, 1.5, 1.0);
    checkViolation(report, "V1", "CutPAR", 0.04, 0.01, 1.0);
    checkViolation(report, "V1", "CutCAR", 0.04, 0.02, 1.0);
    checkViolation(report, "M2", "PAR", 3.5, 2.0, 2.0);
    checkViolation(report, "M2", "CAR", 8.0, 7.0, 2.0);
    checkViolation(report, "M2", "PSR", 0.8, 0.5, 2.0);
    checkViolation(report, "M2", "CSR", 1.8, 1.5, 2.0);
    require(getViolation(report, "M1", "PAR").find("Metal area: 9 | Side area: 2 | Cut area: 0") != std::string::npos, "M1 area context");
    require(getViolation(report, "V1", "CutPAR").find("Cut area: 0.04") != std::string::npos, "cut area context");
    require(getViolation(report, "M2", "PAR").find("Metal area: 7 | Side area: 1.6 | Cut area: 0") != std::string::npos, "M2 area context");

    MJI.checkAntenna({});
    require(report == readText(_output / "antenna_checker/antenna_check.rpt"), "repeated check changed its result");
  }

  std::string readText(const std::filesystem::path& path)
  {
    std::ifstream* stream = MJUTIL.getInputFileStream(path.string());
    std::string text((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
    MJUTIL.closeFileStream(stream);
    return text;
  }

  void checkViolation(const std::string& report, const std::string& layer, const std::string& type, double ratio, double threshold, double gate_area)
  {
    std::string violation = getViolation(report, layer, type);
    require(imj::Utility::equalDoubleByError(getNumber(violation, "Ratio: "), ratio, 1e-9), "ratio mismatch: " + layer + " " + type);
    require(imj::Utility::equalDoubleByError(getNumber(violation, "Threshold: "), threshold, 1e-9), "threshold mismatch: " + layer + " " + type);
    require(imj::Utility::equalDoubleByError(getNumber(violation, "Gate area: "), gate_area, 1e-9), "gate area mismatch: " + layer + " " + type);
    require(violation.find("Gate pin: early/A\n") != std::string::npos, "representative gate mismatch: " + layer + " " + type);
  }

  std::string getViolation(const std::string& report, const std::string& layer, const std::string& type)
  {
    return getViolationOf(report, "violating", layer, type);
  }

  std::string getViolationOf(const std::string& report, const std::string& net, const std::string& layer, const std::string& type)
  {
    std::string header = "Net: " + net + " | Layer: " + layer + " | Type: " + type + "\n";
    size_t start = report.find(header);
    require(start != std::string::npos, "missing violation: " + net + " " + layer + " " + type);
    size_t end = report.find("\n\n", start);
    return report.substr(start, end - start);
  }

  double getNumber(const std::string& text, const std::string& key)
  {
    size_t start = text.find(key);
    require(start != std::string::npos, "missing numeric field: " + key);
    return std::stod(text.substr(start + key.size()));
  }

  void testClockNetsAreChecked()
  {
    // Same routed net, but declared USE CLOCK: it must be analysed, not silently skipped.
    idb::IdbNet* net = dmInst->get_idb_design()->get_net_list()->find_net("violating");
    net->set_connect_type(idb::IdbConnectType::kClock);
    MJI.checkAntenna({{"-step_json", (_output / "clock.step.json").string()}});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    require(report.find("Total Violations: 10\n") != std::string::npos, "clock net was not antenna-checked");
    checkViolation(report, "M1", "PAR", 9.0, 2.0, 1.0);
    require(readText(_output / "clock.step.json").find("\"signal_net_count\": 3") != std::string::npos,
            "clock net missing from the analysed net count");
    net->set_connect_type(idb::IdbConnectType::kSignal);
  }

  void testUnspecifiedUseNetIsChecked()
  {
    idb::IdbNet* net = dmInst->get_idb_design()->get_net_list()->find_net("violating");
    net->set_connect_type(idb::IdbConnectType::kNone);
    MJI.checkAntenna({{"-step_json", (_output / "unspecified.step.json").string()}});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    require(report.find("Total Violations: 10\n") != std::string::npos, "unspecified USE net was skipped");
    require(readText(_output / "unspecified.step.json").find("\"signal_net_count\": 3") != std::string::npos,
            "unspecified USE net missing from analysed net count");
    net->set_connect_type(idb::IdbConnectType::kSignal);
  }

  void testDiffusionThreshold()
  {
    idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer("M1"));
    require(layer != nullptr, "missing M1");
    layer->set_antenna_diff_area_ratio_pwl({{0.0, 3.0}, {4.0, 11.0}});
    idb::IdbPin* pin = dmInst->get_idb_design()->get_instance_list()->find_instance("driver")->get_pin("Y");
    pin->get_term()->set_antenna_diff_area(2.0);
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    checkViolation(report, "M1", "DiffPAR", 9.0, 7.0, 1.0);
    require(getViolation(report, "M1", "DiffPAR").find("Diffusion area: 2") != std::string::npos, "diffusion area context");
    require(report.find("Layer: M1 | Type: PAR\n") == std::string::npos, "plain threshold used instead of diffusion PWL");

    // Equality with the threshold is not a violation.
    layer->set_antenna_diff_area_ratio_pwl({{0.0, 5.0}, {4.0, 13.0}});
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    require(report.find("Layer: M1 | Type: DiffPAR\n") == std::string::npos, "threshold equality reported as a violation");
    require(report.find("Total Violations: 9\n") != std::string::npos, "threshold equality changed unrelated checks");
  }

  void testCumRoutingPlusCut()
  {
    // LEF/DEF 5.8 Appendix C recurrence: ANTENNACUMROUTINGPLUSCUT on a layer selects
    // the immediately preceding cut/routing cumulative value rather than summing all
    // prior metal and cut layers unconditionally.
    // 1) Enable on both V1 and M2: CutCAR(V1) = 9 + 0.04 = 9.04; CAR(M2) = (9.04 + 7) / 2 = 8.02.
    loadDesign();
    auto* m2 = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer("M2"));
    auto* v1 = dynamic_cast<idb::IdbLayerCut*>(dmInst->get_idb_layout()->get_layers()->find_layer("V1"));
    require(m2 != nullptr && v1 != nullptr, "missing M2 or V1");
    m2->set_antenna_cum_routing_plus_cut(true);
    v1->set_antenna_cum_routing_plus_cut(true);
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    checkViolation(report, "V1", "CutCAR", 9.04, 0.02, 1.0);
    checkViolation(report, "M2", "CAR", 8.02, 7.0, 2.0);

    // 2) Mixed setting: ANTENNACUMROUTINGPLUSCUT enabled on M2 only (not V1).
    // Then CutCAR(V1) = 0.04 and CAR(M2) = (0.04 + 7) / 2 = 3.52 (not 8.02).
    v1->set_antenna_cum_routing_plus_cut(false);
    m2->set_antenna_cum_routing_plus_cut(true);
    m2->set_antenna_cum_area_ratio(3.0);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    checkViolation(report, "V1", "CutCAR", 0.04, 0.02, 1.0);
    checkViolation(report, "M2", "CAR", 3.52, 3.0, 2.0);
  }

  void testZeroDiffusionOutputIsNotProtected()
  {
    // The driver's Y pin is an OUTPUT with "ANTENNADIFFAREA 0": no real diffusion, so the
    // plain threshold (2) applies, not the scalar diode threshold (5).
    loadDesign();
    idb::IdbLayerRouting* layer = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer("M1"));
    require(layer != nullptr, "missing M1");
    layer->set_antenna_diff_area_ratio(5.0);
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    checkViolation(report, "M1", "PAR", 9.0, 2.0, 1.0);
    require(report.find("Layer: M1 | Type: DiffPAR\n") == std::string::npos, "zero-diffusion output selected the diode threshold");

    // Positive control: real diffusion area switches to the diode threshold.
    idb::IdbPin* pin = dmInst->get_idb_design()->get_instance_list()->find_instance("driver")->get_pin("Y");
    pin->get_term()->set_antenna_diff_area(2.0);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    checkViolation(report, "M1", "DiffPAR", 9.0, 5.0, 1.0);
  }

  void testLayerQualifiedPinAreas()
  {
    // GATE_Q/A declares "ANTENNAGATEAREA 1 LAYER M1", "ANTENNAGATEAREA 4 LAYER M2" and
    // "ANTENNADIFFAREA 3 LAYER M2" with only an M1 PORT shape.
    // 1) When the net only routes on M1, only the M1 gate (1) applies: PAR = 9 / 1 > 2.
    //    Summing every layer (gate 5) would hide the violation, and the M2-only diffusion
    //    must not be credited at M1.
    loadDesign("design_layer_gate.def.in");
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string par = getViolationOf(report, "layered", "M1", "PAR");
    require(imj::Utility::equalDoubleByError(getNumber(par, "Ratio: "), 9.0, 1e-9), "M1 PAR ratio");
    require(imj::Utility::equalDoubleByError(getNumber(par, "Gate area: "), 1.0, 1e-9), "gate area summed across layers");
    require(par.find("Diffusion area: 0") != std::string::npos, "diffusion of another layer credited");

    // 2) LEF/DEF 5.8 Appendix C Figure C-22: when the net continues to M2 via V12, the
    //    M2-qualified gate (4) and diffusion (3) become active at M2 through internal macro
    //    connectivity even though GATE_Q/A has no exposed M2 PORT shape.
    //    M1 metal = 9 (at M1, gate = 1), M2 metal = 23 (at M2, gate = 4, diff = 3):
    //    M2 CAR = (9 + 23) / 4 = 8.0 > 7.
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN antenna_layer_gate_m2_test ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 40000 40000 ) ;
ROW ROW_0 CORE 0 0 N DO 40 BY 1 STEP 1000 0 ;
COMPONENTS 1 ;
- q GATE_Q + FIXED ( 2000 1500 ) N ;
END COMPONENTS
NETS 1 ;
- layered ( q A )
  + USE SIGNAL
  + ROUTED M1 ( 2000 2000 ) ( 10000 2000 ) V12
    NEW M2 ( 10000 2000 ) ( 10000 24000 ) ;
END NETS
END DESIGN
)";
    writeCaseFiles(_output / "layer_q_no_m2_port", readText(_data / "cell_layer_qualified.lef"), def);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string m2_car = getViolationOf(report, "layered", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(m2_car, "Gate area: "), 4.0, 1e-9),
            "M2 LAYER-qualified gate area ignored when pin had no M2 PORT shape");
    require(imj::Utility::equalDoubleByError(getNumber(m2_car, "Diffusion area: "), 3.0, 1e-9),
            "M2 LAYER-qualified diff area ignored when pin had no M2 PORT shape");
    require(imj::Utility::equalDoubleByError(getNumber(m2_car, "Ratio: "), 8.0, 1e-9),
            "unexpected M2 CAR when pin had no M2 PORT shape");

    // 3) Same M2-qualified annotations when the pin also has a higher-layer M3 PORT shape
    //    (PORTs on M1 and M3, but no M2 PORT). The M2 annotation must anchor to the
    //    already-reached M1 PORT and activate at M2, not be delayed until M3.
    const std::string m1_m3_cell_lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO GATE_Q_M1_M3
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 LAYER M1 ;
    ANTENNAGATEAREA 4 LAYER M2 ;
    ANTENNADIFFAREA 3 LAYER M2 ;
    PORT
      LAYER M1 ;
        RECT 0 0 1 1 ;
      LAYER M3 ;
        RECT 0 0 1 1 ;
    END
  END A
END GATE_Q_M1_M3
END LIBRARY
)";
    const std::string def_m1_m3 = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN antenna_layer_gate_m1_m3_test ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 40000 40000 ) ;
ROW ROW_0 CORE 0 0 N DO 40 BY 1 STEP 1000 0 ;
COMPONENTS 1 ;
- q GATE_Q_M1_M3 + FIXED ( 2000 1500 ) N ;
END COMPONENTS
NETS 1 ;
- layered ( q A )
  + USE SIGNAL
  + ROUTED M1 ( 2000 2000 ) ( 10000 2000 ) V12
    NEW M2 ( 10000 2000 ) ( 10000 24000 ) ;
END NETS
END DESIGN
)";
    const std::string extra_tech_m3 = R"(LAYER M3
  TYPE ROUTING ;
  DIRECTION HORIZONTAL ;
  PITCH 2 ;
  WIDTH 1 ;
  SPACING 1 ;
  THICKNESS 0.1 ;
  ANTENNAAREARATIO 2 ;
  ANTENNACUMAREARATIO 7 ;
END M3
)";
    writeCaseFiles(_output / "layer_q_m1_m3_port", m1_m3_cell_lef, def_m1_m3, extra_tech_m3);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    m2_car = getViolationOf(report, "layered", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(m2_car, "Gate area: "), 4.0, 1e-9),
            "M2 LAYER-qualified gate area delayed to M3 when pin had M1 and M3 PORT shapes");
    require(imj::Utility::equalDoubleByError(getNumber(m2_car, "Diffusion area: "), 3.0, 1e-9),
            "M2 LAYER-qualified diff area delayed to M3 when pin had M1 and M3 PORT shapes");
    require(imj::Utility::equalDoubleByError(getNumber(m2_car, "Ratio: "), 8.0, 1e-9),
            "unexpected M2 CAR when pin had M1 and M3 PORT shapes");

    // 4) Same GATE_Q_M1_M3 macro (single PORT containing both M1 and M3 rectangles),
    //    external route connects ONLY to the M3 PORT shape.
    //    At M1, the M1 PORT rect (area 1) is connected to the M1 gate (area 1), accumulating
    //    M1 = 1. At M3, the same-PORT M1 and M3 shapes connect, upgrading gate to M2 (4) and
    //    diffusion to M2 (3): M3 PAR = 32 / 4 = 8.0 and M3 CAR = (1 + 32) / 4 = 8.25 > 7.
    const std::string def_m3_only = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN antenna_layer_gate_m3_only_test ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 40000 40000 ) ;
ROW ROW_0 CORE 0 0 N DO 40 BY 1 STEP 1000 0 ;
COMPONENTS 1 ;
- q GATE_Q_M1_M3 + FIXED ( 2000 1500 ) N ;
END COMPONENTS
NETS 1 ;
- layered ( q A )
  + USE SIGNAL
  + ROUTED M3 ( 2000 2000 ) ( 33000 2000 ) ;
END NETS
END DESIGN
)";
    writeCaseFiles(_output / "layer_q_m3_only_port", m1_m3_cell_lef, def_m3_only, extra_tech_m3);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string m3_par = getViolationOf(report, "layered", "M3", "PAR");
    require(imj::Utility::equalDoubleByError(getNumber(m3_par, "Ratio: "), 8.0, 1e-9),
            "unexpected M3 PAR when net connected only to M3 PORT");
    std::string m3_car = getViolationOf(report, "layered", "M3", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(m3_car, "Gate area: "), 4.0, 1e-9),
            "M2 LAYER-qualified gate area missing when net connected only to M3 PORT");
    require(imj::Utility::equalDoubleByError(getNumber(m3_car, "Diffusion area: "), 3.0, 1e-9),
            "M2 LAYER-qualified diff area missing when net connected only to M3 PORT");
    require(imj::Utility::equalDoubleByError(getNumber(m3_car, "Ratio: "), 8.25, 1e-9),
            "unexpected M3 CAR when same-PORT M1 shape carried M1 area to M3");
  }

  void testCumulativeAreaSurvivesGatelessLayer()
  {
    // LEF/DEF 5.8 Appendix C ("Calculating a CAR", Figures C-8 and C-10): a lower-layer
    // node that was not electrically connected to a gate when that lower layer was
    // processed must not be added to the gate's CAR when an upper layer connects them.
    // Here the only gate sits on M2 (area 1). M1 has area 9 with no gate at M1, and M2
    // has area 7, so M2 PAR = 7 / 1 = 7 (> 2) and M2 CAR = 7 / 1 = 7 (not 16, and not > 7).
    loadDesign("design_late_gate.def.in");
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    require(report.find("Net: island | Layer: M2 | Type: CAR\n") == std::string::npos,
            "gateless M1 island was retroactively added into M2 CAR");
    require(imj::Utility::equalDoubleByError(getNumber(getViolationOf(report, "island", "M2", "PAR"), "Ratio: "), 7.0, 1e-9),
            "M2 PAR changed");

    // Lowering the M2 CAR threshold below 7 verifies that M2 CAR is exactly 7.
    auto* m2 = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer("M2"));
    require(m2 != nullptr, "missing M2");
    m2->set_antenna_cum_area_ratio(6.0);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string car = getViolationOf(report, "island", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Ratio: "), 7.0, 1e-9),
            "M2 CAR included lower-layer metal that had no gate during M1 processing");
  }

  void writeCaseFiles(const std::filesystem::path& dir, const std::string& extra_lef, const std::string& def_text,
                      const std::string& extra_tech_layers = "")
  {
    std::filesystem::create_directories(dir);
    std::filesystem::path tech_lef_path = _data / "tech.lef";
    if (!extra_tech_layers.empty()) {
      std::string tech = readText(tech_lef_path);
      const std::string marker = "END M2\n";
      size_t pos = tech.find(marker);
      require(pos != std::string::npos, "missing END M2 in tech.lef");
      tech.insert(pos + marker.size(), extra_tech_layers);
      tech_lef_path = dir / "tech.lef";
      std::ofstream s(tech_lef_path);
      require(s.is_open(), "open tech.lef");
      s << tech;
    }
    {
      std::ofstream s(dir / "extra.lef");
      require(s.is_open(), "open extra.lef");
      s << extra_lef;
    }
    {
      std::ofstream s(dir / "case.def");
      require(s.is_open(), "open case.def");
      s << def_text;
    }
    dmInst->reset();
    require(dmInst->readLef(std::vector<std::string>{tech_lef_path.string()}, true), "read tech");
    require(dmInst->readLef(std::vector<std::string>{(_data / "cell.lef").string()}), "read cell");
    require(dmInst->readLef(std::vector<std::string>{(dir / "extra.lef").string()}), "read extra lef");
    require(dmInst->readDef((dir / "case.def").string()), "read case def");
  }

  void testMultilayerPinAreasDoNotSum()
  {
    const std::string lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO MULTI_Q
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 LAYER M1 ;
    ANTENNAGATEAREA 4 LAYER M2 ;
    ANTENNADIFFAREA 2 LAYER M1 ;
    ANTENNADIFFAREA 2 LAYER M2 ;
    PORT
      LAYER M1 ;
        RECT 0 0 1 1 ;
      LAYER M2 ;
        RECT 0 0 1 1 ;
    END
  END A
END MULTI_Q
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN multi_layer_q ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 40000 40000 ) ;
ROW ROW_0 CORE 0 0 N DO 40 BY 1 STEP 1000 0 ;
COMPONENTS 1 ;
- u0 MULTI_Q + FIXED ( 1500 1500 ) N ;
END COMPONENTS
NETS 1 ;
- mq ( u0 A )
  + USE SIGNAL
  + ROUTED M1 ( 10000 2000 ) ( 2000 2000 ) V12
    NEW M2 ( 2000 2000 ) ( 2000 24000 ) ;
END NETS
END DESIGN
)";
    writeCaseFiles(_output / "multi_q", lef, def);
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string car = getViolationOf(report, "mq", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Gate area: "), 4.0, 1e-9),
            "multi-layer pin summed gate area across M1 and M2 instead of taking M2 total");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Diffusion area: "), 2.0, 1e-9),
            "multi-layer pin summed diff area across M1 and M2 instead of taking M2 total");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Ratio: "), 8.0, 1e-9), "unexpected M2 CAR ratio");
  }

  void testSamePortCrossLayerConnectivityWithoutExternalVia()
  {
    // Shapes belonging to the same LEF PORT are internally connected even without an external
    // DEF via bridging M1 and M2.
    // 1) Gate declared only on M1 (`ANTENNAGATEAREA 1 LAYER M1`): M1=9 + M2=23 -> M2 CAR = 32.0.
    const std::string lef_m1_gate = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO MULTI_Q_M1
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 LAYER M1 ;
    PORT
      LAYER M1 ;
        RECT 0 0 1 1 ;
      LAYER M2 ;
        RECT 0 0 1 1 ;
    END
  END A
END MULTI_Q_M1
END LIBRARY
)";
    const std::string def_no_via_m1 = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN multi_layer_q_no_via ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 40000 40000 ) ;
ROW ROW_0 CORE 0 0 N DO 40 BY 1 STEP 1000 0 ;
COMPONENTS 1 ;
- u0 MULTI_Q_M1 + FIXED ( 1500 1500 ) N ;
END COMPONENTS
NETS 1 ;
- mq ( u0 A )
  + USE SIGNAL
  + ROUTED M1 ( 10000 2000 ) ( 2000 2000 )
    NEW M2 ( 2000 2000 ) ( 2000 24000 ) ;
END NETS
END DESIGN
)";
    writeCaseFiles(_output / "multi_q_no_via_m1", lef_m1_gate, def_no_via_m1);
    auto* m2 = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer("M2"));
    require(m2 != nullptr, "missing M2");
    m2->set_antenna_cum_area_ratio(28.0);
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string car = getViolationOf(report, "mq", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Gate area: "), 1.0, 1e-9),
            "unexpected M2 gate area on same-PORT pin without external via");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Ratio: "), 32.0, 1e-9),
            "same-PORT M1 area lost when external route omitted V12");

    // 2) Both M1 and M2 LAYER-qualified gates (`1` on M1, `4` on M2), no external via:
    //    M1=9 + M2=23, gate deduplicated to M2=4 -> M2 CAR = 8.0 > 7.
    const std::string lef_m1_m2_gate = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO MULTI_Q
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAGATEAREA 1 LAYER M1 ;
    ANTENNAGATEAREA 4 LAYER M2 ;
    ANTENNADIFFAREA 2 LAYER M1 ;
    ANTENNADIFFAREA 2 LAYER M2 ;
    PORT
      LAYER M1 ;
        RECT 0 0 1 1 ;
      LAYER M2 ;
        RECT 0 0 1 1 ;
    END
  END A
END MULTI_Q
END LIBRARY
)";
    const std::string def_no_via_both = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN multi_layer_q_no_via_both ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 40000 40000 ) ;
ROW ROW_0 CORE 0 0 N DO 40 BY 1 STEP 1000 0 ;
COMPONENTS 1 ;
- u0 MULTI_Q + FIXED ( 1500 1500 ) N ;
END COMPONENTS
NETS 1 ;
- mq ( u0 A )
  + USE SIGNAL
  + ROUTED M1 ( 10000 2000 ) ( 2000 2000 )
    NEW M2 ( 2000 2000 ) ( 2000 24000 ) ;
END NETS
END DESIGN
)";
    writeCaseFiles(_output / "multi_q_no_via_both", lef_m1_m2_gate, def_no_via_both);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    car = getViolationOf(report, "mq", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Gate area: "), 4.0, 1e-9),
            "multi-layer same-PORT pin gate area wrong without external via");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Ratio: "), 8.0, 1e-9),
            "multi-layer same-PORT M2 CAR wrong without external via");
  }

  void testPartialAreaOnLayerQualifiedPinReachedViaUpperLayer()
  {
    const std::string lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO PARTIAL_Q
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SITE CORE ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    ANTENNAPARTIALMETALAREA 20 LAYER M1 ;
    ANTENNAGATEAREA 1 LAYER M2 ;
    PORT
      LAYER M2 ;
        RECT 0 0 1 1 ;
    END
  END A
END PARTIAL_Q
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN partial_q ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 30000 30000 ) ;
ROW ROW_0 CORE 0 0 N DO 30 BY 1 STEP 1000 0 ;
COMPONENTS 1 ;
- u0 PARTIAL_Q + FIXED ( 1500 1500 ) N ;
END COMPONENTS
NETS 1 ;
- pq ( u0 A )
  + USE SIGNAL
  + ROUTED M2 ( 2000 2000 ) ( 2000 3000 ) ;
END NETS
END DESIGN
)";
    writeCaseFiles(_output / "partial_q", lef, def);
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string car = getViolationOf(report, "pq", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Ratio: "), 22.0, 1e-9),
            "ANTENNAPARTIALMETALAREA on M1 lost on LAYER-qualified upper pin");
  }

  void testDriverOnlyLowerIslandAccumulates()
  {
    // On M1, only the driver output diffusion (DRV_DIFF/Y) is connected to the M1 segment
    // (area 9); no gate is reached until M2 (GATE_M2/A, gate area 1, M2 area 7). Per LEF 5.8
    // Appendix C, the driver-only M1 segment caused no gate oxide damage during M1 processing
    // and must not accumulate into M2 CAR (which is 7 / 1 = 7, not 16), while the driver
    // diffusion on M1 still connects to the component by M2.
    const std::string lef = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
MACRO DRV_DIFF
  CLASS CORE ;
  ORIGIN 0 0 ;
  SIZE 1 BY 1 ;
  SITE CORE ;
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    ANTENNADIFFAREA 2.0 ;
    PORT
      LAYER M1 ;
        RECT 0 0 1 1 ;
    END
  END Y
END DRV_DIFF
END LIBRARY
)";
    const std::string def = R"(VERSION 5.8 ;
DIVIDERCHAR "/" ;
BUSBITCHARS "[]" ;
DESIGN drv_island ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 30000 30000 ) ;
ROW ROW_0 CORE 0 0 N DO 30 BY 1 STEP 1000 0 ;
COMPONENTS 2 ;
- drv DRV_DIFF + FIXED ( 1500 1500 ) N ;
- late GATE_M2 + FIXED ( 9500 7500 ) N ;
END COMPONENTS
NETS 1 ;
- dnet ( drv Y ) ( late A )
  + USE SIGNAL
  + ROUTED M1 ( 2000 2000 ) ( 10000 2000 ) V12
    NEW M2 ( 10000 2000 ) ( 10000 8000 ) ;
END NETS
END DESIGN
)";
    writeCaseFiles(_output / "drv_island", lef, def);
    MJI.checkAntenna({});
    std::string report = readText(_output / "antenna_checker/antenna_check.rpt");
    require(report.find("Net: dnet | Layer: M2 | Type: CAR\n") == std::string::npos,
            "driver-only M1 island was retroactively added into M2 CAR");
    auto* m2 = dynamic_cast<idb::IdbLayerRouting*>(dmInst->get_idb_layout()->get_layers()->find_layer("M2"));
    require(m2 != nullptr, "missing M2");
    m2->set_antenna_cum_area_ratio(6.0);
    MJI.checkAntenna({});
    report = readText(_output / "antenna_checker/antenna_check.rpt");
    std::string car = getViolationOf(report, "dnet", "M2", "CAR");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Ratio: "), 7.0, 1e-9),
            "driver-only lower-layer island inflated M2 CAR");
    require(imj::Utility::equalDoubleByError(getNumber(car, "Diffusion area: "), 2.0, 1e-9),
            "driver diffusion on lower layer lost when merged on M2");
  }
};

int main()
{
  try {
    AntennaReportingTest().run();
  } catch (const std::exception& error) {
    MJLOG.warn(imj::Loc::current(), "Antenna checking regression failed: ", error.what());
    return 1;
  }
  return 0;
}
