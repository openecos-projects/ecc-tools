// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "SdcTestDesign.hpp"
#include "DelayCalculator.hpp"
#include "SdcWriter.hpp"
#include "TimingAnalyzer.hpp"

namespace {
void near(double actual, double expected, const char* message)
{
  require(std::abs(actual - expected) < 1e-10, message);
}
std::string effectiveSdc(Database& db)
{
  std::ostringstream output;
  sdc::writeSdc(db, output);
  return output.str();
}
auto pathValues(Database& db)
{
  std::vector<std::tuple<std::string, std::string, AnalysisType, TransType, double, double, double>> values;
  for (auto& group : db.pathGroups())
    for (auto& [endpoint, paths] : group.endpointPaths())
      for (auto& path : paths)
        values.emplace_back(path.get_start_point(), path.get_end_point(), path.get_analysis_type(), path.get_trans_type(),
                            path.get_path_delay(), path.get_required_time(), path.get_slack());
  return values;
}
void makeDesign()
{
  auto& db = STADM.getDatabase();
  db.network().setName("rebuild");
  auto& cell = db.liberty().get_cell_map()["BUF"];
  cell.get_port_map()["A"].set_is_input(true);
  cell.get_port_map()["A"].set_capacitance(.02);
  cell.get_port_map()["Y"].set_is_output(true);
  TimingCellArc arc;
  arc.set_source_port("A");
  arc.set_sink_port("Y");
  TimingArc table;
  table.set_sense(TimingArcSense::kPositive);
  for (const auto edge : {TransType::kRise, TransType::kFall}) {
    auto& delay = table.get_delay_table_map()[edge];
    delay.set_variable_type1(TimingTableVariableType::kOutputCapacitance);
    delay.set_axis_list({{0., 1.}});
    delay.set_value_list({.1, 1.1});
    table.get_slew_table_map()[edge].set_value_list({edge == TransType::kRise ? .05 : .08});
  }
  arc.get_timing_arc_list().push_back(table);
  cell.get_cell_arc_list().push_back(arc);
  addInstance("data", "BUF", false);
  addInstance("clock", "BUF", false);
  for (const auto* name : {"in", "clk"})
    addPin("", name, PinDirection::kInput);
  for (const auto* name : {"out", "clkout"})
    addPin("", name, PinDirection::kOutput);
  addNet("in", {"in", "data:A"});
  addNet("out", {"data:Y", "out"});
  addNet("clk", {"clk", "clock:A"});
  addNet("clkout", {"clock:Y", "clkout"});
}
}  // namespace
int main(int, char** argv)
{
  Tcl_FindExecutable(argv[0]);
  Logger::initInst();
  DataManager::initInst();
  makeDesign();
  auto& db = STADM.getDatabase();
  Config config;
  config.thread_number = 1;
  DelayCalculator delay(db, config);
  SdcCommand interpreter;
  sdc::registerSdcCommands(interpreter);
  const auto run = [&](const char* text) {
    require(interpreter.evalString(text) == TCL_OK, std::string(text) + ": " + Tcl_GetStringResult(interpreter.getInterp()));
  };
  run("create_clock -name M -period 10 [get_ports clk]; "
      "set_input_delay -clock M 1 [get_ports in]; set_output_delay -clock M 2 [get_ports out]; "
      "set_input_transition .1 [get_ports {in clk}]; set_load .2 [get_ports out]; "
      "set_load -wire_load .3 [get_ports out]; set_clock_uncertainty .04 [get_clocks M]; "
      "set_timing_derate -late -cell_delay -data 1.1; set_max_transition .5 [current_design]; "
      "set_max_capacitance 1 [current_design]");
  require(db.graph().vertices().empty(), "fixture applied SDC after graph construction");
  for (int stage = 0; stage < 5; ++stage) {
    if (stage == 1)
      run("set_propagated_clock [get_clocks M]");
    if (stage == 2)
      run("remove_propagated_clock [get_clocks M]; set_load .7 [get_ports out]");
    if (stage == 3)
      run("set_load .8 [get_nets out]; set_false_path -from [get_ports in] -to [get_ports out]");
    if (stage == 4)
      run("reset_path -from [get_ports in] -to [get_ports out]; set_case_analysis 1 [get_ports in]");
    const auto constraints = effectiveSdc(db);
    Database fresh;
    fresh.network() = db.network();
    fresh.liberty() = db.liberty();
    fresh.sdc() = db.sdc();
    DelayCalculator fresh_delay(fresh, config);
    TimingAnalyzer(fresh, fresh_delay).analyze();
    for (int repeat = 0; repeat < 2; ++repeat) {
      TimingAnalyzer(db, delay).analyze();
      require(effectiveSdc(db) == constraints, "graph rebuild changed effective SDC");
      require(pathValues(db) == pathValues(fresh), "rebuilt timing differs from a fresh analysis of the same constraints");
      const double load = stage >= 3 ? 1.8 : stage == 2 ? 1. : .5;
      near(delay.pinLoad("data:Y", AnalysisType::kMax, TransType::kRise), load, "set_load lost or duplicated during rebuild");
      const auto& clock = db.graph().points().at("clock:Y").get_clock_state("M");
      near(clock.arrival_map.at(AnalysisType::kMax).at(TransType::kRise), stage == 1 ? .1 : 0., "propagated/ideal clock mode lost during rebuild");
      const auto& source = db.graph().driverPoint("clk").get_clock_state("M");
      for (const auto mode : {AnalysisType::kMin, AnalysisType::kMax})
        for (const auto edge : {TransType::kRise, TransType::kFall}) {
          near(source.waveforms.at(edge).physicalSlew(mode, edge).value(), .1, "clock source ignored set_input_transition");
          near(source.slew_map.at(mode).at(edge), stage == 1 ? .1 : 0., "physical port slew changed ideal clock transition");
        }
      if (stage < 3) {
        bool found_setup = false;
        for (const auto& group : db.pathGroups())
          for (auto& [endpoint, paths] : group.endpointPaths())
            for (const auto& path : paths)
              if (path.get_analysis_type() == AnalysisType::kMax) {
                near(path.get_slack(), 7. - .04 - (load + .1) * 1.1, "I/O constraints or derate changed during rebuild");
                found_setup = true;
              }
        require(found_setup, "missing constrained setup path after rebuild");
      }
      if (stage == 3)
        require(db.pathStats().path_count == 0, "false path disappeared during graph rebuild");
      if (stage == 4) {
        require(db.graph().simValues().value("in") == TimingCaseValue::kOne, "case analysis disappeared during graph rebuild");
        require(db.graph().simValues().value("data:A") == TimingCaseValue::kOne, "case analysis did not propagate to receiver");
        require(db.pathStats().path_count == 0, "static input produced a timing path after graph rebuild");
      }
    }
  }
  // Drive the clock port through a library cell, then replace that drive with
  // an explicit port waveform. Both consumers must use the same electrical input.
  auto& port = db.sdc().get_port_constraint_map().at("clk");
  port = TimingPortConstraint{};
  TimingDrivingCell driver;
  driver.set_cell_name("BUF");
  driver.set_from_pin("A");
  driver.set_to_pin("Y");
  for (const auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (const auto edge : {TransType::kRise, TransType::kFall})
      port.set_driving_cell(mode, edge, driver);
  run("set_propagated_clock [get_clocks M]");
  for (int replace = 0; replace < 2; ++replace) {
    if (replace)
      run("set_input_transition .12 [get_ports clk]");
    TimingAnalyzer(db, delay).analyze();
    for (const auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (const auto edge : {TransType::kRise, TransType::kFall}) {
        const auto expected = replace ? .12 : edge == TransType::kRise ? .05 : .08;
        near(db.graph().driverPoint("clk").get_clock_state("M").slew_map.at(mode).at(edge), expected, "clock input drive selection");
        near(db.graph().points().at("clock:A").get_clock_state("M").slew_map.at(mode).at(edge), expected, "clock receiver lost port waveform");
        near(db.graph().driverPoint("clk").get_clock_state("M").arrival_map.at(mode).at(edge), replace ? 0. : .02,
             "clock root lost driving-cell load-dependent delay");
      }
  }
  DataManager::destroyInst();
  Logger::destroyInst();
}
