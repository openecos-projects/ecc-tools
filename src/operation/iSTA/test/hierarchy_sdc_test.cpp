// SPDX-License-Identifier: MulanPSL-2.0
#include <exception>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ClockPropagator.hpp"
#include "DataManager.hpp"
#include "DataPropagator.hpp"
#include "DelayCalculator.hpp"
#include "GraphBuilder.hpp"
#include "IdbLayout.h"
#include "Logger.hpp"
#include "ReadHierarchy.hpp"
#include "SdcCommand.hpp"
#include "VerilogImportPlan.hh"
#include "VerilogLibrary.hh"
#include "network/Network.hpp"
#include "query/ObjectQuery.hpp"
#include "sdc/ExceptionMatcher.hpp"
#include "verilog/VerilogFrontend.hh"
using namespace ista;
void require(bool value, const std::string& message)
{
  if (!value)
    throw std::runtime_error(message);
}
void command(const std::string& text)
{
  auto& sdc = SdcCommand::getInst();
  const auto rc = sdc.evalString(text);
  require(rc == TCL_OK, text + ": " + Tcl_GetStringResult(sdc.getInterp()));
}
void rejects(const std::string& text)
{
  require(SdcCommand::getInst().evalString(text) == TCL_ERROR, "accepted unsupported constraint: " + text);
}
std::unique_ptr<idb::verilog_import::VerilogImportPlan> makePlan(bool bidirectional = false, const std::string& custom = {})
{
  idb::IdbLayout layout;
  auto* master = layout.get_cell_master_list()->set_cell_master("BUF");
  for (const auto& name : {"A", "B", "Y"}) {
    auto* term = master->add_term(name);
    term->set_direction(std::string(name) != "Y" ? idb::IdbConnectDirection::kInput : idb::IdbConnectDirection::kOutput);
    term->set_type(idb::IdbConnectType::kSignal);
  }
  idb::verilog_import::VerilogLibrary library(layout);
  const std::string input = !custom.empty() ? custom
                            : bidirectional ? R"(
module block(inout C, input D, output Y);
BUF local(.A(D), .Y(C)); BUF sink(.A(C), .Y(Y)); endmodule
module top(input clk, output out, output z);
wire shared; BUF src(.A(clk),.Y(shared));
block b(.C(shared),.D(clk),.Y(out)); BUF sibling(.A(shared),.Y(z)); endmodule
)"
                                            : R"(
