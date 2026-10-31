// SPDX-License-Identifier: MulanPSL-2.0
#include "DataManager.hpp"
#include "Logger.hpp"
#include "SdcCommand.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>

using namespace ipw;

void require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void eval(const std::string& script)
{
  auto& sdc = SdcCommand::getInst();
  const int status = Tcl_EvalEx(sdc.getInterp(), script.c_str(), -1, TCL_EVAL_GLOBAL);
  require(status == TCL_OK, "SDC failed: " + script + ": " + std::string(Tcl_GetStringResult(sdc.getInterp())));
}

void reject(const std::string& script)
{
  require(
      Tcl_EvalEx(SdcCommand::getInst().getInterp(), script.c_str(), -1, TCL_EVAL_GLOBAL) == TCL_ERROR,
      "invalid SDC accepted: " + script
  );
}

void buildDesign()
{
  auto& database = PWDM.getDatabase();
  database.set_design_name("sdc_test");
  for (const std::string name : {"clk", "in", "out"}) {
    auto& pin = database.get_pin_map()[name];
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_direction(name == "out" ? PinDirection::kOutput : PinDirection::kInput);
  }
  auto& cell = database.get_timing_library().get_cell_map()["BUF"];
  cell.set_library_name("testlib");
  cell.get_port_map()["A"].set_is_input(true);
  cell.get_port_map()["Y"].set_is_output(true);
}

void testVirtualPropagation()
{
  auto& clocks = PWDM.getDatabase().get_timing_constraint().get_clock_map();
  eval("create_clock -name VCLK -period 10\nset_propagated_clock [all_clocks]");
  require(!clocks.at("VCLK").get_is_propagated(), "virtual clock propagated");
  eval("create_clock -name CLK -period 10 [get_ports clk]\nset_propagated_clock [all_clocks]");
  require(clocks.at("CLK").get_is_propagated(), "real clock did not propagate");
  require(!clocks.at("VCLK").get_is_propagated(), "mixed collection propagated virtual clock");
  eval("remove_propagated_clock [all_clocks]");
  require(!clocks.at("CLK").get_is_propagated(), "remove propagation failed");
  eval("set_propagated_clock [get_ports clk]\nunset_propagated_clock [get_clocks CLK]");
  require(!clocks.at("CLK").get_is_propagated(), "unset propagation failed");
  reject("set_propagated_clock {}");
  reject("set_propagated_clock missing");
}

