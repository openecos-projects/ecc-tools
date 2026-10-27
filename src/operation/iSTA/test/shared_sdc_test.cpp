// SPDX-License-Identifier: MulanPSL-2.0
#include "SdcTestDesign.hpp"

int main(int, char** argv)
{
  Tcl_FindExecutable(argv[0]);
  Logger::initInst();
  DataManager::initInst();
  {
    SdcCommand interpreter;
    sdc::registerSdcCommands(interpreter);
    const auto run
        = [&](const std::string& script) { require(interpreter.evalString(script) == TCL_OK, script + ": " + Tcl_GetStringResult(interpreter.getInterp())); };
    run("get_ports -quiet missing");
    require(std::string(Tcl_GetStringResult(interpreter.getInterp())).empty(), "normal empty-design query became a declaration");
    interpreter.setIgnoreUnknownCommands(true);
    run("create_clock -name M -period 10 [list [get_pins -leaf z/Q] [get_ports a] [get_nets n] [get_ports a]]");
    auto& clocks = STADM.getDatabase().sdc().get_clock_map();
    require(clocks.at("M").get_source_objects() == std::vector<std::string>{"a", "n", "z/Q"}, "source normalization changed");
    require(clocks.at("M").get_source_kinds() == std::vector<ClockSourceKind>{ClockSourceKind::kPort, ClockSourceKind::kNet, ClockSourceKind::kPin},
            "source kinds lost during sorting or deduplication");
    run("create_generated_clock -name G -source [get_pins -leaf z/Q] -divide_by 2 [list [get_pins -leaf x/Q] [get_ports b]]");
    run("create_clock -name N -period 5 [get_ports [get_ports a]]");
    require(clocks.at("M").get_source_objects() == std::vector<std::string>{"n", "z/Q"}, "clock replacement discarded retained sources");
    require(clocks.at("M").get_source_kinds() == std::vector<ClockSourceKind>{ClockSourceKind::kNet, ClockSourceKind::kPin},
            "clock replacement misaligned kinds");
    run("create_clock -name M -period 12 [list [get_pins -leaf z/Q] [get_nets n]]");
    const auto& generated = clocks.at("G");
    require(generated.get_period() == 24, "generated clock was not refreshed");
    require(generated.get_source_kinds() == std::vector<ClockSourceKind>{ClockSourceKind::kPort, ClockSourceKind::kPin}, "refreshed target kinds lost");
    require(generated.get_generated_clock_definition()->target_kinds == generated.get_source_kinds(), "generated target metadata disagrees");
    require(generated.get_generated_clock_definition()->master_source_kind == ClockSourceKind::kPin, "generated source kind lost");
    run("get_ports -quiet missing*");
    require(std::string(Tcl_GetStringResult(interpreter.getInterp())).empty(), "unbound wildcard fabricated a port");
    buildDesign();
    run("get_ports -quiet missing");
    require(std::string(Tcl_GetStringResult(interpreter.getInterp())).empty(), "loaded design accepted an unbound port");
  }
  DataManager::destroyInst();
  Logger::destroyInst();
}