module inner(input CLK, output Y); BUF sink(.A(CLK),.Y(Y)); endmodule
module block(input [0:1] C, output Y); inner inner(.CLK(C[0]),.Y(Y)); endmodule
module top(input clk, output out, output z);
wire shared; BUF src(.A(clk),.Y(shared));
block b(.C({shared,clk}),.Y(out)); BUF sibling(.A(shared),.Y(z));
BUF \b/fake (.A(shared)); endmodule
)";
  auto flat = idb::verilog::compile(input, "<hierarchy-test>", "top", {}, [&](std::string_view name) { return library.lookup(name); });
  require(bool(flat), flat ? "" : flat.diagnostics.front().text());
  auto plan = idb::verilog_import::makeImportPlan(*flat.design, library);
  require(bool(plan), plan ? "" : plan.diagnostics.front().text());
  return std::move(plan.plan);
}
void fill(Database& db, const idb::verilog_import::VerilogImportPlan& plan)
{
  db.network().setName("top");
  auto add = [&](std::string instance, const auto& p) {
    const std::string name = instance.empty() ? p.name : instance + ":" + p.name;
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(p.name);
    pin.set_instance_name(instance);
    pin.set_is_port(instance.empty());
    pin.set_direction(p.direction == idb::verilog::Direction::input ? PinDirection::kInput : PinDirection::kOutput);
    const std::string net_name = "physical_" + std::to_string(p.net);  // Deliberately different net names.
    pin.set_net_name(net_name);
    auto& net = db.network().nets()[net_name];
    net.set_net_name(net_name);
    net.get_pin_name_list().push_back(name);
    if (!instance.empty())
      db.network().instances()[instance].get_pin_name_list().push_back(name);
  };
  for (const auto& port : plan.ports)
    add("", port);
  for (const auto& cell : plan.instances) {
    auto& inst = db.network().instances()[cell.name];
    inst.set_instance_name(cell.name);
    inst.set_cell_name(cell.master);
    for (const auto& pin : cell.pins)
      add(cell.name, pin);
  }
  for (const std::string name : {"A", "B", "Y"}) {
    auto& port = db.liberty().get_cell_map()["BUF"].get_port_map()[name];
    port.set_port_name(name);
    port.set_is_input(name != "Y");
    port.set_is_output(name == "Y");
  }
  linkHierarchy(db.network(), plan);
}
void testScopeAndBus()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan();
  fill(db, *plan);
  require(db.network().hierarchyPins().contains("b:C[0]"), "ascending bus boundary missing");
  command("if {[get_object_name [get_nets shared]] ne {shared}} {error {logical net name lost}}");
  command(
      "if {[lsort [get_object_name [get_pins -of_objects [get_nets shared]]]] ne [lsort {b/C[0] b/fake/A sibling/A src/Y}]} {error {default net query crossed "
      "hierarchy}}");
  command(
      "if {[lsort [get_object_name [get_pins -leaf -of_objects [get_nets shared]]]] ne [lsort {b/inner/sink/A b/fake/A sibling/A src/Y}]} {error {leaf net "
      "query lost connectivity}}");
  command(
      "if {[lsort [get_object_name [get_pins -of_objects [get_nets b/inner/CLK]]]] ne [lsort {b/inner/CLK b/inner/sink/A}]} {error {nested local net query "
      "crossed scope}}");
  command(
      "if {[lsort [get_object_name [get_pins -filter {direction == in} -of_objects [get_nets shared]]]] ne [lsort {b/C[0] b/fake/A sibling/A}]} {error {filter "
      "applied to wrong pins}}");

  command("if {[lsort [get_cells *]] ne [list b b/fake sibling src]} {error {root query lost escaped instance or included descendants}}");
  command("if {[get_cells b/*] ne {b/inner}} {error {explicit hierarchy query included escaped root or deep leaf}}");
  command("if {[llength [get_cells -hierarchical *]] != 6} {error {hierarchical query lost cells}}");
  command("if {[lsort [get_pins *]] ne [lsort {b:C[0] b:C[1] b:Y b/fake:A sibling:A sibling:Y src:A src:Y}]} {error \"wrong root pins: [get_pins *]\"}");
  command("if {[get_pins -hierarchical sink/A] ne {b/inner/sink:A}} {error {hierarchical pin query did not use local cell/pin name}}");
  command("if {[llength [get_pins -quiet src*]] != 0} {error {default pin pattern crossed cell/pin separator}}");
  command("if {[lsort [get_pins -hierarchical src*]] ne {src:A src:Y}} {error {hierarchical pin pattern lost local full name}}");

  command("if {[get_nets clk] ne [get_nets -of_objects [get_ports clk]]} {error {top port net name was lost during flattening}}");
  command("set_load .2 [get_nets clk]");
  require(db.sdc().get_net_load_map().contains(db.network().pins().at("clk").get_net_name()), "alias net load did not reach physical connectivity");

  command("if {[lindex [get_full_name [get_pins {b/C[0]}]] 0] ne {b/C[0]}} {error {boundary identity lost}}");
  command("if {[llength [get_pins -leaf {b/C[0]}]] != 0} {error {boundary became a leaf}}");
  command("if {[llength [get_nets -of_objects [get_pins {b/C[0]}]]] != 1} {error {boundary net query failed}}");
  command("create_clock -name upstream -period 20 [get_pins src/Y]");
  command("create_clock -name local -period 10 [get_pins {b/C[0]}]");
  require(db.sdc().get_clock_map().at("upstream").get_source_list() == std::vector<std::string>{"src:Y"}, "boundary overwrote upstream root");
  require(db.sdc().clockAllowsNetArc(db.network(), "local", "src:Y", "b/inner/sink:A"), "local clock missed inside load");
  require(!db.sdc().clockAllowsNetArc(db.network(), "local", "src:Y", "sibling:A"), "local clock leaked to sibling");
  require(!db.sdc().clockAllowsNetArc(db.network(), "upstream", "src:Y", "b/inner/sink:A"), "upstream clock crossed replacement boundary");
  require(db.sdc().clockAllowsNetArc(db.network(), "upstream", "src:Y", "sibling:A"), "upstream sibling clock was lost");
  sdc::ObjectQueryOptions related;
  related.has_of_objects = true;
  related.of_objects = {{sdc::QueryObjectType::kPin, "b:C[0]"}};
  require(sdc::findObjects(SdcCommand::getInst().getInterp(), db, {"*"}, sdc::QueryObjectType::kClock, related) == std::vector<std::string>{"local"},
          "boundary related clock query failed");
  require(!db.sdc().clockAllowsNetArc(db.network(), "local", "src:Y", "b/fake:A"), "escaped sibling incorrectly belongs to boundary");
  command("create_clock -name inner -period 8 [get_pins b/inner/CLK]");
  require(db.sdc().clockAllowsNetArc(db.network(), "inner", "src:Y", "b/inner/sink:A"), "inner boundary clock blocked by outer boundary");
  require(!db.sdc().clockAllowsNetArc(db.network(), "local", "src:Y", "b/inner/sink:A"), "outer boundary clock passed inner root");
  db.sdc().get_clock_map().erase("inner");
  command("create_generated_clock -name gen -source [get_pins b/inner/CLK] -master_clock local -divide_by 2 [get_ports out]");
  require(db.sdc().get_clock_map().at("gen").get_period() == 20, "generated master boundary failed");
  command("set_max_delay 5 -through [get_pins {b/C[0]}]");
  command("set_false_path -hold -through [get_pins b/inner/CLK]");
  auto state = ExceptionMatcher(db.sdc(), db.network()).initState("clk", "", TransType::kRise, AnalysisType::kMin, TransType::kRise);
  require(ExceptionMatcher(db.sdc(), db.network()).advanceState(ExceptionThroughIndex(db.sdc(), db.network()), state, "sibling:A", TransType::kRise, "src:Y")[1]
              == 0,
          "through matched sibling");
  require(ExceptionMatcher(db.sdc(), db.network())
                  .advanceState(ExceptionThroughIndex(db.sdc(), db.network()), state, "b/inner/sink:A", TransType::kRise, "src:Y")[1]
              == 1,
          "through missed boundary");
  command("set_false_path -through [get_pins {b/C[0]}] -through [get_pins b/inner/CLK]");
  command("set_false_path -through [get_pins b/inner/CLK] -through [get_pins {b/C[0]}]");
  auto ordered = ExceptionMatcher(db.sdc(), db.network()).initState("clk", "", TransType::kRise, AnalysisType::kMax, TransType::kRise);
  ordered = ExceptionMatcher(db.sdc(), db.network())
                .advanceState(ExceptionThroughIndex(db.sdc(), db.network()), ordered, "b/inner/sink:A", TransType::kRise, "src:Y");
  require(ordered[2] == 2, "ordered nested through boundaries did not match");
  require(ordered[3] != 2, "reversed nested through boundaries incorrectly matched");
  rejects("set_false_path -from [get_pins {b/C[0]}]");
  DelayCalculator::initInst();
  STADC.clear();
  GraphBuilder(STADM.getDatabase()).build();
  STADC.initialize(STADM.getDatabase().graph());
  STADM.getDatabase().graph().clearTiming();
  ClockPropagator(STADM.getDatabase(), STADC).propagate();
  require(db.graph().points().at("b/inner/sink:A").has_clock_state("local"), "clock marking missed inside load");
  require(!db.graph().points().at("sibling:A").has_clock_state("local"), "clock marking leaked outside");
  require(db.graph().points().at("sibling:A").has_clock_state("upstream"), "clock marking lost sibling");
  STADC.clear();
  DelayCalculator::destroyInst();
  command("set_max_delay 7 -through [list [get_pins {b/C[0]}] [get_pins b/inner/CLK]]");
  require(db.sdc().get_path_exception_list().back().get_through_list().front().get_objects()
              == ExceptionObjects{{ExceptionObjectType::kPin, "b:C[0]"}, {ExceptionObjectType::kPin, "b/inner:CLK"}},
          "nested pin collection lost objects");
  require(db.network().hierarchyDrivers("b:Y") == std::vector<std::string>{"b/inner/sink:Y"}, "output boundary picked wrong driver");
  require(db.network().crossesHierarchyPin(db.network().checkedHierarchyPin("b:Y"), "b/inner/sink:Y", "out"), "output boundary missed external load");
  db.network().pins().at("b/inner/sink:A").set_net_name("changed");
  linkHierarchy(db.network(), *plan);
  rejects("create_clock -name bad -period 10 [get_pins {b/C[0]}]");
  DataManager::destroyInst();
}
void testNetSegments()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan();
  fill(db, *plan);
  const auto pin_count = db.network().pins().size(), net_count = db.network().nets().size();
  command("if {[get_object_name [get_nets -of_objects [get_pins {b/C[0]}]]] ne {shared}} {error {boundary query did not select outer segment}}");
  command("set_false_path -through [get_nets {b/C[0]}]");
  ExceptionMatcher matcher(db.sdc(), db.network());
  ExceptionThroughIndex index(db.sdc(), db.network());
  const auto start = matcher.initState("clk", "", TransType::kRise, AnalysisType::kMax, TransType::kRise);
  require(matcher.advanceState(index, start, "sibling:A", TransType::kRise, "src:Y")[0] == 0, "net exception leaked to sibling");
  require(matcher.advanceState(index, start, "b/inner/sink:A", TransType::kRise, "src:Y")[0] == 1, "net exception lost nested branch");
  require(matcher.advanceState(index, start, "b/fake:A", TransType::kRise, "src:Y")[0] == 0, "net exception crossed escaped root instance");
  command("set_load .7 [get_nets {b/C[0]}]");
  const auto physical = db.network().pins().at("src:Y").get_net_name();
  require(db.sdc().get_net_load_map().at(physical).get(AnalysisType::kMax, TransType::kRise) == .7, "segment load missed physical net");
  command("set_timing_derate -late -net_delay 1.3 [get_nets {b/C[0]}]");
  const DerateContext context{"", "", physical};
  require(db.sdc().timingDerates().factor(AnalysisType::kMax, false, TransType::kRise, DerateDelay::kNet, context) == 1.3,
          "segment derate missed physical net");
  require(db.network().netQueryName(physical) == "shared", "physical annotation has no top-level name for export");
  require(db.network().pins().size() == pin_count && db.network().nets().size() == net_count, "logical query changed physical connectivity");
  DataManager::destroyInst();
}

