// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "Logger.hpp"
#include "STAInterface.hpp"
#include "SdcTestDesign.hpp"
#include "SdcWriter.hpp"
#include "tcl_sta.h"

std::string interoperability_output;

std::string snapshot()
{
  std::ostringstream output;
  sdc::writeSdc(STADM.getDatabase(), output);
  return output.str();
}

void reload(const std::string& script)
{
  STADM.getDatabase().sdc() = Sdc{};
  SdcCommand::destroyInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  eval(script);
}

void testConstraintFamilies()
{
  eval(R"TCL(
    create_clock -name z_master -period 10 -waveform {1 4} -comment {master $comment} [get_ports clk]
    create_clock -name virtual -period 7
    create_generated_clock -name a_divided -source [get_ports clk] -master_clock z_master -divide_by 2 [get_pins cdc/source_reg/CK]
    create_generated_clock -name aa_shifted -source [get_pins cdc/source_reg/CK] -master_clock a_divided -edges {1 2 3} -edge_shift {1 2 1} [get_pins cdc/sink_reg/CK]
    set_propagated_clock [get_clocks a_divided]
    set_clock_latency -source -early -rise 0 [get_clocks z_master]
    set_clock_latency -max -fall 1.25 [get_clocks z_master]
    set_clock_transition -min -rise 0.12 [get_clocks z_master]
    set_clock_uncertainty -setup 0.2 [get_clocks z_master]
    set_clock_uncertainty -hold 0.1 [get_clocks z_master]
    set_clock_uncertainty -setup -rise_from [get_clocks z_master] -fall_to [get_clocks a_divided] 0.35
    set_input_delay -min -fall -clock z_master -clock_fall -source_latency_included -network_latency_included -1.5 [get_ports in]
    set_input_delay -max -clock z_master 2 [get_ports in]
    set_input_delay -max -add_delay -clock virtual 3 [get_ports in]
    set_output_delay -clock z_master -reference_pin [get_pins cdc/source_reg/CK] 1.5 [get_ports out]
    set_driving_cell -lib_cell COMB -from_pin A -pin Y -input_transition_rise 0.03 -input_transition_fall 0.05 [get_ports in]
    set_input_transition -min -rise 0 [get_ports in]
    set_load -pin_load -max -rise 0.5 [get_ports out]
    set_load -wire_load -max -rise 0.25 [get_ports out]
    set_load -min 0 [get_nets sink_net]
    set_case_analysis 1 [get_ports reset]
    set_max_fanout 20 [current_design]
    set_max_fanout 0 [get_ports in]
    set_clock_groups -name groups -comment {test groups} -asynchronous -allow_paths -group [get_clocks z_master] -group [get_clocks virtual]
    set_false_path -setup -rise_from [get_clocks z_master] -through [get_pins cdc/buf/A] -fall_through [get_pins cdc/reconv/A] -to [get_clocks virtual] -comment {false path}
    set_max_delay -ignore_clock_latency -fall 5 -from [get_ports in] -to [get_ports out]
    set_min_delay -0.2 -from [get_ports in] -to [get_ports out]
    set_multicycle_path -setup -start 3 -from [get_clocks z_master] -to [get_clocks a_divided]
    set_multicycle_path -hold -end 2 -from [get_clocks z_master] -to [get_clocks a_divided]
    set_clock_latency -source 9 [get_clocks virtual]
    remove_clock_latency -source [get_clocks virtual]
  )TCL");
  const auto first = snapshot();
  if (!interoperability_output.empty())
    sdc::writeSdcFile(STADM.getDatabase(), interoperability_output);
  reload(first);
  auto& c = STADM.getDatabase().sdc();
  require(c.get_clock_map().size() == 4, "export lost clocks");
  require(c.get_clock_map().at("a_divided").get_period() == 20, "generated clock division changed");
  require(c.get_clock_map().at("aa_shifted").get_rise_edge() == 2, "generated clock shift changed");
  require(c.get_clock_map().at("a_divided").get_is_propagated(), "propagated clock state lost");
  require(c.get_clock_map().at("z_master").get_source_latency_map().at(AnalysisType::kMin).at(ClockVariation::kEarly).at(TransType::kRise) == 0,
          "explicit zero latency lost");
  require(c.get_clock_map().at("virtual").get_source_latency_map().empty(), "removed latency resurrected");
  require(c.get_clock_uncertainty_list().size() == 1 && c.get_clock_uncertainty_list()[0].get_to_trans_type() == TransType::kFall,
          "inter-clock uncertainty selector lost");
  auto& input = c.get_port_constraint_map().at("in");
  require(input.get_input_delay_list().size() == 5 && input.get_input_delay_min() == -1.5 && input.get_input_delay_max() == 3,
          "I/O delay multiplicity or value changed");
  require(input.get_has_input_transition(AnalysisType::kMin, TransType::kRise) && input.get_input_transition(AnalysisType::kMin, TransType::kRise) == 0
              && !input.get_has_driving_cell(AnalysisType::kMax, TransType::kFall),
          "input drive precedence changed");
  require(c.get_port_constraint_map().at("out").get_load(AnalysisType::kMax, TransType::kRise) == 0.75, "pin/wire loads changed");
  require(c.get_net_load_map().at("sink_net").has(AnalysisType::kMin, TransType::kRise), "zero net load lost");
  require(c.get_max_fanout() == 20 && c.get_port_max_fanout_map().at("in") == 0, "fanout constraints lost");
  require(c.get_case_analysis_map().at("reset") == TimingCaseValue::kOne, "case analysis lost");
  require(c.get_clock_group_list().size() == 1 && c.get_clock_group_list()[0].get_allow_paths(), "clock group options lost");
  require(c.get_path_exception_list().size() == 5 && c.get_path_exception_list()[0].get_through_list().size() == 2,
          "exceptions or ordered through clauses lost");
  require(first == snapshot(), "constraint snapshot is not stable after reimport");
}

