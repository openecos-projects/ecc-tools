// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "SdcTestDesign.hpp"
#include "SdcWriter.hpp"
#include "graph/TimingStateQuery.hpp"

int main(int argc, char** argv)
{
  Tcl_FindExecutable(argv[0]);
  Logger::initInst();
  DataManager::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  buildDesign();
  auto& db = STADM.getDatabase();
  auto& interp = SdcCommand::getInst();
  const std::string mode = argc > 1 ? argv[1] : "transition";
  const auto path = std::filesystem::temp_directory_path() / ("ista-limits-" + std::to_string(getpid()) + ".sdc");
  eval("create_clock -name core -period 10 [get_ports clk]");
  std::string expected;
  auto cellDelay = [&](const std::string& cell) {
    Arc arc;
    arc.set_type(ArcType::kCell);
    arc.set_source_pin(cell + ":A");
    arc.set_sink_pin(cell + ":Y");
    arc.get_input_output_delay_map()[AnalysisType::kMax][TransType::kRise][TransType::kRise] = 2.;
    return TimingStateQuery(db).getDataArcDelay(arc, AnalysisType::kMax, TransType::kRise, TransType::kRise);
  };
  auto near
      = [&](double actual, double wanted, const std::string& message) { require(std::abs(actual - wanted) < 1e-12, message + ": " + std::to_string(actual)); };
  if (mode == "pulse") {
    eval("set_timing_derate -min_period -rise 1.3");
    eval("set_timing_derate -min_pulse_width -fall 1.6 [get_cells cdc/source_reg]");
    eval("set_timing_derate -min_period -min_pulse_width -increment 0.1 [get_lib_cells */FF]");
    for (const auto& invalid :
         {"set_timing_derate -min_period -early 1.1", "set_timing_derate -min_pulse_width -late 1.1", "set_timing_derate -min_period -clock 1.1",
          "set_timing_derate -min_period -cell_check 1.1", "set_timing_derate -min_pulse_width -net_delay 1.1"})
      require(interp.evalString(invalid) == TCL_ERROR, "incompatible pulse derate options accepted");
    near(db.sdc().timingDerates().factor(AnalysisType::kNone, false, TransType::kFall, DerateDelay::kWidth, {"cdc/source_reg", "/FF", ""}), 1.7,
         "pulse baseline/increment scopes");
    expected = "-min_pulse_width";
  } else if (mode == "force") {
    eval("set_max_transition 0.1 [current_design]");
    eval("set_max_transition -force 0.8 [get_ports in]");
    eval("set_max_capacitance 0.1 [current_design]");
    eval("set_max_capacitance -force 0.8 [get_pins cdc/buf/Y]");
    near(*db.sdc().electricalLimits().limit(ElectricalRule::kTransition, "in", true, "", "", false, TransType::kRise), .8,
         "forced port limit lost to design limit");
    for (const auto& invalid : {"set_max_transition -force 0.9 [current_design]", "set_max_capacitance -force 0.9 [get_clocks core]"})
      require(interp.evalString(invalid) == TCL_ERROR, "invalid forced scope accepted");
    expected = "-force";
  } else if (mode == "hierarchy") {
    db.network().hierarchyPins()["cdc:boundary"] = {"cdc", "boundary", PinDirection::kInput, "clock_net"};
    eval("set_timing_derate -late 1.10");
    eval("set_timing_derate -late -cell_delay 1.2 [get_cells cdc]");
    near(cellDelay("cdc/buf"), 2.4, "hierarchical cell factor not consumed");
    eval("set_timing_derate -late 1.3 [get_lib_cells */COMB]");
    near(cellDelay("cdc/buf"), 2.6, "hierarchy incorrectly outranked library cell");
    eval("set_max_transition 0.01 [get_pins cdc/boundary]");
    eval("set_max_capacitance 0.01 [get_pins cdc/boundary]");
    require(interp.evalString("set_timing_derate -late 1.4 [get_cells cdc]") == TCL_ERROR, "hierarchy derate accepted without explicit kind");
    expected = "get_cells";
  } else if (mode == "scoped") {
    eval("set_timing_derate -late 1.10");
    eval("set_timing_derate -late -increment 0.04");
    eval("set_timing_derate -late 1.05 [get_cells cdc/buf]");
    near(cellDelay("cdc/buf"), 2.18, "local baseline lost inherited increment");
    near(cellDelay("cdc/reconv"), 2.28, "local baseline leaked to another instance");
    eval("set_timing_derate -late -increment 0.02");
    near(cellDelay("cdc/buf"), 2.14, "increment accumulated instead of replacing its selector");
    eval("set_timing_derate -late 1.30 [get_lib_cells */COMB]");
    near(cellDelay("cdc/buf"), 2.14, "library baseline overrode leaf baseline");
    near(cellDelay("cdc/reconv"), 2.64, "library baseline did not override global baseline");
    eval("set_timing_derate -late -increment -0.03 [get_cells cdc/reconv]");
    near(cellDelay("cdc/reconv"), 2.54, "local negative increment did not override global increment");
    eval("set_timing_derate -late -net_delay 1.9 [get_cells cdc/buf]");
    near(cellDelay("cdc/buf"), 2.14, "explicit net qualifier incorrectly changed a leaf cell delay");
    require(interp.evalString("set_timing_derate -late 1.8 [list [get_cells cdc/buf] missing]") == TCL_ERROR, "invalid mixed derate collection accepted");
    near(cellDelay("cdc/buf"), 2.14, "invalid collection partly modified derates");
    expected = "set_timing_derate";
  } else if (mode == "check") {
    eval("set_timing_derate -late -cell_check 1.2 [get_cells cdc/source_reg]");
    eval("set_timing_derate -early -cell_check -increment 0.1 [get_lib_cells */FF]");
    for (const auto& invalid : {"set_timing_derate -late -cell_check -clock 1.1", "set_timing_derate -late -cell_check -data 1.1",
                                "set_timing_derate -late -cell_check -cell_delay 1.1", "set_timing_derate -late -cell_check -net_delay 1.1"})
      require(interp.evalString(invalid) == TCL_ERROR, "incompatible check derate options accepted");
    expected = "set_timing_derate";
  } else if (mode == "transition") {
    eval("set_max_transition 0.5 -clock_path -rise [get_clocks core]");
    eval("set_max_transition 1.0 -data_path [get_clocks core]");
    eval("set_max_transition 0.8 [current_design]");
    expected = "set_max_transition";
    const auto count = db.sdc().electricalLimits().entries().size();
    require(interp.evalString("set_max_transition 0.01 [list [get_ports in] missing]") == TCL_ERROR, "partly invalid collection accepted");
    require(db.sdc().electricalLimits().entries().size() == count, "invalid collection partially edited constraints");
    for (const auto& invalid : {"set_max_transition -1 [current_design]", "set_max_transition 1 {}", "set_max_transition 1 -clock_path [get_ports in]",
                                "set_max_transition 1 [get_nets clock_net]"})
      require(interp.evalString(invalid) == TCL_ERROR, "invalid transition constraint accepted");
  } else if (mode == "capacitance") {
    eval("set_max_capacitance 0.15 [current_design]");
    eval("set_max_capacitance 0.1 [get_ports out]");
    expected = "set_max_capacitance";
    for (const auto& invalid : {"set_max_capacitance -1 [current_design]", "set_max_capacitance 1 {}", "set_max_capacitance 1 [get_cells cdc/buf]"})
      require(interp.evalString(invalid) == TCL_ERROR, "invalid capacitance constraint accepted");
  } else if (mode == "derate") {
    eval("set_timing_derate -early 0.95 -clock -delay_corner [all_delay_corners]");
    eval("set_timing_derate -late 1.10 -clock -delay_corner [all_delay_corners]");
    eval("set_timing_derate -late 1.10 -data -delay_corner [all_delay_corners]");
    eval("set_timing_derate -late 1.25 -rise -net_delay -data -static");
    const auto& factors = db.sdc().timingDerates();
    require(factors.factor(AnalysisType::kMax, false, TransType::kRise, DerateDelay::kNet) == 1.25
                && factors.factor(AnalysisType::kMax, false, TransType::kFall, DerateDelay::kNet) == 1.1
                && factors.factor(AnalysisType::kMax, false, TransType::kRise, DerateDelay::kCell) == 1.1
                && factors.factor(AnalysisType::kMax, true, TransType::kRise, DerateDelay::kNet) == 1.1,
            "derate edge, kind or path qualifier leaked");
    expected = "set_timing_derate";
    for (const auto& invalid : {"set_timing_derate 1.1", "set_timing_derate -early -late 1.1", "set_timing_derate -early -1",
                                "set_timing_derate -late 1.1 -delay_corner {}", "set_timing_derate -late 1.1 -delay_corner missing"})
      require(interp.evalString(invalid) == TCL_ERROR, "invalid derate accepted");
  } else {
    eval("all_delay_corners");
    int count = 0;
    require(Tcl_ListObjLength(interp.getInterp(), Tcl_GetObjResult(interp.getInterp()), &count) == TCL_OK && count == 1,
            "single analysis corner must be selectable, not an empty no-op");
  }
  sdc::writeSdcFile(db, path.string());
  std::ifstream file(path);
  const std::string before((std::istreambuf_iterator<char>(file)), {});
  require(expected.empty() || before.find(expected) != std::string::npos, "effective constraint lost by write_sdc");
  db.sdc().clearConstraints();
  require(interp.evalScriptFile(path.string()) == TCL_OK, "written constraints cannot be read");
  sdc::writeSdcFile(db, path.string());
  std::ifstream reread(path);
  const std::string after((std::istreambuf_iterator<char>(reread)), {});
  require(before == after, "effective constraints changed across SDC roundtrip");
  if (mode == "scoped") {
    near(cellDelay("cdc/buf"), 2.14, "roundtrip changed local baseline/inherited increment");
    near(cellDelay("cdc/reconv"), 2.54, "roundtrip changed library baseline/local increment");
  }
  if (mode == "force") {
    near(*db.sdc().electricalLimits().limit(ElectricalRule::kTransition, "in", true, "", "", false, TransType::kRise), .8, "roundtrip lost force");
    eval("set_max_transition 0.7 [get_ports in]");
    near(*db.sdc().electricalLimits().limit(ElectricalRule::kTransition, "in", true, "", "", false, TransType::kRise), .1,
         "ordinary replacement retained force");
  }
  std::filesystem::remove(path);
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
}