void testMultipleDrivers()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan(true);
  fill(db, *plan);
  command("set_false_path -through [get_pins b/C]");
  const auto state = ExceptionMatcher(db.sdc(), db.network()).initState("clk", "", TransType::kRise, AnalysisType::kMax, TransType::kRise);
  require(ExceptionMatcher(db.sdc(), db.network()).advanceState(ExceptionThroughIndex(db.sdc(), db.network()), state, "b/sink:A", TransType::kRise, "src:Y")[0]
              == 1,
          "incoming inout crossing lost");
  require(
      ExceptionMatcher(db.sdc(), db.network()).advanceState(ExceptionThroughIndex(db.sdc(), db.network()), state, "sibling:A", TransType::kRise, "b/local:Y")[0]
          == 1,
      "outgoing inout crossing lost");
  require(
      ExceptionMatcher(db.sdc(), db.network()).advanceState(ExceptionThroughIndex(db.sdc(), db.network()), state, "b/sink:A", TransType::kRise, "b/local:Y")[0]
          == 0,
      "local driver falsely crossed boundary");
  require(ExceptionMatcher(db.sdc(), db.network()).advanceState(ExceptionThroughIndex(db.sdc(), db.network()), state, "sibling:A", TransType::kRise, "src:Y")[0]
              == 0,
          "external driver falsely crossed boundary");
  DataManager::destroyInst();
}