void testNamesAndIdentity()
{
  STADM.getDatabase().sdc() = Sdc{};
  for (const std::string name : {"data[0]", "data0", "literal*", "literal_suffix", "space brace} $d;[x]\\tail"})
    addPin("", name, PinDirection::kInput);
  addPin("", "package", PinDirection::kInout);
  addNet("package", {"package"});
  eval(R"TCL(
    create_clock -name {clock with space} -period 10 [get_ports clk]
    create_generated_clock -name {generated with space} -source [get_ports clk] -master_clock {clock with space} -divide_by 2 [get_pins cdc/source_reg/CK]
    set_input_delay -clock [get_clocks {{clock with space}}] 1 [get_ports -exact {data[0]}]
    set_input_transition 0.2 [get_ports -exact {literal*}]

  )TCL");
  const std::string special = "space brace} $d;[x]\\tail";
  Tcl_SetVar(SdcCommand::getInst().getInterp(), "special", special.c_str(), TCL_GLOBAL_ONLY);
  eval("set_input_transition 0.3 [get_ports -exact [list $special]]");
  eval("set_load 0.7 [get_ports package]; set_load 0.2 [get_nets package]");
  const auto first = snapshot();
  reload(first);
  auto& c = STADM.getDatabase().sdc();
  require(c.get_clock_map().at("generated with space").get_period() == 20, "spaced clock name changed");
  require(c.get_port_constraint_map().at("data[0]").get_input_delay_max() == 1, "bus name changed");
  require(!c.get_port_constraint_map().contains("data0"), "bus brackets matched a different port");
  require(c.get_port_constraint_map().at("literal*").get_input_transition() == 0.2 && !c.get_port_constraint_map().contains("literal_suffix"),
          "literal wildcard became a pattern");
  require(c.get_port_constraint_map().at(special).get_input_transition() == 0.3, "Tcl metacharacters in an object name changed");
  require(c.get_port_constraint_map().at("package").get_load(AnalysisType::kMax, TransType::kRise) == 0.7
              && c.get_net_load_map().at("package").get(AnalysisType::kMax, TransType::kRise) == 0.2,
          "same-name port and net loads became conflated");
  require(first == snapshot(), "special-name snapshot changed after reimport");
}

void testGeneratedSnapshotAfterMasterChange()
{
  STADM.getDatabase().sdc() = Sdc{};
  eval(R"TCL(
    create_clock -name master -period 10 [get_ports clk]
    create_generated_clock -name gen -master_clock master -source [get_ports clk] -divide_by 2 [get_pins cdc/source_reg/CK]
    create_clock -name master -period 20 [get_ports clk]
  )TCL");
  const auto exported = snapshot();
  reload(exported);
  require(STADM.getDatabase().sdc().get_clock_map().at("gen").get_period() == 40, "generated clock did not follow redefined master through export");
}

