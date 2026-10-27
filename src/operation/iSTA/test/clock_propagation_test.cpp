// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ClockArcPropagator.hpp"
#include "ClockPropagator.hpp"
#include "ClockReconvergenceCalculator.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "StartpointInitializer.hpp"
using namespace ista;

void generatedInsertion()
{
  Logger::initInst();
  Database db;
  Config config;
  auto add_pin = [&](const std::string& name, const std::string& instance, PinDirection direction) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(instance.empty() ? name : name.substr(instance.size() + 1));
    pin.set_instance_name(instance);
    pin.set_is_port(instance.empty());
    pin.set_direction(direction);
    if (!instance.empty())
      db.network().instances()[instance].get_pin_name_list().push_back(name);
  };
  add_pin("clk", "", PinDirection::kInput);
  auto& inv = db.liberty().get_cell_map()["INV"];
  inv.get_port_map()["A"].set_is_input(true);
  inv.get_port_map()["Y"].set_is_output(true);
  TimingCellArc cell;
  cell.set_source_port("A");
  cell.set_sink_port("Y");
  TimingArc timing;
  timing.set_sense(TimingArcSense::kNegative);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    timing.get_delay_table_map()[edge].set_value_list({edge == TransType::kRise ? .1 : .2});
    timing.get_slew_table_map()[edge].set_value_list({.05});
  }
  cell.get_timing_arc_list().push_back(timing);
  inv.get_cell_arc_list().push_back(cell);
  for (const std::string name : {"ci0", "ci1"}) {
    auto& instance = db.network().instances()[name];
    instance.set_instance_name(name);
    instance.set_cell_name("INV");
    add_pin(name + ":A", name, PinDirection::kInput);
    add_pin(name + ":Y", name, PinDirection::kOutput);
  }
  db.liberty().get_cell_map()["FF"].get_port_map()["CK"].set_is_input(true);
  for (const std::string name : {"launch", "capture"}) {
    auto& instance = db.network().instances()[name];
    instance.set_instance_name(name);
    instance.set_cell_name("FF");
    instance.set_is_sequential(true);
    instance.set_clock_pin_name(name + ":CK");
    add_pin(name + ":CK", name, PinDirection::kInput);
  }
  auto add_net = [&](const std::string& name, std::vector<std::string> pins) {
    auto& net = db.network().nets()[name];
    net.set_net_name(name);
    net.set_pin_name_list(pins);
    for (const auto& pin : pins)
      db.network().pins().at(pin).set_net_name(name);
  };
  add_net("clk", {"clk", "ci0:A"});
  add_net("common", {"ci0:Y", "launch:CK", "ci1:A"});
  add_net("generated", {"ci1:Y", "capture:CK"});
  for (const auto* name : {"master", "generated"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_period(25.);
    clock.set_fall_edge(12.5);
    clock.set_is_propagated(true);
    clock.set_source_list({std::string(name) == "master" ? "clk" : "ci1:Y"});
  }
  auto& generated = db.sdc().get_clock_map().at("generated");
  generated.set_master_clock_name("master");
  generated.set_master_source("clk");
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    db.sdc().timingDerates().set(AnalysisType::kMin, true, edge, DerateDelay::kCell, .8);
    db.sdc().timingDerates().set(AnalysisType::kMax, true, edge, DerateDelay::kCell, 1.2);
  }
  GraphBuilder(db).build();
  DelayCalculator delay(db, config);
  delay.initialize(db.graph());
  ClockPropagator(db, delay).propagate();
  std::string common;
  const auto correction = ClockReconvergenceCalculator(db).correction("launch:CK", TransType::kRise, "master", "capture:CK", TransType::kRise, "generated",
                                                                      AnalysisType::kMax, common, TransType::kFall, TransType::kRise);
  // Same scalar-cell experiment: PT U-2022.12-SP3 and OpenSTA return .04 ns.
  if (std::abs(correction - .04) > 1e-9 || common != "ci0:Y")
    throw std::runtime_error("generated insertion trace lost common-path correction across opposite source phases");
  if (db.graph().points().at("ci0:Y").has_clock_state("generated"))
    throw std::runtime_error("generated insertion trace leaked into ordinary clock coverage");
  Logger::destroyInst();
}

