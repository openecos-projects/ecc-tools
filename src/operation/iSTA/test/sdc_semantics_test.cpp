// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <filesystem>

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
  auto& sdc = db.sdc();
  auto& interp = SdcCommand::getInst();
  const std::string mode = argc > 1 ? argv[1] : "uncertainty";
  eval("foreach c {A B C D} { create_clock -name $c -period 10 }");
  if (mode == "writer_names") {
    eval("create_clock -name M -period 10 [get_ports clk]; set_clock_latency .7 [get_pins cdc/buf/Y]");
    std::ostringstream output;
    sdc::writeSdc(db, output);
    require(output.str().find("cdc/buf:Y") == std::string::npos && output.str().find("cdc/buf/Y") != std::string::npos,
            "local clock annotation exported internal pin spelling");
  } else if (mode == "removed_source") {
    eval(
        "create_clock -name M -period 10 [get_ports {in clk}];"
        "create_generated_clock -name G -source [get_ports in] -master_clock M -divide_by 2 [get_pins cdc/buf/Y]");
    std::ostringstream before;
    sdc::writeSdc(db, before);
    require(interp.evalString("create_clock -name R -period 12 [get_ports in]") == TCL_ERROR, "removing a generated clock master source succeeded");
    std::ostringstream after;
    sdc::writeSdc(db, after);
    require(before.str() == after.str(), "failed master-source replacement partially changed constraints");
    sdc.clearConstraints();
    eval(
        "create_clock -name M -period 10 [get_ports {in clk}];"
        "create_generated_clock -name G -source [get_pins cdc/source_reg/CK] -master_clock M -divide_by 2 [get_pins cdc/buf/Y]");
    require(interp.evalString("create_clock -name R -period 12 [get_ports clk]") == TCL_ERROR,
            "removing the only root reaching an internal generated source succeeded");

  } else if (mode == "removed_refs") {
    eval(
        "create_clock -name M -period 10 [get_ports in]; create_clock -name N -period 10;"
        "set_clock_uncertainty .3 -from {M A} -to {N B};"
        "set_false_path -from [get_clocks {M A}] -to [get_clocks N];"
        "set_false_path -from [get_clocks M] -to [get_clocks N];"
        "set_input_delay .2 -clock M [get_ports in]; set_input_delay -add_delay .4 -clock A [get_ports in];"
        "set_output_delay .3 -clock M [get_ports out];"
        "set_clock_latency -clock M .6 [get_pins cdc/buf/Y];"
        "set_clock_latency -clock A .7 [get_pins cdc/buf/Y];"
        "set_clock_groups -name pair -asynchronous -group M -group N;"
        "set_clock_groups -name triple -asynchronous -group {M A} -group N -group B;"
        "set_max_transition .2 [get_clocks M]; set_max_transition .3 [get_clocks A];"
        "create_clock -name R -period 12 [get_ports in]");
    require(!sdc.get_clock_map().contains("M"), "displaced clock remains");
    std::ostringstream output;
    sdc::writeSdc(db, output);
    sdc.clearConstraints();
    require(interp.evalString(output.str()) == TCL_OK, "deleted clock left dangling serialized constraints");
    require(sdc.resolve_clock_uncertainty("A", TransType::kRise, "N", TransType::kRise, AnalysisType::kMax) == .3,
            "clock deletion lost unaffected uncertainty pair");
    require(sdc.get_path_exception_list().size() == 1
                && sdc.get_path_exception_list().front().get_from_objects() == ExceptionObjects{{ExceptionObjectType::kClock, "A"}},
            "clock deletion widened or lost exception subset");
    require(sdc.get_port_constraint_map().at("in").inputClock() == "A", "clock deletion lost other IO delay");
    require(sdc.get_clock_group_list().size() == 1 && sdc.get_clock_group_list().front().get_groups().size() == 3,
            "clock deletion changed a two-group cut into a global cut");
    require(sdc.clockPinConstraints().at("cdc/buf:Y").contains("A"), "clock deletion lost another local selector");
  } else if (mode == "status") {
    const std::vector<std::string> commands = {
        R"TCL(create_clock -name M -period 10 [get_ports in])TCL",
        R"TCL(create_clock -name V -period 12)TCL",
        R"TCL(create_generated_clock -add -name G -master_clock M -source [get_ports in] -divide_by 2 [get_pins cdc/buf/Y])TCL",
        R"TCL(set_clock_latency -source .1 M)TCL",
        R"TCL(remove_clock_latency -source M)TCL",
        R"TCL(set_clock_transition .05 M)TCL",
        R"TCL(set_clock_uncertainty .1 M)TCL",
        R"TCL(remove_clock_uncertainty M)TCL",
        R"TCL(set_propagated_clock M)TCL",
        R"TCL(remove_propagated_clock M)TCL",
        R"TCL(set_clock_groups -asynchronous -group M -group V)TCL",
        R"TCL(set_input_delay .2 -clock M [get_ports in])TCL",
        R"TCL(set_output_delay .3 -clock M [get_ports out])TCL",
        R"TCL(set_input_transition .05 [get_ports in])TCL",
        R"TCL(set_driving_cell -lib_cell COMB -pin Y [get_ports in])TCL",
        R"TCL(set_load .2 [get_ports out])TCL",
        R"TCL(set_max_fanout 10 [current_design])TCL",
        R"TCL(set_max_transition .5 [current_design])TCL",
        R"TCL(set_max_capacitance .1 [current_design])TCL",
        R"TCL(set_timing_derate -late 1.1)TCL",
        R"TCL(set_case_analysis 0 [get_ports out])TCL",
        R"TCL(set_false_path -from [get_ports in] -to [get_ports out])TCL",
        R"TCL(reset_path -from [get_ports in] -to [get_ports out])TCL",
        R"TCL(set_max_delay 9 -from [get_ports in] -to [get_ports out])TCL",
        R"TCL(set_min_delay .1 -from [get_ports in] -to [get_ports out])TCL",
        R"TCL(set_multicycle_path 2 -setup -from [get_ports in] -to [get_ports out])TCL",
        R"TCL(set_units -time ns -capacitance pF)TCL",
        R"TCL(unset_clock_latency M)TCL",
        R"TCL(unset_clock_uncertainty M)TCL",
        R"TCL(unset_propagated_clock M)TCL",
        R"TCL(unset_path_exceptions -from [get_ports in] -to [get_ports out])TCL",
    };
    for (const auto& command : commands) {
      eval(command);
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "1", "successful constraint command returned no status: " + command);
    }
    eval("get_ports -quiet no_such_port");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).empty(), "empty query collection changed to status one");
    const auto period = sdc.get_clock_map().at("M").get_period();
    require(interp.evalString("create_clock -name M -period -1 [get_ports in]") == TCL_ERROR, "invalid period silently succeeded");
    require(sdc.get_clock_map().at("M").get_period() == period, "failed command changed the old clock");
  } else if (mode == "units") {
    require(interp.evalString("set_units -capacitance 0.5pF") == TCL_OK, "positive unit multiplier rejected as syntax");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "0", "unit mismatch did not return zero");
    eval("set_units -time 1000ps -capacitance pF");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "1", "matching units did not return one");
    db.liberty().set_time_unit(TimingTimeUnit::kPS);
    eval("set_units -time ps");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "1", "unit check ignored main-library unit");
    STADM.getDatabase().liberty().set_cap_unit(TimingCapacitiveUnit::kFF);
    eval("create_clock -name units -period 10000 -waveform {1000 4000} [get_ports clk]");
    require(std::abs(sdc.get_clock_map().at("units").get_period() - 10) < 1e-12, "library ps units not converted for clock periods");
    eval("set_input_delay -clock units 200 [get_ports in]; set_load 200 [get_ports out]");
    require(std::abs(sdc.get_port_constraint_map().at("in").get_input_delay_max() - .2) < 1e-12, "library ps units not converted for I/O delays");
    require(std::abs(sdc.get_port_constraint_map().at("out").get_load(AnalysisType::kMax, TransType::kRise) - .2) < 1e-12,
            "library ff units not converted for loads");
    std::ostringstream output;
    sdc::writeSdc(STADM.getDatabase(), output);
    sdc.clearConstraints();
    eval(output.str());
    require(std::abs(sdc.get_clock_map().at("units").get_period() - 10) < 1e-12, "non-default unit roundtrip changed clock period");

  } else if (mode == "driving") {
    eval("set_driving_cell -lib_cell COMB -pin Y [get_ports in]; set_input_transition -fall .05 [get_ports in]");
    require(!sdc.get_port_constraint_map().at("in").get_has_driving_cell(AnalysisType::kMax, TransType::kRise),
            "input transition failed to clear the old driving-cell model");
    require(sdc.get_port_constraint_map().at("in").get_input_transition(AnalysisType::kMax, TransType::kRise) == 0,
            "fall-only input transition populated an unspecified rise");
    for (const auto* option : {"-multiply_by 2", "-dont_scale"})
      require(interp.evalString(std::string("set_driving_cell -lib_cell COMB -pin Y ") + option + " [get_ports in]") == TCL_ERROR,
              "unsupported driver option silently ignored");
  } else if (mode == "local_clock") {
    eval("create_clock -name M -period 10 [get_ports in]");
    eval("set_clock_latency .7 -clock M [get_pins cdc/buf/Y]");
    eval("set_clock_uncertainty .4 [get_pins cdc/buf/Y]");
    eval("set_propagated_clock [get_pins cdc/reconv/Y]");
    require(interp.evalString("set_clock_latency -source .4 [get_pins cdc/buf/Y]") == TCL_ERROR, "source latency accepted on an ordinary internal pin");
    require(interp.evalString("set_clock_latency .5 [get_nets in]") == TCL_ERROR, "typed net accepted as a clock");
    eval("create_clock -name N -period 10 -add [get_ports in]");
    eval("set_propagated_clock [get_pins cdc/reconv/Y]; set_clock_latency .8 -clock M [get_pins cdc/reconv/Y]");
    auto values = [&](const std::string& pin, const std::string& clock) { return clockPinValues(db, pin, clock, AnalysisType::kMax, TransType::kRise); };
    require(!values("cdc/reconv:Y", "M").propagated && values("cdc/reconv:Y", "M").network_latency == .8 && values("cdc/reconv:Y", "N").propagated,
            "local clock selector changed another clock");
    const auto file = std::filesystem::temp_directory_path() / ("ista-local-clock-" + std::to_string(getpid()) + ".sdc");
    sdc::writeSdcFile(db, file.string());
    sdc.clearConstraints();
    require(interp.evalScriptFile(file.string()) == TCL_OK, "local clock roundtrip failed");
    require(!values("cdc/reconv:Y", "M").propagated && values("cdc/reconv:Y", "M").network_latency == .8 && values("cdc/reconv:Y", "N").propagated,
            "writer reordered scoped clock annotations");
    require(values("cdc/buf:Y", "M").uncertainty == .4 && values("cdc/buf:Y", "M").network_latency == .7, "local annotations lost on roundtrip");
    eval("set_clock_uncertainty .2 M; remove_clock_uncertainty [get_pins cdc/buf/Y]");
    require(values("cdc/buf:Y", "M").uncertainty == .2, "local uncertainty removal did not expose clock fallback");
    std::filesystem::remove(file);
  } else if (mode == "latency") {
    eval("create_clock -name M -period 10 [get_ports in]");
    eval(
        "set_clock_latency -source -min -early .1 M; set_clock_latency -source -max -early .2 M;"
        "set_clock_latency -source -min -late .3 M; set_clock_latency -source -max -late .4 M");
    require(sdc.get_clock_map().at("M").get_source_latency(AnalysisType::kMin, clockVariation(AnalysisType::kMin), TransType::kRise) == .1,
            "late source latency overwrote early/min value");
    eval("set_propagated_clock M; set_clock_latency .6 M");
    require(!sdc.get_clock_map().at("M").get_is_propagated(), "network latency did not restore ideal clock");
    eval("set_propagated_clock M");
    const auto file = std::filesystem::temp_directory_path() / ("ista-latency-" + std::to_string(getpid()) + ".sdc");
    sdc::writeSdcFile(db, file.string());
    sdc.clearConstraints();
    require(interp.evalScriptFile(file.string()) == TCL_OK, "latency roundtrip failed");
    require(sdc.get_clock_map().at("M").get_is_propagated()
                && sdc.get_clock_map().at("M").get_source_latency(AnalysisType::kMin, clockVariation(AnalysisType::kMin), TransType::kRise) == .1,
            "writer reordered latency and propagated annotations");
    const auto& restored = sdc.get_clock_map().at("M");
    require(restored.get_source_latency(AnalysisType::kMax, ClockVariation::kEarly, TransType::kRise) == .2
                && restored.get_source_latency(AnalysisType::kMin, ClockVariation::kLate, TransType::kRise) == .3
                && restored.get_source_latency(AnalysisType::kMax, ClockVariation::kLate, TransType::kRise) == .4,
            "source latency roundtrip collapsed an independent axis");
    std::filesystem::remove(file);
  } else if (mode == "waveform") {
    eval("create_clock -name W -period 10 -waveform {8 2}");
    require(sdc.get_clock_map().at("W").get_waveform() == std::vector<double>{8, 12}, "wrapping clock waveform not normalized");
    require(interp.evalString("create_clock -name Z -period 10 -waveform {0 0}") == TCL_ERROR, "zero-width clock waveform accepted");
    eval("create_clock -name M -period 10 -waveform {0 2 5 7} [get_ports in]");
    for (const auto& [transform, period, waveform] : std::vector<std::tuple<std::string, double, std::vector<double>>>{{"-edges {1 2 3 4 5}", 10, {0, 2, 5, 7}},
                                                                                                                       {"-divide_by 2", 20, {0, 5, 10, 15}},
                                                                                                                       {"-multiply_by 2", 5, {0, 1, 2.5, 3.5}},
                                                                                                                       {"-divide_by 3", 30, {0, 6, 15, 21}},
                                                                                                                       {"-combinational", 10, {0, 2, 5, 7}}}) {
      eval("create_generated_clock -add -name G -master_clock M -source [get_ports in] " + transform + " [get_pins cdc/buf/Y]");
      require(sdc.get_clock_map().at("G").get_period() == period && sdc.get_clock_map().at("G").get_waveform() == waveform,
              "generated clock lost a master waveform edge");
    }
  } else if (mode == "generated") {
    eval("create_clock -name M -period 10 -waveform {1 4} [get_ports in]");
    eval("create_generated_clock -name G -source [get_ports in] -master_clock M -divide_by 2 [get_pins cdc/buf/Y]");
    eval("create_generated_clock -name H -source [get_pins cdc/buf/Y] -master_clock G -divide_by 2 [get_pins cdc/reconv/Y]");
    eval("set_clock_uncertainty 0.25 G; set_clock_latency -source 0.4 G; set_propagated_clock G");
    eval("set_clock_uncertainty .15 M; set_clock_latency -source .2 M; set_propagated_clock M");
    eval("create_clock -name M -period 20 -waveform {2 8} [get_ports in]");
    require(sdc.get_clock_map().at("M").get_is_propagated() && sdc.get_clock_map().at("M").get_setup_uncertainty() == .15,
            "same-name redefinition discarded clock annotations");
    require(sdc.get_clock_map().at("G").get_period() == 40 && sdc.get_clock_map().at("H").get_period() == 80,
            "master redefinition left generated descendants stale");
    require(sdc.get_clock_map().at("G").get_setup_uncertainty() == 0.25 && sdc.get_clock_map().at("G").get_is_propagated()
                && sdc.get_clock_map().at("G").get_source_latency(AnalysisType::kMax, clockVariation(AnalysisType::kMax), TransType::kRise) == 0.4,
            "refresh discarded generated clock annotations");
    const auto file = std::filesystem::temp_directory_path() / ("ista-generated-" + std::to_string(getpid()) + ".sdc");
    sdc::writeSdcFile(db, file.string());
    sdc.clearConstraints();
    require(interp.evalScriptFile(file.string()) == TCL_OK, "generated definition roundtrip failed");
    eval("create_clock -name M -period 30 -waveform {3 12} [get_ports in]");
    require(sdc.get_clock_map().at("G").get_period() == 60 && sdc.get_clock_map().at("H").get_period() == 120, "write_sdc froze generated transformation");
    require(interp.evalString("create_generated_clock -name M -source [get_pins cdc/reconv/Y] -master_clock H -divide_by 2 [get_ports in]") == TCL_ERROR,
            "generated dependency cycle accepted");
    require(sdc.get_clock_map().at("M").get_period() == 30 && sdc.get_clock_map().at("H").get_period() == 120, "cycle failure modified existing clocks");
    require(interp.evalString("create_clock -name N -period 12 [get_ports in]") == TCL_ERROR, "removing a required master left stale generated clocks");
    require(sdc.get_clock_map().contains("M") && !sdc.get_clock_map().contains("N"), "missing-master failure was not atomic");
    eval("create_clock -name alone -period 10 [get_ports reset]; create_clock -name replacement -period 12 [get_ports reset]");
    require(!sdc.get_clock_map().contains("alone"), "displaced primary clock silently became virtual");
    std::filesystem::remove(file);
  } else if (mode == "uncertainty") {
    eval("set_clock_uncertainty 0.3 -from [get_clocks {A B}] -to [get_clocks {C D}]");
    eval("remove_clock_uncertainty -setup -from [get_clocks A] -to [get_clocks C]");
    auto value = [&](const std::string& from, const std::string& to, TransType edge, AnalysisType type) {
      return sdc.resolve_clock_uncertainty_value(from, edge, to, TransType::kRise, type);
    };
    require(!value("A", "C", TransType::kRise, AnalysisType::kMax), "uncertainty subset was not removed");
    for (const auto& pair : {std::pair{"A", "D"}, {"B", "C"}, {"B", "D"}})
      require(value(pair.first, pair.second, TransType::kRise, AnalysisType::kMax) == 0.3, "uncertainty deletion damaged other pairs");
    require(value("A", "C", TransType::kRise, AnalysisType::kMin) == 0.3, "setup deletion removed hold");
    eval("set_clock_uncertainty -setup 0.5 -from A -to C; set_clock_uncertainty -setup 0.7 -from A -to C");
    eval("remove_clock_uncertainty -setup -rise_from A -rise_to C");
    require(!value("A", "C", TransType::kRise, AnalysisType::kMax), "older uncertainty resurrected after edge removal");
    require(value("A", "C", TransType::kFall, AnalysisType::kMax) == 0.7, "edge removal erased opposite edge");
    const auto file = std::filesystem::temp_directory_path() / ("ista-uncertainty-" + std::to_string(getpid()) + ".sdc");
    sdc::writeSdcFile(db, file.string());
    sdc.clearConstraints();
    require(interp.evalScriptFile(file.string()) == TCL_OK, "uncertainty roundtrip failed");
    require(!value("A", "C", TransType::kRise, AnalysisType::kMax) && value("A", "C", TransType::kFall, AnalysisType::kMax) == 0.7,
            "writer changed uncertainty subtraction");
    std::filesystem::remove(file);
    eval("set_clock_uncertainty -setup 0.5 -rise_from A -rise_to C; set_clock_uncertainty -setup 0.9 -from A -to C");
    require(value("A", "C", TransType::kRise, AnalysisType::kMax) == 0.9, "new generic uncertainty failed to replace edge-specific value");
  } else if (mode == "reset") {
    for (const auto& setter : {"set_false_path", "set_max_delay 1", "set_min_delay 1", "set_multicycle_path 2"}) {
      sdc.get_path_exception_list().clear();
      eval(std::string(setter) + " -through [get_pins cdc/buf/A]");
      eval("reset_path -through [get_pins cdc/buf/A] -through [get_pins cdc/reconv/A]");
      require(!sdc.get_path_exception_list().empty(), "longer ordered through incorrectly removed an exception");
      eval("reset_path -through [get_pins cdc/buf/A]");
      require(sdc.get_path_exception_list().empty(), "exact ordered through failed to remove exception");
    }
    for (const auto& filter : {"-through [get_pins cdc/reconv/A]", "-through [get_pins cdc/buf/A] -through [get_pins cdc/reconv/A]"}) {
      eval("set_false_path -through [get_pins cdc/buf/A] -through [get_pins cdc/buf/Y] -through [get_pins cdc/reconv/A]");
      eval(std::string("reset_path ") + filter);
      require(sdc.get_path_exception_list().empty(), "ordered through subsequence did not reset exception");
    }
    eval("set_false_path -through [get_pins cdc/buf/A] -through [get_pins cdc/reconv/A]");
    eval("reset_path -through [get_pins cdc/reconv/A] -through [get_pins cdc/buf/A]");
    require(!sdc.get_path_exception_list().empty(), "reversed through sequence reset exception");
    sdc.get_path_exception_list().clear();
    eval("set_false_path -from A -to C; reset_path -rise_from A -to C");
    require(!sdc.get_path_exception_list().empty(), "edge-specific selector removed unqualified exception");
    sdc.get_path_exception_list().clear();
    eval("set_false_path -rise_from A -to C; reset_path -from A -to C");
    require(!sdc.get_path_exception_list().empty(), "unqualified selector removed edge-specific exception");
  } else if (mode == "groups") {
    eval("set_clock_groups -asynchronous -group A -group B");
    require(interp.evalString("set_clock_groups -asynchronous -allow_paths -group A -group B") == TCL_ERROR, "contradictory group relation accepted");
    require(sdc.get_clock_group_list().size() == 1 && !sdc.get_clock_group_list().front().get_allow_paths(), "group conflict mutated constraints");
  }
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
}
