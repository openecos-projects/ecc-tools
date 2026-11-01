// SPDX-License-Identifier: MulanPSL-2.0
#include "DataManager.hpp"
#include "Logger.hpp"
#include "SdcCommand.hpp"

using namespace ipw;

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void eval(const std::string& script)
{
  Tcl_Interp* interp = SdcCommand::getInst().getInterp();
  int32_t status = Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL);
  require(status == TCL_OK, "SDC failed: " + std::string(Tcl_GetStringResult(interp)));
}

void buildDesign()
{
  Database& database = PWDM.getDatabase();
  database.set_design_name("sdc_compatibility");
  for (std::string name : {"clk", "in", "out"}) {
    Pin& pin = database.get_pin_map()[name];
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_direction(name == "out" ? PinDirection::kOutput : PinDirection::kInput);
  }
}

void testGeneratedDefaultSdc()
{
  Database& database = PWDM.getDatabase();
  // Mirror ECC's generated default SDC, including its physical and virtual clock variants.
  for (bool virtual_clock : {false, true}) {
    database.get_timing_constraint() = TimingConstraint();
    eval(R"TCL(
      set sdc_version 2.1
      set_units -time ns -resistance kOhm -capacitance pF -voltage V -current mA
      set clk_freq_mhz 100
      set clk_period [expr {1000.0 / $clk_freq_mhz}]
    )TCL");
    if (virtual_clock) {
      eval(R"TCL(
        set clk_name __VIRTUAL_CLK__
        create_clock -name $clk_name -period $clk_period
        set data_inputs [all_inputs]
      )TCL");
    } else {
      eval(R"TCL(
        set clk_name clk
        set clk_port_name clk
        set clk_port [get_ports $clk_port_name]
        create_clock -name $clk_name -period $clk_period $clk_port
        set_propagated_clock [all_clocks]
        set data_inputs [remove_from_collection [all_inputs] $clk_port]
      )TCL");
    }
    eval(R"TCL(
      set_clock_latency -source 0.5 [get_clocks $clk_name]
      set_input_delay -max 3.0 -clock [get_clocks $clk_name] $data_inputs
      set_input_delay -min 1.0 -clock [get_clocks $clk_name] $data_inputs
      set_output_delay -max 1.0 -clock [get_clocks $clk_name] [all_outputs]
      set_load 0.2 [all_outputs]
      set_clock_uncertainty -setup 0.20 [all_clocks]
      set_clock_uncertainty -hold 0.25 [get_clocks $clk_name]
      set_clock_transition 0.15 [get_clocks $clk_name]
      set_input_transition 0.20 $data_inputs
      set_max_fanout 32 [current_design]
      set_max_transition [expr {$clk_period * 0.17}] -clock_path [get_clocks $clk_name]
      set_max_transition [expr {$clk_period * 0.34}] -data_path [get_clocks $clk_name]
      set_max_capacitance 0.15 [current_design]
      set_timing_derate -early 0.95 -clock -delay_corner [all_delay_corners]
      set_timing_derate -late 1.10 -clock -delay_corner [all_delay_corners]
      set_timing_derate -late 1.10 -data -delay_corner [all_delay_corners]
      set corners [all_delay_corners]
      if {$corners ne "default"} {error "default delay corner missing"}
      set_timing_derate -late 1.10 -data -delay_corner $corners
    )TCL");

    TimingConstraint& constraint = database.get_timing_constraint();
    require(constraint.get_clock_map().size() == 1, "clock count changed");
    TimingClock& clock = constraint.get_clock_map().begin()->second;
    require(
        clock.get_period() == 10.0 && clock.get_rise_edge() == 0.0 && clock.get_fall_edge() == 5.0,
        "default SDC clock values changed"
    );
    require(clock.get_is_propagated() == !virtual_clock, "clock propagation mode changed");
    require(
        clock.get_source_list() == (virtual_clock ? std::vector<std::string>() : std::vector<std::string>{"clk"}),
        "clock source changed"
    );
    require(constraint.get_port_constraint_map().size() == (virtual_clock ? 3 : 2), "unexpected port constraint");
    require(constraint.get_port_constraint_map().contains("clk") == virtual_clock, "data-input collection changed");
    for (AnalysisType analysis_type : {AnalysisType::kMax, AnalysisType::kMin}) {
      for (TransType trans_type : {TransType::kRise, TransType::kFall}) {
        require(clock.get_transition_map()[analysis_type][trans_type] == 0.15, "clock transition changed");
        require(
            constraint.get_port_constraint_map()["in"].get_input_transition(analysis_type, trans_type) == 0.20,
            "input transition changed"
        );
        require(
            constraint.get_port_constraint_map()["out"].get_load(analysis_type, trans_type) == 0.2,
            "output load changed"
        );
      }
    }
    require(database.get_arc_list().empty(), "syntax-only commands built a graph");
    require(constraint.get_case_analysis_map().empty(), "syntax-only commands changed logic constraints");
  }
}

void testCommandValidation()
{
  Tcl_Interp* interp = SdcCommand::getInst().getInterp();
  for (std::string script :
       {"all_delay_corners unexpected",
        "all_delay_corners -unknown",
        "unsupported_sdc_command",
        "set_clock_transition bad [all_clocks]",
        "set_load bad [get_ports out]"}) {
    require(Tcl_EvalEx(interp, script.c_str(), -1, TCL_EVAL_GLOBAL) == TCL_ERROR, "invalid syntax accepted: " + script);
  }
  eval(R"TCL(if {[all_delay_corners] ne "default"} {error {default corner was not restored after an error}})TCL");
  eval("set_input_transition 0.3 [get_ports in]");
  require(
      PWDM.getDatabase().get_timing_constraint().get_port_constraint_map()["in"].get_input_transition(
          AnalysisType::kMax,
          TransType::kRise
      ) == 0.3,
      "supported command stopped working after an error"
  );
}

int main()
{
  Logger::initInst();
  DataManager::initInst();
  SdcCommand::initInst();
  int32_t status = 0;
  try {
    buildDesign();
    testGeneratedDefaultSdc();
    testCommandValidation();
  } catch (const std::exception& error) {
    PWLOG.warn(Loc::current(), error.what());
    status = 1;
  }
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
  return status;
}