void sourceProbe()
{
  Database db;
  Config config;
  DelayCalculator delay(db, config);
  auto& pin = db.network().pins()["in"];
  pin.set_is_port(true);
  pin.set_direction(PinDirection::kInput);
  pin.set_full_name("in");
  db.graph().points()["in"] = TimingPoint{};
  for (const auto& name : {"a", "b"}) {
    auto& clock = db.sdc().get_clock_map()[name];
    clock.set_clock_name(name);
    clock.set_source_list({"in"});
    clock.set_period(10.);
    clock.set_rise_edge(name == std::string("a") ? 1. : 3.);
    clock.set_fall_edge(name == std::string("a") ? 6. : 8.);
  }
  std::vector<std::string> starts{"in"};
  StartpointInitializer(db, delay).seed(starts);
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (auto edge : {TransType::kRise, TransType::kFall})
      for (auto& [tag, path] : db.graph().points().at("in").get_path_state_map().at(mode).at(PathSourceType::kInput).at(edge)) {
        double expected = path.get_clock_name() == "a" ? 1.0 : 3.0;
        if (edge == TransType::kFall)
          expected += 5.0;
        if (std::fabs(path.get_arrival() - expected) > 1e-9 || std::fabs(path.get_launch_time() - expected) > 1e-9)
          throw std::runtime_error("input clock seed used another named clock");
      }
}
void sharedArc(bool slow_first, AnalysisType mode, TransType edge, bool derate = false, bool mixed = false)
{
  Database db;
  Config config;
  DelayCalculator delay(db, config);
  ClockArcPropagator propagation(db, delay);
  const auto output = mixed ? (edge == TransType::kRise ? TransType::kFall : TransType::kRise) : edge;
  const double factor = derate ? (mode == AnalysisType::kMax ? 1.2 : 0.8) : 1.0;
  if (derate) {
    db.sdc().timingDerates().set(mode, true, edge, DerateDelay::kCell, factor);
    db.sdc().timingDerates().set(mode, false, edge, DerateDelay::kCell, 1.8);
  }
  for (const auto& name : {"u:A", "u:Y"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_instance_name("u");
    db.graph().points()[name] = TimingPoint{};
  }
  TimingArc timing;
  timing.set_sense(mixed ? TimingArcSense::kNegative : TimingArcSense::kPositive);
  auto& table = timing.get_delay_table_map()[output];
  table.set_axis_list({{0., 2.}});
  table.set_variable_type1(TimingTableVariableType::kInputTransition);
  table.set_value_list({1., 3.});
  timing.get_slew_table_map()[output].set_value_list({1.});
  TimingCellArc cell;
  cell.set_source_port("A");
  cell.set_sink_port("Y");
  cell.get_timing_arc_list().push_back(timing);
  db.liberty().get_cell_map()["BUF"].get_cell_arc_list().push_back(cell);
  Arc arc;
  arc.set_type(ArcType::kCell);
  arc.set_source_pin("u:A");
  arc.set_sink_pin("u:Y");
  arc.setLibertyArcId(LibertyArcId{"BUF", 0});
  arc.get_input_output_delay_map()[mode][edge][output] = 0.;
  db.graph().arcs().push_back(arc);
  std::vector<std::string> sources{"u:A"};
  ClockSource slow("slow", sources, true), fast("fast", sources, true);
  for (auto name : {"slow", "fast"}) {
    auto& state = db.graph().points().at("u:A").get_clock_state(name);
    state.waveforms[edge].physical_arrival_map[mode][edge] = 0.;
    state.waveforms[edge].physical_slew_map[mode][edge] = name == std::string("slow") ? 2. : .1;
    if (mixed) {
      // Opposite source phases reconverged into the same local transition.
      state.waveforms[output].physical_arrival_map[mode][edge] = 5.;
      state.waveforms[output].physical_slew_map[mode][edge] = .45;
    }
    state.summarize();
    db.graph().points().at("u:Y").get_clock_state(name);
  }
  for (auto* clock : (slow_first ? std::vector<ClockSource*>{&slow, &fast} : std::vector<ClockSource*>{&fast, &slow})) {
    propagation.propagateSlew(*clock, 0, mode, edge, false);
    propagation.propagateArrival(*clock, 0, mode, edge, false);
  }
  for (auto name : {"slow", "fast"}) {
    auto& state = db.graph().points().at("u:Y").get_clock_state(name);
    double expected = (std::string(name) == "slow" ? 3.0 : 1.1) * factor;
    if (mixed) {
      if (state.waveforms.size() != 2 || std::fabs(*state.waveforms.at(output).physicalArrival(mode, output) - 6.45) > 1e-9
          || std::fabs(*state.waveforms.at(edge).physicalArrival(mode, output) - expected) > 1e-9)
        throw std::runtime_error("opposite clock source phases shared a delay winner or cache entry");
      continue;
    }
    if (std::fabs(state.physical_arrival_map.at(mode).at(edge) - expected) > 1e-9
        || std::fabs(state.physical_predecessor_arc_delay_map.at(mode).at(edge) - expected) > 1e-9)
      throw std::runtime_error("named clock consumed another clock's aggregated arc delay");
  }
}
int main(int argc, char** argv)
{
  if (argc > 1 && std::string(argv[1]) == "generated")
    generatedInsertion();
  else if (argc > 1 && std::string(argv[1]) == "source")
    sourceProbe();
  else
    for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        sharedArc(true, mode, edge, argc > 1 && std::string(argv[1]) == "derate", argc > 1 && std::string(argv[1]) == "phase");
        sharedArc(false, mode, edge, argc > 1 && std::string(argv[1]) == "derate", argc > 1 && std::string(argv[1]) == "phase");
      }
}