void testFeedthroughAndEscapedPort()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan(false, R"(
module pass(input A, output Y); assign Y=A; endmodule
module block(input \C/foo , output Y); BUF sink(.A(\C/foo ),.Y(Y)); endmodule
module top(input clk, output out, output z);
wire shared; pass p(.A(clk),.Y(shared)); BUF sink(.A(shared),.Y(out));
block b(.\C/foo (clk),.Y(z)); endmodule
)");
  fill(db, *plan);
  command("if {[llength [get_pins {b/C/foo}]] != 1} {error {escaped slash port lost}}");
  rejects("create_clock -name feedthrough -period 10 [get_pins p/A]");
  rejects("set_false_path -through [get_pins p/Y]");
  DataManager::destroyInst();
}
void testGeneratedInsertion()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan();
  fill(db, *plan);
  command("create_clock -name master -period 10 [get_pins {b/C[0]}]");
  command("create_generated_clock -name gen -source [get_pins {b/C[0]}] -master_clock master -divide_by 2 [get_pins b/inner/sink/A]");
  command("set_propagated_clock [all_clocks]");
  command("set_input_delay -clock master -max 100 [get_ports clk]");
  const auto net_name = db.network().pins().at("src:Y").get_net_name();
  auto& parasitic = db.parasitics().nets()[net_name];
  parasitic.set_net_name(net_name);
  parasitic.get_node_map()["src:Y"].set_capacitance(0.01);
  parasitic.get_node_map()["b/inner/sink:A"].set_capacitance(0.02);
  ParasiticResistor resistor;
  resistor.set_source_node("src:Y");
  resistor.set_sink_node("b/inner/sink:A");
  resistor.set_resistance(100.0);
  parasitic.get_resistor_list().push_back(resistor);
  DelayCalculator::initInst();
  STADC.clear();
  GraphBuilder(STADM.getDatabase()).build();
  STADC.initialize(STADM.getDatabase().graph());
  STADM.getDatabase().graph().clearTiming();
  ClockPropagator(STADM.getDatabase(), STADC).propagate();
  const auto& target = db.graph().points().at("b/inner/sink:A").get_clock_state("gen");
  require(target.physical_arrival_map.at(AnalysisType::kMax).at(TransType::kRise) > 0, "generated insertion delay lost");
  require(!target.physical_predecessor_map.empty(), "generated source path fell back to zero insertion");
  auto& target_point = db.graph().points().at("b/inner/sink:A");
  require(target_point.findDefaultClockState() == &target, "generated default state missing before data propagation");
  const auto generated_slew = target.slew(AnalysisType::kMax, TransType::kRise);
  DataPropagator(STADM.getDatabase(), STADC).propagate();
  require(target_point.launchClock() == "master",
          "fixture did not replace launch clock: " + target_point.launchClock() + " predecessor: " + target_point.get_predecessor());
  require(target_point.findDefaultClockState() == &target && target_point.findDefaultClockState()->slew(AnalysisType::kMax, TransType::kRise) == generated_slew,
          "data launch-clock summary replaced generated default clock state");
  STADC.clear();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void testSiblingClockDataAndGeneratedSource()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan(false, R"(
module block(input CLK, output Y); BUF sink(.A(CLK),.Y(Y)); endmodule
module top(input clk, output out, output z);
block b(.CLK(clk),.Y(out)); block c(.CLK(clk),.Y(z)); endmodule
)");
  fill(db, *plan);
  command("create_clock -name master -period 10 [get_pins b/CLK]");
  DelayCalculator::initInst();
  STADC.clear();
  GraphBuilder(STADM.getDatabase()).build();
  STADC.initialize(STADM.getDatabase().graph());
  STADM.getDatabase().graph().clearTiming();
  ClockPropagator(STADM.getDatabase(), STADC).propagate();
  DataPropagator(STADM.getDatabase(), STADC).propagate();
  auto& outside = db.graph().points().at("c/sink:A");
  for (auto& [analysis, modes] : outside.get_path_state_map())
    for (auto& [source, transitions] : modes)
      for (auto& [transition, paths] : transitions)
        for (auto& [tag, path] : paths)
          require(path.get_clock_name() != "master", "clock-as-data escaped its boundary");
  require(!db.graph().points().at("b/sink:A").get_path_state_map().empty(), "clock-as-data path missing inside boundary");
  command("create_generated_clock -name invalid -source [get_pins c/CLK] -master_clock master -divide_by 2 [get_pins c/CLK]");
  command("set_propagated_clock [all_clocks]");
  bool rejected = false;
  try {
    STADM.getDatabase().graph().clearTiming();
    ClockPropagator(STADM.getDatabase(), STADC).propagate();
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "generated source accepted an unrelated sibling master");
  db.sdc().get_clock_map().erase("invalid");
  command("create_generated_clock -name valid -add -source [get_pins b/CLK] -master_clock master -divide_by 2 [get_pins b/CLK]");
  command("set_propagated_clock [all_clocks]");
  STADM.getDatabase().graph().clearTiming();
  ClockPropagator(STADM.getDatabase(), STADC).propagate();
  STADC.clear();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void testGeneratedReconvergence()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan(false, R"(
module block(input CLK, output Y); BUF sink(.A(CLK),.Y(Y)); endmodule
module top(input clk, output out);
wire b_out,c_out; block b(.CLK(clk),.Y(b_out)); block c(.CLK(clk),.Y(c_out));
BUF target(.A(b_out),.B(c_out),.Y(out)); endmodule
)");
  fill(db, *plan);
  for (const auto& input : {"A", "B"}) {
    TimingCellArc arc;
    arc.set_source_port(input);
    arc.set_sink_port("Y");
    db.liberty().get_cell_map()["BUF"].get_cell_arc_list().push_back(arc);
  }
  command("create_clock -name master -period 10 [get_pins b/CLK]");
  command("create_generated_clock -name gen -source [get_pins b/CLK] -master_clock master -divide_by 2 [get_pins target/Y]");
  command("set_propagated_clock [all_clocks]");
  DelayCalculator::initInst();
  STADC.clear();
  GraphBuilder(STADM.getDatabase()).build();
  STADC.initialize(STADM.getDatabase().graph());
  STADM.getDatabase().graph().clearTiming();
  ClockPropagator(STADM.getDatabase(), STADC).propagate();
  auto* sibling = db.graph().points().at("c/sink:A").find_clock_state("gen");
  require(!sibling || sibling->physical_arrival_map.empty(), "generated source replay restored a rejected sibling edge");
  auto* target = db.graph().points().at("target:Y").find_clock_state("gen");
  require(target && !target->physical_predecessor_map.empty(), "generated reconvergent insertion path was lost");
  command("create_generated_clock -name invalid_chain -source [get_pins b/CLK] -master_clock gen -divide_by 2 [get_pins b/sink/A]");
  command("set_propagated_clock [all_clocks]");
  bool rejected = false;
  try {
    STADM.getDatabase().graph().clearTiming();
    ClockPropagator(STADM.getDatabase(), STADC).propagate();
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "insertion-only master trace was accepted as an active source clock");
  STADC.clear();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}