void testMixedObjectCollections()
{
  STADM.getDatabase().sdc() = Sdc{};
  eval(R"TCL(
    create_clock -name {clock with space} -period 10 [concat [get_ports clk] [get_pins {{cell with space/A}}]]
    set_false_path -from [concat [get_ports in] [get_clocks {{clock with space}}]] -to [get_ports out]
  )TCL");
  const auto exported = snapshot();
  reload(exported);
  auto& c = STADM.getDatabase().sdc();
  require(c.get_clock_map().at("clock with space").get_source_objects().size() == 2, "mixed clock sources changed");
  require(c.get_path_exception_list()[0].get_from_objects().size() == 2, "mixed exception objects changed");
  require(exported == snapshot(), "mixed collection snapshot changed after reimport");
}

void testRemovedInterClockUncertainty()
{
  STADM.getDatabase().sdc() = Sdc{};
  eval("create_clock -name launch -period 10; create_clock -name capture -period 12");
  eval("set_clock_uncertainty -from [get_clocks launch] -to [get_clocks capture] 0.7");
  eval("remove_clock_uncertainty -from [get_clocks launch] -to [get_clocks capture]");
  reload(snapshot());
  require(STADM.getDatabase().sdc().resolve_clock_uncertainty("launch", TransType::kRise, "capture", TransType::kRise, AnalysisType::kMax) == 0,
          "export resurrected removed inter-clock uncertainty");
}

void testLevelSensitiveDelay()
{
  STADM.getDatabase().sdc() = Sdc{};
  eval("create_clock -name clock -period 10 [get_ports clk]");
  eval("set_output_delay -clock clock -level_sensitive 1 [get_ports out]");
  reload(snapshot());
  require(STADM.getDatabase().sdc().get_port_constraint_map().at("out").get_output_delay_list()[0].get_level_sensitive(),
          "output delay level-sensitive qualifier lost");
}

void testModelIoDelay()
{
  STADM.getDatabase().sdc() = Sdc{};
  auto& port = STADM.getDatabase().sdc().get_port_constraint_map()["in"];
  port.set_port_name("in");
  std::vector<TimingIoDelay> delays;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    TimingIoDelay delay;
    delay.set_trans_type(edge);
    delay.set_delay(2.5);
    delays.push_back(delay);
  }
  port.set_input_delays(delays, false);
  const auto exported = snapshot();
  reload(exported);
  auto& ports = STADM.getDatabase().sdc().get_port_constraint_map();
  require(ports.contains("in") && ports.at("in").get_input_delay_max() == 2.5, "model I/O delay was omitted");
}

