// SPDX-License-Identifier: MulanPSL-2.0
#include <unistd.h>

#include <array>
#include <filesystem>

#include "SdcTestDesign.hpp"
#include "SdcWriter.hpp"
#include "query/SdcObject.hpp"
#include "sdc/ExceptionMatcher.hpp"

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
  const std::string mode = argc > 1 ? argv[1] : "load";
  if (mode == "object_names" || mode == "query_collection" || mode == "all_clock_collection") {
    eval("create_clock -name {lit*} -period 10; create_clock -name lit_suffix -period 10");
    if (mode == "object_names") {
      eval("get_object_name [get_clocks -regexp {^lit[*]$}]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "lit*", "name query expanded typed wildcard");
      eval("get_object_name [current_design]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "sdc_test", "design full name missing");
      addNet("in", {"in"});
      eval("get_object_name [list [get_nets -exact in] [get_ports -exact in]]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "in in", "same-name objects collapsed");
      addPin("u/v", "A", PinDirection::kInput);
      addPin("u", "v:A", PinDirection::kInput);
      Tcl_SetVar2Ex(interp.getInterp(), "one_pin", nullptr, sdc::SdcObject::create("u/v:A", sdc::SdcObjectType::kPin), TCL_GLOBAL_ONLY);
      eval("get_object_name $one_pin");
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "u/v/A", "pin name reinterpreted as alias");
      eval("llength [get_fanin -to $one_pin]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "1", "fan query expanded canonical pin alias");
    } else if (mode == "query_collection") {
      eval("get_clocks [get_clocks -regexp {^lit[*]$}]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "lit*", "nested query expanded typed wildcard");
      eval("get_clocks [get_clocks -quiet missing]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())).empty(), "empty collection became all clocks");
      eval("get_clocks -filter {period > 11} [get_clocks -regexp {^lit[*]$}]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())).empty(), "typed selection bypassed filter");
      require(interp.evalString("get_clocks [get_ports in]") == TCL_ERROR, "wrong collection type accepted");
      require(interp.evalString("get_pins -of_objects [current_design]") == TCL_ERROR, "design reinterpreted as untyped pattern");
      eval("create_clock -name {clock one} -period 10");
      eval("get_object_name [get_clocks [get_clocks -regexp {{^clock one$}}]]");
      require(std::string(Tcl_GetStringResult(interp.getInterp())) == "{clock one}", "space split typed name");
    } else {
      eval("set_clock_latency .9 [remove_from_collection [all_clocks] [get_clocks lit_suffix]]");
      require(db.sdc().get_clock_map().at("lit_suffix").get_network_latency(AnalysisType::kMax, TransType::kRise) == 0, "excluded clock regained latency");
      require(db.sdc().get_clock_map().at("lit*").get_network_latency(AnalysisType::kMax, TransType::kRise) == .9, "literal clock lost latency");
    }
  } else if (mode == "io_clock_count") {
    eval("create_clock -name M -period 10; create_clock -name V -period 12");
    eval("create_clock -name {clock one} -period 10");
    for (bool input : {true, false}) {
      const std::string cmd = input ? "set_input_delay" : "set_output_delay";
      const std::string port = input ? "in" : "out";
      eval(cmd + " .2 -clock M [get_ports " + port + "]");
      for (const auto* clocks : {"[get_clocks {M V}]", "{}", "[get_ports in]"}) {
        require(interp.evalString(cmd + " .6 -clock " + clocks + " [get_ports " + port + "]") == TCL_ERROR, "multiple/invalid explicit I/O clocks accepted");
        const auto& c = db.sdc().get_port_constraint_map().at(port);
        for (auto a : {AnalysisType::kMin, AnalysisType::kMax})
          for (auto t : {TransType::kRise, TransType::kFall}) {
            const auto delays = input ? c.get_input_delays(a, t) : c.get_output_delays(a, t);
            require(delays.size() == 1 && delays.front()->get_clock_name() == "M" && delays.front()->get_delay() == .2,
                    "failed clock selection changed I/O delay");
          }
      }
      eval(cmd + " .3 -clock {{clock one}} [get_ports " + port + "]");
      eval(cmd + " .2 -clock {M M} [get_ports " + port + "]");
      eval(cmd + " .4 -add_delay -clock V [get_ports " + port + "]");
      const auto& c = db.sdc().get_port_constraint_map().at(port);
      require((input ? c.get_input_delays(AnalysisType::kMax, TransType::kRise) : c.get_output_delays(AnalysisType::kMax, TransType::kRise)).size() == 2,
              "independent add_delay clock records lost");
    }
  } else if (mode == "empty_related") {
    eval("get_cells -of_objects [get_pins -quiet NO_SUCH_PIN]");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).empty(), "empty related query expanded to all cells");
    require(interp.evalString("get_cells * -of_objects {}") == TCL_ERROR, "explicit empty of_objects bypassed option conflict");
  } else if (mode == "query_scope") {
    db.network().hierarchyPins()["cdc:I"] = {"cdc", "I", PinDirection::kInput, "", "", 0, 0, 1};
    eval("get_cells *");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).find("cdc/") == std::string::npos, "default glob crossed hierarchy");
    eval("get_cells cdc/*");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).find("cdc/buf") != std::string::npos, "explicit hierarchy path lost");
    eval("get_cells -hierarchical *");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).find("cdc/buf") != std::string::npos, "hierarchical glob lost descendants");
  } else if (mode == "typed_setters") {
    addNet("in", {"in", "cdc/buf:A"});
    const auto before = db.sdc().get_port_constraint_map().size();
    for (const auto& script : {"set_case_analysis 0 [get_nets in]", "set_max_fanout 2 [get_nets in]", "set_input_transition 0.2 [get_nets in]",
                               "set_input_transition 0.2 [get_ports out]", "set_input_transition 0.2 {}", "set_case_analysis 0 {in NO_SUCH_PIN}",
                               "set_input_transition 0.2 {in out}", "set_input_delay 0.2 [get_nets in]", "set_output_delay 0.2 [get_nets in]"}) {
      require(interp.evalString(script) == TCL_ERROR, std::string("invalid collection accepted: ") + script);
      require(db.sdc().get_case_analysis_map().empty() && db.sdc().get_port_max_fanout_map().empty() && db.sdc().get_port_constraint_map().size() == before,
              "failed setter partially mutated constraints");
    }
    eval("set_case_analysis 0 [get_ports -exact in]");
    require(db.sdc().get_case_analysis_map().contains("in"), "valid typed port rejected");
  } else if (mode == "clock_kinds") {
    eval("create_clock -name in -period 10 [get_ports in]");
    for (const auto& script : {"set_load .2 [get_clocks in]", "set_clock_transition .2 [get_ports in]",
                               "set_input_delay .2 -clock [get_ports in] [get_ports in]", "set_clock_uncertainty .2 -from [get_ports in] -to [get_clocks in]",
                               "set_clock_groups -asynchronous -group [get_ports in]", "create_clock -name invalid -period 8 [get_clocks in]"})
      require(interp.evalString(script) == TCL_ERROR, std::string("typed identity lost: ") + script);
    eval("create_clock -name {literal*} -period 10; create_clock -name literal_suffix -period 10");
    eval("set_clock_uncertainty .2 [get_clocks -regexp {^literal[*]$}]");
    require(db.sdc().get_clock_map().at("literal*").get_setup_uncertainty() == .2 && db.sdc().get_clock_map().at("literal_suffix").get_setup_uncertainty() == 0,
            "typed literal clock expanded a wildcard");
    eval("set_clock_latency .3 [list [get_clocks in]]");
  } else if (mode == "exception_kinds") {
    eval("create_clock -name in -period 10 [get_ports clk]");
    auto applies = [&] {
      return ExceptionMatcher(db.sdc(), db.network()).initState("reset", "in", TransType::kRise, AnalysisType::kMax, TransType::kRise).front() >= 0;
    };
    eval("set_false_path -from [get_ports in]");
    require(!applies(), "port exception matched unrelated paths launched by a same-name clock");
    db.sdc().get_path_exception_list().clear();
    eval("set_false_path -from [get_clocks in]; reset_path -from [get_ports in]");
    require(db.sdc().get_path_exception_list().size() == 1 && applies(), "port reset deleted a clock exception");
    const auto path = std::filesystem::temp_directory_path() / ("ista-exception-kind-" + std::to_string(getpid()) + ".sdc");
    sdc::writeSdcFile(db, path.string());
    db.sdc().clearConstraints();
    require(interp.evalScriptFile(path.string()) == TCL_OK && applies(), "writer changed a clock selector into a same-name port");
    std::filesystem::remove(path);
  } else if (mode == "typed_alias") {
    addPin("u/v", "A", PinDirection::kInput);
    addPin("u", "v:A", PinDirection::kInput);
    auto* target = sdc::SdcObject::create("u/v:A", sdc::SdcObjectType::kPin);
    Tcl_SetVar2Ex(interp.getInterp(), "one_pin", nullptr, target, TCL_GLOBAL_ONLY);
    eval("set_false_path -through $one_pin");
    require(db.sdc().get_path_exception_list().back().get_through_list().front().get_objects() == ExceptionObjects{{ExceptionObjectType::kPin, "u/v:A"}},
            "typed pin expanded into another canonical pin");
  } else if (mode == "typed_related") {
    addNet("in", {"in", "cdc/buf:A"});
    eval("get_cells -of_objects [get_nets -exact in]");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "cdc/buf", "related net reinterpreted as same-name port");
  } else if (mode == "load") {
    const auto path = std::filesystem::temp_directory_path() / ("ista-load-" + std::to_string(getpid()) + ".sdc");
    for (const auto& script :
         {"set_load -max -fall 2 [get_ports out]; set_load -max -rise 1 [get_ports out]",
          "set_load -max -rise 1 [get_ports out]; set_load -max -fall 2 [get_ports out]",
          "set_load -max -rise 2 [get_ports out]; set_load -max -rise -wire_load 0 [get_ports out]",
          "set_load -min -fall -wire_load 3 [get_ports out]; set_load -max -rise 1 [get_ports out]; set_load -max -rise -wire_load 2 [get_ports out]"}) {
      db.sdc().get_port_constraint_map().clear();
      eval(script);
      std::vector<double> before;
      for (auto a : {AnalysisType::kMin, AnalysisType::kMax})
        for (auto t : {TransType::kRise, TransType::kFall})
          before.push_back(db.sdc().get_port_constraint_map().at("out").get_load(a, t));
      sdc::writeSdcFile(db, path.string());
      db.sdc().get_port_constraint_map().clear();
      require(interp.evalScriptFile(path.string()) == TCL_OK, "SDC reload failed");
      std::size_t i = 0;
      for (auto a : {AnalysisType::kMin, AnalysisType::kMax})
        for (auto t : {TransType::kRise, TransType::kFall})
          require(db.sdc().get_port_constraint_map().at("out").get_load(a, t) == before[i++], "load roundtrip changed an effective mode/edge value");
    }
    std::filesystem::remove(path);
  } else if (mode == "collection") {
    eval("remove_from_collection [get_nets -exact clock_net] {}");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "clock_net", "net collection lost");
    addNet("in", {"in"});
    eval("set_load 3 [remove_from_collection [get_nets -exact in] {}]");
    require(db.sdc().get_net_load_map().contains("in") && !db.sdc().get_port_constraint_map().contains("in"), "collection lost net identity");
    eval("set mixed [concat [get_nets -exact in] [get_ports -exact in]]; set_load 4 [remove_from_collection $mixed [get_ports -exact in]]");
    require(!db.sdc().get_port_constraint_map().contains("in"), "same-name objects removed by name instead of type");
    eval("set_load 5 [remove_from_collection -intersect $mixed [get_ports -exact in]]");
    require(db.sdc().get_port_constraint_map().at("in").get_load(AnalysisType::kMax, TransType::kRise) == 5, "intersection lost port identity");
    addPin("", "sdc_test", PinDirection::kInput);
    eval("remove_from_collection [current_design] [current_design]");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).empty(), "design identity degraded to same-name port");
    eval("remove_from_collection [current_design] [get_ports -exact sdc_test]");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "sdc_test", "port selector removed same-name design");
    eval("remove_from_collection -intersect [current_design] [current_design]");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "sdc_test", "intersection lost design identity");
  } else if (mode == "fan") {
    db.network().hierarchyPins()["h:A"] = {"h", "A", PinDirection::kInput, "input_net", "", 0, 1, 1};
    db.network().hierarchyIndices()["cdc/source_reg"] = 0;
    auto size = db.network().nets().size();
    eval("get_fanin -to [get_pins h/A]");
    require(db.network().nets().size() == size && !db.network().nets().contains("h:A"), "hierarchy fan query inserted a phantom net");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).find("in") != std::string::npos, "hierarchy fanin lost the upstream input");
    for (const auto& suffix : {std::string("[0]"), std::string("0")}) {
      addPin("", "bus" + suffix, PinDirection::kInput);
      addPin("buffer", "A" + suffix, PinDirection::kInput);
      addNet("wire" + suffix, {"bus" + suffix, "buffer:A" + suffix});
    }
    // Every typed collection and existing literal root must retain its name;
    // a bracketed bus name must never resolve to its scalar glob match.
    for (const auto& command : {std::string("get_fanin -to "), std::string("get_fanout -from ")}) {
      for (const auto& root :
           {"[get_ports -exact {bus[0]}]", "[get_pins -exact {buffer/A[0]}]", "[get_nets -exact {wire[0]}]", "{bus[0]}", "{buffer:A[0]}", "{wire[0]}"}) {
        eval(command + root);
        const std::string result = Tcl_GetStringResult(interp.getInterp());
        require(!result.empty() && result.find("[0]") != std::string::npos && result.find("bus0") == std::string::npos
                    && result.find("buffer:A0") == std::string::npos,
                "fan query reinterpreted resolved root as glob: " + command + root + " -> " + result);
      }
    }
    eval("get_fanin -to {bus*}");
    require(std::string(Tcl_GetStringResult(interp.getInterp())).find("bus0") != std::string::npos, "raw fan glob stopped expanding");
  } else if (mode == "regexp") {
    addPin("", "abc", PinDirection::kInput);
    addPin("", "123", PinDirection::kInput);
    eval(R"(get_ports -regexp -nocase {\D+})");
    auto result = std::string(Tcl_GetStringResult(interp.getInterp()));
    require(result.find("abc") != std::string::npos && result.find("123") == std::string::npos, "nocase inverted regexp escape semantics");
    eval("get_ports -regexp -nocase {ABC}");
    require(std::string(Tcl_GetStringResult(interp.getInterp())) == "abc", "nocase did not match letters");
    require(interp.evalString("get_ports -regexp -nocase {[}") == TCL_ERROR, "invalid regexp accepted");
  }
  SdcCommand::destroyInst();
  DataManager::destroyInst();
  Logger::destroyInst();
}