void testNestedCollections()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan(false, R"(
module top(input [7:0] gpio); BUF sink(.A(gpio[7]),.B(gpio[6])); endmodule
)");
  fill(db, *plan);
  command("set_false_path -from [list [get_ports {gpio[7]}] [get_ports {gpio[6]}]]");
  require(db.sdc().get_path_exception_list().back().get_from_objects()
              == ExceptionObjects{{ExceptionObjectType::kPort, "gpio[7]"}, {ExceptionObjectType::kPort, "gpio[6]"}},
          "nested get_ports collection lost its bus members");
  auto& literal = db.network().pins()["literal{bus[0]}"];
  literal.set_is_port(true);
  literal.set_full_name("literal{bus[0]}");
  command("set_false_path -from [list [get_ports -exact {literal{bus[0]}}]]");
  require(db.sdc().get_path_exception_list().back().get_from_objects() == ExceptionObjects{{ExceptionObjectType::kPort, "literal{bus[0]}"}},
          "typed scalar name was split as a collection");
  DataManager::destroyInst();
}

void testClockDataReconvergence()
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = makePlan(false, R"(
module block(input CLK, output Y); BUF sink(.A(CLK),.Y(Y)); endmodule
module top(input clk, input data, output out);
wire b_out, merged; block b(.CLK(clk),.Y(b_out));
BUF merge_cell(.A(b_out),.B(data),.Y(merged)); block d(.CLK(merged),.Y(out)); endmodule
)");
  fill(db, *plan);
  for (const auto& input : {"A", "B"}) {
    TimingCellArc arc;
    arc.set_source_port(input);
    arc.set_sink_port("Y");
    db.liberty().get_cell_map()["BUF"].get_cell_arc_list().push_back(arc);
  }
  command("create_clock -name master -period 10 [get_pins b/CLK]");
  command("create_clock -name replacement -period 8 [get_pins d/CLK]");
  command("set_input_delay -clock master -max -1 [get_ports data]");
  DelayCalculator::initInst();
  STADC.clear();
  GraphBuilder(STADM.getDatabase()).build();
  STADC.initialize(STADM.getDatabase().graph());
  STADM.getDatabase().graph().clearTiming();
  ClockPropagator(STADM.getDatabase(), STADC).propagate();
  DataPropagator(STADM.getDatabase(), STADC).propagate();
  auto& paths = db.graph().points().at("d/sink:A").get_path_state_map()[AnalysisType::kMax][PathSourceType::kInput][TransType::kRise];
  bool data_found = false;
  for (auto& [tag, path] : paths)
    if (path.get_clock_name() == "master") {
      require(!path.get_is_clock_source_path(), "clock-origin data crossed a replacement boundary");
      data_found |= path.get_start_point() == "data";
    }
  require(data_found, "clock-origin state discarded ordinary data at reconvergence");
  STADC.clear();
  DelayCalculator::destroyInst();
  DataManager::destroyInst();
}
void testPortlessParentScope()
{
  DataManager::initInst();
  auto plan = makePlan(false, R"(
module child(input C); BUF sink(.A(C)); endmodule
module wrapper(); wire clk; child nested(.C(clk)); endmodule
module top(input clk); wrapper w(); endmodule
)");
  fill(STADM.getDatabase(), *plan);
  command("if {[get_cells *] ne {w}} {error {portless parent missing from root scope}}");
  command("if {[get_cells w/*] ne {w/nested}} {error {portless parent's child has wrong scope}}");
  command("if {[get_pins -hierarchical sink/A] ne {w/nested/sink:A}} {error {portless hierarchy lost leaf pin}}");
  DataManager::destroyInst();
}
void testNetCatalog(const std::string& mode)
{
  DataManager::initInst();
  auto& db = STADM.getDatabase();
  auto plan = mode == "net_filter" ? makePlan() : makePlan(false, R"(
module block(input A, output Y);
wire \local/net ; BUF m(.A(A),.Y(\local/net )); BUF n(.A(\local/net ),.Y(Y)); endmodule
module top(input clk, output out);
wire \escaped/net ; BUF src(.A(clk),.Y(\escaped/net )); block b(.A(\escaped/net ),.Y(out)); endmodule
)");
  fill(db, *plan);
  if (mode == "net_filter") {
    GraphBuilder(db).build();
    command("if {[get_object_name [get_nets -filter {fanout > 2} shared]] ne {shared}} {error {net fanout filter lost}}");
    command("if {[get_nets -filter {fanout < 3} shared] ne {}} {error {net fanout filter used wrong connectivity}}");
    command("if {[llength [get_nets -filter {fanout == 3} {b/C[0]}]] != 1} {error {segment fanout filter lost}}");
  } else {
    command("if {[lsearch -exact [get_object_name [get_nets *]] escaped/net] < 0} {error {root wildcard lost escaped net}}");
    command("if {[lsearch -exact [get_object_name [get_nets *]] b/local/net] >= 0} {error {root wildcard entered child scope}}");
    command("if {[lsearch -exact [get_object_name [get_nets b/*]] b/local/net] < 0} {error {child wildcard lost escaped local net}}");
    command("if {[get_object_name [get_nets -hierarchical local*]] ne {b/local/net}} {error {hierarchical match lost local escaped name}}");
  }
  DataManager::destroyInst();
}

int main(int argc, char** argv)
{
  Logger::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  try {
    if (argc > 1) {
      testNetCatalog(argv[1]);
    } else {
      testScopeAndBus();
      testPortlessParentScope();
      testNetSegments();
      testMultipleDrivers();
      testFeedthroughAndEscapedPort();
      testGeneratedInsertion();
      testSiblingClockDataAndGeneratedSource();
      testGeneratedReconvergence();
      testNestedCollections();
      testClockDataReconvergence();
    }
    std::cout << "hierarchy SDC tests passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
  SdcCommand::destroyInst();
  Logger::destroyInst();
}