void testFileAndCommandErrors()
{
  require(SdcCommand::getInst().evalString("info commands output_sdc") == TCL_OK, "command lookup failed");
  require(std::string(Tcl_GetStringResult(SdcCommand::getInst().getInterp())).empty(), "obsolete output_sdc command registered");
  require(SdcCommand::getInst().evalString("info commands write_sdc") == TCL_OK, "command lookup failed");
  require(std::string(Tcl_GetStringResult(SdcCommand::getInst().getInterp())) == "write_sdc", "write_sdc command missing");
  const auto exportFails = [](const std::filesystem::path& path) {
    try {
      sdc::writeSdcFile(STADM.getDatabase(), path.string());
    } catch (const std::exception&) {
      return true;
    }
    return false;
  };
  const auto dir = std::filesystem::temp_directory_path() / ("ista-sdc-writer-" + std::to_string(getpid()));
  std::filesystem::create_directory(dir);
  const auto path = dir / "result.sdc";
  auto* interp = SdcCommand::getInst().getInterp();
  Tcl_SetVar(interp, "destination", path.c_str(), TCL_GLOBAL_ONLY);
  eval("write_sdc -nosplit $destination");
  require(std::string(Tcl_GetStringResult(interp)) == "1", "write_sdc success result is not 1");
  require(std::filesystem::file_size(path) > 0, "file writer produced no data");
  std::ifstream saved(path);
  const std::string original((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
  require(original == snapshot(), "command output differs from the serializer");
  for (const auto* command : {"write_sdc", "write_sdc {}", "write_sdc -nosplit", "write_sdc $destination extra", "write_sdc -version 1.9 $destination",
                              "write_sdc -compress gzip $destination", "write_sdc -include exceptions $destination", "write_sdc -bogus"}) {
    require(SdcCommand::getInst().evalString(command) == TCL_ERROR, "invalid write_sdc command accepted");
  }
  eval("write_sdc $destination -nosplit");
  eval("write_sdc $destination");
  const auto spaced_path = dir / "spaces $[x];.sdc";
  Tcl_SetVar(interp, "destination", spaced_path.c_str(), TCL_GLOBAL_ONLY);
  eval("write_sdc $destination");
  require(std::filesystem::file_size(spaced_path) > 0, "write_sdc changed literal path characters");
  std::filesystem::remove(spaced_path);
  require(exportFails(dir / "missing" / "result.sdc"), "output into a missing directory succeeded");
  require(exportFails(dir), "output overwrote a directory");
  const auto source = dir / "constraints.tcl";
  std::ofstream(source) << "proc transition_value {} { return [expr {0.1 + 0.2}] }\n"
                           "set_input_transition [transition_value] [get_ports in]\n";
  eval("source {" + source.string() + "}");
  reload(snapshot());
  require(std::abs(STADM.getDatabase().sdc().get_port_constraint_map().at("in").get_input_transition() - 0.3) < 1e-12,
          "sourced procedure result did not survive export");
  std::filesystem::remove(source);
  std::ofstream(path) << "sentinel";
  auto& clocks = STADM.getDatabase().sdc().get_clock_map();
  TimingClock invalid;
  invalid.set_clock_name("invalid");
  invalid.set_period(std::numeric_limits<double>::quiet_NaN());
  clocks["invalid"] = invalid;
  require(exportFails(path), "non-finite clock export succeeded");
  std::ifstream input(path);
  std::string retained;
  input >> retained;
  require(retained == "sentinel", "failed export overwrote existing output");
  clocks.erase("invalid");
  // Replacing the final target deletes the old clock, never creates a virtual
  // generated clock. Other malformed exports above still preserve the file.
  STADM.getDatabase().sdc() = Sdc{};
  eval("create_clock -name master -period 10 [get_ports clk]");
  eval("create_generated_clock -name displaced -source [get_ports clk] -master_clock master -divide_by 2 [get_pins cdc/source_reg/CK]");
  eval("create_clock -name replacement -period 5 [get_pins cdc/source_reg/CK]");
  require(!STADM.getDatabase().sdc().get_clock_map().contains("displaced"), "displaced clock became virtual");
  require(!exportFails(path), "valid replacement clock could not be exported");

  for (const auto& entry : std::filesystem::directory_iterator(dir))
    require(entry.path() == path, "temporary output file leaked");
  std::filesystem::remove_all(dir);
}

void testPublicCommand()
{
  auto* engine = ecc::ScriptEngine::getOrCreateInstance();
  Tcl_CreateObjCommand(engine->get_interp(), "write_sdc", &tcl::writeSdcCommand, nullptr, nullptr);
  require(engine->evalString("write_sdc unused.sdc") == TCL_ERROR, "public command accepted uninitialized STA");
  require(std::string(engine->getResult()).find("initialized") != std::string::npos, "public command lost initialization error");
  require(engine->evalString("write_sdc {}") == TCL_ERROR, "public command accepted empty path");
  require(engine->evalString("write_sdc") == TCL_ERROR, "public command reused its previous path");
  ecc::ScriptEngine::destroyInstance();
  STAInterface::destroyInst();
}

int main(int argc, char** argv)
{
  if (argc > 1)
    interoperability_output = argv[1];
  Logger::initInst();
  DataManager::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  int status = 0;
  try {
    buildDesign();
    testConstraintFamilies();
    testNamesAndIdentity();
    testGeneratedSnapshotAfterMasterChange();
    testMixedObjectCollections();
    testRemovedInterClockUncertainty();
    testLevelSensitiveDelay();
    testModelIoDelay();
    testFileAndCommandErrors();
    testPublicCommand();
    STALOG.info(Loc::current(), "SDC output tests passed");
  } catch (const std::exception& error) {
    STALOG.warn(Loc::current(), error.what());
    status = 1;
  }
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
  return status;
}