void testLibraryQueries()
{
  eval(R"TCL(
    if {[get_libs -quiet test*] ne "testlib"} {error "library query failed"}
    if {[get_lib_cell testlib/*] ne "testlib/BUF"} {error "cell query failed"}
    if {[get_lib_pins -filter {direction == out} */*/*] ne "testlib/BUF/Y"} {error "pin filter failed"}
    if {[get_full_name [get_lib_pin testlib/BUF/A]] ne "testlib/BUF/A"} {error "library full name failed"}
    if {[get_lib_cells -of_objects [get_lib_pins testlib/BUF/A]] ne "testlib/BUF"} {error "related query failed"}
    if {[get_libs -of_objects [get_lib_cells testlib/BUF]] ne "testlib"} {error "related library failed"}
    if {[llength [get_lib_pins -of_objects [get_lib_cells testlib/BUF]]] != 2} {error "related pins failed"}
  )TCL");
  reject("get_lib_cells -regexp -exact BUF");
}

void testTimingValidation()
{
  for (const char* script :
       {"set_clock_latency bad [all_clocks]",
        "set_clock_latency -early 1 [all_clocks]",
        "set_clock_uncertainty NaN [all_clocks]",
        "set_input_delay 1 -clock missing [get_ports in]",
        "set_output_delay 1 [get_ports in]",
        "set_max_fanout -1 [current_design]",
        "set_multicycle_path 1.5 -from [get_ports in]",
        "set_false_path -from missing",
        "set_clock_groups -asynchronous -logically_exclusive -group CLK -group VCLK"})
    reject(script);
}

void testPostCtsFile()
{
  const auto path = std::filesystem::temp_directory_path() / "ipw_sdc_compatibility.tcl";
  {
    std::ofstream file(path);
    file << R"TCL(
      create_clock -name CLK -period 10 [get_ports clk]
      create_clock -name VCLK -period 10
      set_clock_latency -source 0.5 [all_clocks]
      set_clock_uncertainty -setup 0.2 [all_clocks]
      set_clock_uncertainty -hold 0.1 [all_clocks]
      set_input_delay -max 3 -clock VCLK [get_ports in]
      set_output_delay -min -0.1 -clock VCLK [all_outputs]
      set_input_transition 0.2 [get_ports in]
      set_load 0.3 [all_outputs]
      set_clock_transition 0.15 [all_clocks]
      set_max_fanout 32 [current_design]
      set_propagated_clock [all_clocks]
    )TCL";
  }
  const int status = SdcCommand::getInst().evalScriptFile(path.string());
  std::filesystem::remove(path);
  require(status == TCL_OK, "post-CTS file rejected: " + SdcCommand::getInst().get_error_message());
  auto& constraint = PWDM.getDatabase().get_timing_constraint();
  require(constraint.get_clock_map().at("CLK").get_is_propagated(), "post-CTS clock stayed ideal");
  require(!constraint.get_clock_map().at("VCLK").get_is_propagated(), "post-CTS virtual clock propagated");
  require(
      constraint.get_port_constraint_map().at("in").get_input_transition(AnalysisType::kMax, TransType::kRise) == 0.2,
      "post-CTS input slew changed"
  );
  require(
      constraint.get_port_constraint_map().at("out").get_load(AnalysisType::kMax, TransType::kRise) == 0.3,
      "post-CTS output load changed"
  );
}

void testLatencyAndUncertainty()
{
  auto& c = PWDM.getDatabase().get_timing_constraint();
  eval("set_clock_latency -source -early -rise 0.4 CLK\nset_clock_latency -max 0.8 CLK");
  auto& clock = c.get_clock_map().at("CLK");
  require(clock.get_source_latency(AnalysisType::kMin, TransType::kRise) == 0.4, "source latency missing");
  require(clock.get_network_latency(AnalysisType::kMax, TransType::kFall) == 0.8, "network latency missing");
  eval("remove_clock_latency CLK\nunset_clock_latency -source CLK");
  require(clock.get_source_latency(AnalysisType::kMin, TransType::kRise) == 0.0, "source latency not removed");
  require(clock.get_network_latency(AnalysisType::kMax, TransType::kFall) == 0.0, "network latency not removed");
  eval("set_clock_uncertainty 0.25 CLK\nremove_clock_uncertainty -setup CLK");
  require(clock.get_setup_uncertainty() == 0.0 && clock.get_hold_uncertainty() == 0.25, "uncertainty qualifier lost");
  eval("unset_clock_uncertainty -hold CLK");
  require(clock.get_hold_uncertainty() == 0.0, "hold uncertainty not removed");
  eval("set_clock_uncertainty -setup -rise_from CLK -fall_to VCLK 0.3");
  require(c.get_clock_uncertainty_list().size() == 1, "inter-clock uncertainty missing");
  const auto& pair = c.get_clock_uncertainty_list().front();
  require(
      pair.get_value() == 0.3 && pair.get_from_trans_type() == TransType::kRise
          && pair.get_to_trans_type() == TransType::kFall && pair.get_setup() && !pair.get_hold(),
      "uncertainty edges lost"
  );
  eval("unset_clock_uncertainty -setup -rise_from CLK -fall_to VCLK");
  require(c.get_clock_uncertainty_list().empty(), "inter-clock uncertainty not removed");
  reject("remove_clock_latency -source missing");
  reject("remove_clock_uncertainty -from CLK");
  reject("unset_clock_uncertainty -from CLK -rise_from VCLK -to CLK");
}

void testIoDelaysAndFanout()
{
  auto& c = PWDM.getDatabase().get_timing_constraint();
  eval("set_input_delay -max -rise -clock CLK 2 in\nset_input_delay -max -rise -clock VCLK -add_delay 3 in");
  const auto& delays = c.get_port_constraint_map().at("in").get_input_delay_list();
  require(
      std::count_if(
          delays.begin(),
          delays.end(),
          [](const auto& delay) {
            return delay.get_analysis_type() == AnalysisType::kMax && delay.get_trans_type() == TransType::kRise;
          }
      ) == 2,
      "add_delay did not retain both clock references"
  );
  eval("set_input_delay -max -rise -clock CLK 4 in");
  require(
      std::count_if(
          delays.begin(),
          delays.end(),
          [](const auto& delay) {
            return delay.get_analysis_type() == AnalysisType::kMax && delay.get_trans_type() == TransType::kRise
                   && delay.get_clock_name() == "CLK" && delay.get_delay() == 4;
          }
      ) == 1,
      "input delay replacement failed"
  );
  eval("set_output_delay -min -fall -clock VCLK -clock_fall -source_latency_included -0.2 out");
  const auto& outputs = c.get_port_constraint_map().at("out").get_output_delay_list();
  require(
      std::any_of(
          outputs.begin(),
          outputs.end(),
          [](const auto& d) {
            return d.get_clock_name() == "VCLK" && d.get_clock_trans_type() == TransType::kFall
                   && d.get_trans_type() == TransType::kFall && d.get_source_latency_included()
                   && d.get_delay() == -0.2;
          }
      ),
      "output delay qualifiers lost"
  );
  eval("set_max_fanout 24 [current_design]\nset_max_fanout 8 [get_ports in]");
  require(c.get_max_fanout() == 24 && c.get_port_max_fanout_map().at("in") == 8, "fanout constraint not retained");
  reject("set_input_delay -clock_fall 2 in");
  reject("set_input_delay -reference_pin clk -source_latency_included 2 in");
  reject("set_max_fanout 7 out");
  require(c.get_max_fanout() == 24, "invalid fanout changed design limit");
}

void testGroupsAndExceptions()
{
  auto& c = PWDM.getDatabase().get_timing_constraint();
  eval("set_clock_groups -asynchronous -name domains -group CLK -group VCLK");
  require(
      c.get_clock_group_list().size() == 1 && c.get_clock_group_list().front().get_groups().size() == 2,
      "repeated clock groups lost"
  );
  eval("set_false_path -setup -from in -rise_through clk -fall_through out -to out");
  require(c.get_path_exception_list().size() == 1, "false path not stored");
  const auto& path = c.get_path_exception_list().front();
  require(
      path.get_through_list().size() == 2 && path.get_through_list()[0].get_trans_type() == TransType::kRise
          && path.get_through_list()[1].get_trans_type() == TransType::kFall,
      "through order lost"
  );
  eval("reset_path -setup -from in");
  require(c.get_path_exception_list().empty(), "reset_path did not remove setup exception");
  eval("set_max_delay -ignore_clock_latency 5 -from in -to out\nset_min_delay 0.2 -from in -to out");
  require(
      c.get_path_exception_list().size() == 2 && c.get_path_exception_list()[0].get_delay() == 5
          && c.get_path_exception_list()[0].get_ignore_clock_latency()
          && c.get_path_exception_list()[1].get_type() == TimingExceptionType::kMinDelay,
      "path delay not stored"
  );
  eval("unset_path_exceptions -from in");
  require(c.get_path_exception_list().empty(), "unset_path_exceptions failed");
  eval("set_multicycle_path 3 -setup -start -from in -to out");
  require(
      c.get_path_exception_list().size() == 1 && c.get_path_exception_list().front().get_setup_multiplier() == 3
          && !c.get_path_exception_list().front().get_setup_use_end_clock(),
      "multicycle semantics lost"
  );
  const auto count = c.get_path_exception_list().size();
  reject("set_false_path -from missing");
  reject("set_multicycle_path 3 -start -end -from in");
  require(c.get_path_exception_list().size() == count, "invalid exception changed state");
  eval("reset_path -from in");
  require(c.get_path_exception_list().empty(), "reset multicycle failed");
}

void testPowerConstraintsAndQueries()
{
  auto& db = PWDM.getDatabase();
  auto& cell = db.get_timing_library().get_cell_map().at("BUF");
  cell.set_area(2.5);
  TimingCellArc arc;
  arc.set_source_port("A");
  arc.set_sink_port("Y");
  cell.get_cell_arc_list().push_back(arc);
  eval(R"TCL(
    if {[get_lib_cells -filter {area > 2} testlib/*] ne "testlib/BUF"} {error "area filter failed"}
    if {[get_libs -regexp -nocase TESTLIB] ne "testlib"} {error "library regexp failed"}
    if {[get_lib testlib] ne "testlib"} {error "library alias failed"}
    if {[get_port in] ne "in"} {error "port alias failed"}
    if {[get_clock CLK] ne "CLK"} {error "clock alias failed"}
    if {[get_object_name [get_ports in]] ne "in"} {error "object name failed"}
    if {[current_design] ne "sdc_test"} {error "current design failed"}
    if {[remove_from_collection [all_inputs] [get_ports clk]] ne "in"} {error "input collection failed"}
    create_generated_clock -name DIV -source clk -divide_by 2 in
    if {[get_generated_clocks] ne "DIV"} {error "generated clock query failed"}
    set_driving_cell -lib_cell BUF -library [get_libs testlib] -pin Y -from_pin A in
    set_case_analysis 0 in
  )TCL");
  auto& c = db.get_timing_constraint();
  require(c.get_clock_map().at("DIV").get_period() == 20, "generated period changed");
  require(c.get_case_analysis_map().at("in") == TimingCaseValue::kZero, "case analysis lost");
  require(
      c.get_port_constraint_map().at("in").get_has_driving_cell(AnalysisType::kMax, TransType::kRise),
      "driver constraint lost"
  );
  eval("set_input_transition -rise 0.6 in\nset_load -rise 0.7 out");
  require(
      !c.get_port_constraint_map().at("in").get_has_driving_cell(AnalysisType::kMax, TransType::kRise),
      "input slew failed to override driver"
  );
  require(
      c.get_port_constraint_map().at("in").get_has_driving_cell(AnalysisType::kMax, TransType::kFall),
      "rise slew overwrote fall driver"
  );
  require(c.get_port_constraint_map().at("out").get_load(AnalysisType::kMax, TransType::kRise) == 0.7, "load lost");
  reject("unknown_sdc_command");
  eval("set_clock_transition 0.4 CLK");
  require(
      c.get_clock_map().at("CLK").get_transition_map().at(AnalysisType::kMax).at(TransType::kRise) == 0.4,
      "interpreter did not recover after an error"
  );
}

void testObjectRelations()
{
  auto& db = PWDM.getDatabase();
  auto& instance = db.get_instance_map()["u"];
  instance.set_cell_name("BUF");
  instance.get_pin_name_list() = {"u:A", "u:Y"};
  for (const std::string name : {"A", "Y"}) {
    auto& pin = db.get_pin_map()["u:" + name];
    pin.set_pin_name(name);
    pin.set_instance_name("u");
    pin.set_direction(name == "A" ? PinDirection::kInput : PinDirection::kOutput);
    pin.set_net_name(name == "A" ? "input_net" : "output_net");
  }
  db.get_pin_map().at("in").set_net_name("input_net");
  db.get_pin_map().at("out").set_net_name("output_net");
  auto& input = db.get_net_map()["input_net"];
  input.get_pin_name_list() = {"in", "u:A"};
  input.get_driver_pin_list() = {"in"};
  input.get_load_pin_list() = {"u:A"};
  auto& output = db.get_net_map()["output_net"];
  output.get_pin_name_list() = {"u:Y", "out"};
  output.get_driver_pin_list() = {"u:Y"};
  output.get_load_pin_list() = {"out"};
  eval(R"TCL(
    if {[get_cell u] ne "u" || [get_cells u] ne "u"} {error "cell query failed"}
    if {[get_net input_net] ne "input_net" || [get_nets input_net] ne "input_net"} {error "net query failed"}
    if {[get_pins u/A] ne "u:A"} {error "internal pin query failed"}
    if {[get_full_name [get_pins u/A]] ne "u/A"} {error "pin full name failed"}
    if {[get_name [get_pins u/A]] ne "A"} {error "pin leaf name failed"}
    if {[get_lib_cells -quiet -of_objects {}] ne {}} {error "empty related query expanded"}
    if {[get_lib_cells -of_objects [get_cells u]] ne "testlib/BUF"} {error "instance library mapping failed"}
    if {[get_lib_pins -of_objects [get_pins u/A]] ne "testlib/BUF/A"} {error "pin library mapping failed"}
    if {[llength [get_fanin -to out]] == 0} {error "fanin missing"}
    if {[llength [get_fanout -from in]] == 0} {error "fanout missing"}
    set_max_transition -rise -clock_path 0.9 CLK
    set_max_capacitance -force 0.4 [get_ports out]
    set_timing_derate -late -cell_delay -data 1.1 [get_cells u]
    set_timing_derate -late -cell_delay -data -increment -0.05 [get_cells u]
  )TCL");
  auto& c = db.get_timing_constraint();
  require(
      c.electricalLimits().forcedLimit(ElectricalRule::kCapacitance, "out", true) == 0.4,
      "forced cap limit missing"
  );
  require(
      std::abs(
          c.timingDerates()
              .factor(AnalysisType::kMax, false, TransType::kRise, DerateDelay::kCell, {"u", "testlib/BUF", ""})
          - 1.05
      ) < 1e-12,
      "derate increment lost"
  );
}

void testLazyLibraryQueries()
{
  auto& lib = PWDM.getDatabase().get_timing_library();
  lib.set_cell_wrap_resolver([&](const std::string& name) {
    if (name != "UNUSED")
      return false;
    auto& cell = lib.get_cell_map()[name];
    cell.set_library_name("testlib");
    cell.get_port_map()["A"].set_is_input(true);
    return true;
  });
  eval("if {[get_lib_cells testlib/UNUSED] ne {testlib/UNUSED}} {error {uninstantiated library cell missing}}");
  lib.set_cell_wrap_resolver({});
}

void testUnitsAndCorners()
{
  eval(R"TCL(
    if {[set_units -time ns -capacitance pF] ne "1"} {error "unit check result missing"}
    if {[set_units -time ps] ne "0"} {error "unit mismatch not reported"}
    if {[all_delay_corners] ne "default"} {error "default corner missing"}
  )TCL");
  reject("set_units -time nonsense");
  reject("set_timing_derate -late -early 1.1");
  reject("set_max_transition -0.1 [current_design]");
  reject("set_max_capacitance -clock_path 0.2 [get_ports in]");
  reject("set_timing_derate -dynamic -late 1.1");
}

void testPinClockConstraints()
{
  eval(R"TCL(
    set_clock_latency -clock CLK 1 [get_pins u/A]
    set_clock_uncertainty -setup 0.3 [get_pins u/A]
    set_propagated_clock [get_pins u/A]
    unset_propagated_clock [get_pins u/A]
    remove_clock_latency -clock CLK [get_pins u/A]
    unset_clock_uncertainty -setup [get_pins u/A]
    set_false_path -from [get_ports -quiet missing_port]
    set_multicycle_path 3 -to [get_pins -quiet missing_pin]
    reset_path -from [get_ports -quiet missing_port]
  )TCL");
  eval("set_propagated_clock CLK\nset_clock_latency 0.5 CLK");
  require(
      !PWDM.getDatabase().get_timing_constraint().get_clock_map().at("CLK").get_is_propagated(),
      "network latency failed to restore ideal mode"
  );
  reject("set_clock_latency -clock CLK 1 CLK");
}

void testLiteralQueryObjects()
{
  auto& db = PWDM.getDatabase();
  for (const std::string name : {"literal*", "literalX"}) {
    auto& pin = db.get_pin_map()[name];
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_direction(PinDirection::kInput);
  }
  eval(R"TCL(
    create_clock -name {star*} -period 7
    create_clock -name starX -period 8
    set_clock_latency 0.7 [get_clocks {{star\*}}]
  )TCL");
  auto& clocks = db.get_timing_constraint().get_clock_map();
  require(
      clocks.at("starX").get_network_latency(AnalysisType::kMax, TransType::kRise) == 0,
      "typed literal clock was reinterpreted as glob"
  );
}

void testWriteSdc()
{
  const auto path = std::filesystem::temp_directory_path() / "ipw_sdc_roundtrip.tcl";
  eval(R"TCL(
    set_clock_latency -source -min -late 1.25 CLK
    set_clock_latency -source -min -early 0.75 CLK
    set_clock_latency -clock CLK -rise 0.35 [get_pins u/A]
    set_clock_uncertainty -setup 0.17 [get_pins u/A]
  )TCL");
  const auto exception_count = PWDM.getDatabase().get_timing_constraint().get_path_exception_list().size();
  eval("write_sdc " + path.string() + " -nosplit");
  auto& c = PWDM.getDatabase().get_timing_constraint();
  c = TimingConstraint();
  const int status = SdcCommand::getInst().evalScriptFile(path.string());
  std::filesystem::remove(path);
  require(status == TCL_OK, "exported SDC failed: " + SdcCommand::getInst().get_error_message());
  require(c.get_clock_map().at("CLK").get_period() == 10, "export lost clock");
  require(
      c.get_port_constraint_map().at("out").get_load(AnalysisType::kMax, TransType::kRise) == 0.7,
      "export lost load"
  );
  require(c.get_case_analysis_map().at("in") == TimingCaseValue::kZero, "export lost case analysis");
  require(c.get_clock_map().at("DIV").get_period() == 20, "export lost generated clock");
  require(c.get_path_exception_list().size() == exception_count, "export lost path exceptions");
  const auto& latency = c.get_clock_map().at("CLK").get_source_latency_map().at(AnalysisType::kMin);
  require(
      latency.at(ClockVariation::kEarly).at(TransType::kRise) == 0.75
          && latency.at(ClockVariation::kLate).at(TransType::kRise) == 1.25,
      "export conflated source latency variation with analysis type"
  );
  require(
      c.clockPinConstraints().at("u:A").at("CLK").network_latency.at(AnalysisType::kMax).at(TransType::kRise) == 0.35,
      "export lost pin latency"
  );
  require(c.clockPinConstraints().at("u:A").at("").setup_uncertainty == 0.17, "export lost pin uncertainty");
  require(
      c.get_clock_map().at("star*").get_network_latency(AnalysisType::kMax, TransType::kRise) == 0.7
          && c.get_clock_map().at("starX").get_network_latency(AnalysisType::kMax, TransType::kRise) == 0,
      "export expanded literal clock name"
  );
  reject("set_input_delay -clock [all_clocks] 1 in");
  reject("write_sdc");
  reject("write_sdc -unknown file");
}

int main()
{
  Logger::initInst();
  DataManager::initInst();
  SdcCommand::initInst();
  int failures = 0;
  buildDesign();
  for (auto test :
       {testVirtualPropagation,
        testLibraryQueries,
        testTimingValidation,
        testPostCtsFile,
        testLatencyAndUncertainty,
        testIoDelaysAndFanout,
        testGroupsAndExceptions,
        testPowerConstraintsAndQueries,
        testObjectRelations,
        testLazyLibraryQueries,
        testUnitsAndCorners,
        testPinClockConstraints,
        testLiteralQueryObjects,
        testWriteSdc}) {
    try {
      test();
    } catch (const std::exception& error) {
      std::cerr << error.what() << '\n';
      ++failures;
    }
  }
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
  return failures == 0 ? 0 : 1;
}
