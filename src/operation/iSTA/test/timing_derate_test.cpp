// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "TimingAnalyzer.hpp"
#include "timing_checker/ClockReconvergenceCalculator.hpp"
#include "timing_propagator/data/DataArrivalPropagator.hpp"
#include "timing_propagator/data/StartpointInitializer.hpp"
using namespace ista;
static void near(double actual, double expected, const char* message)
{
  if (std::abs(actual - expected) > 1e-9)
    throw std::runtime_error(std::string(message) + ": " + std::to_string(actual) + " != " + std::to_string(expected));
}
// Arrival dominance is valid only within the same CPPR launch context.
static void reconvergentCandidates(bool variation)
{
  Database db;
  Config config;
  DelayCalculator delay(db, config);
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.);
  auto& graph = db.graph();
  for (const auto& name : {std::string("a:CK"), std::string("b:CK"), std::string("end")}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_load_vertex_idx(graph.vertices().size());
    pin.set_instance_name(name.substr(0, 1));
    auto& point = graph.points()[name];
    auto& vertex = graph.vertices().emplace_back();
    vertex.set_pin_name(name);
    vertex.set_timing_point(&point);
    graph.vertexOrder().push_back(graph.vertices().size() - 1);
    if (name == "end")
      continue;
    auto& instance = db.network().instances()[pin.get_instance_name()];
    instance.set_is_sequential(true);
    instance.set_clock_pin_name(name);
    instance.set_output_pin_name(name.substr(0, 1) + ":Q");
    point.set_is_clock_point(true);
    point.setLaunchClock("clk");
    auto& state = point.get_clock_state("clk");
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      state.waveforms[edge].arrival_map[AnalysisType::kMin][edge] = name == "a:CK" ? 1. : 1.2;
      state.waveforms[edge].arrival_map[AnalysisType::kMax][edge]
          = variation ? (name == "a:CK" ? 2. : 1.3) : state.waveforms[edge].arrival_map[AnalysisType::kMin][edge];
    }
    state.summarize();
    Arc arc;
    arc.set_type(ArcType::kCell);
    arc.set_source_pin(name);
    arc.set_sink_pin("end");
    for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
      for (auto edge : {TransType::kRise, TransType::kFall})
        arc.get_input_output_delay_map()[mode][edge][edge] = 0.;
    vertex.get_outgoing_arc_list().push_back(graph.arcs().size());
    graph.arcs().push_back(arc);
  }
  std::vector<std::string> starts{"a:CK", "b:CK"};
  StartpointInitializer(db, delay).seed(starts);
  DataArrivalPropagator(db).propagate();
  auto& paths = graph.points().at("end").get_path_state_map().at(AnalysisType::kMin).at(PathSourceType::kRegister).at(TransType::kRise);
  if (paths.size() != (variation ? 2u : 1u))
    throw std::runtime_error("CPPR launch contexts merged, or zero-variation paths unnecessarily separated");
  if (variation) {
    double worst = 100.;
    for (const auto& [tag, path] : paths) {
      std::string common;
      double cppr = ClockReconvergenceCalculator(db).correction(path.get_crpr_clock_pin(), TransType::kRise, "clk", "a:CK", TransType::kRise, "clk",
                                                                AnalysisType::kMin, common);
      worst = std::min(worst, path.get_arrival() - (2. - cppr));
    }
    near(worst, -.8, "arrival-only pruning lost the worst hold path");
  }
}
int main()
{
  Logger::initInst();
  reconvergentCandidates(true);
  reconvergentCandidates(false);
  Database db;
  Config config;
  auto& instance = db.network().instances()["buf"];
  instance.set_instance_name("buf");
  instance.set_cell_name("BUF");
  instance.get_pin_name_list() = {"buf:A", "buf:Y"};
  for (const auto& name : {std::string("in"), std::string("out"), std::string("buf:A"), std::string("buf:Y")}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_is_port(name == "in" || name == "out");
    pin.set_pin_name(pin.get_is_port() ? name : name.substr(4));
    pin.set_instance_name(pin.get_is_port() ? "" : "buf");
    pin.set_direction(name == "in" || name == "buf:A" ? PinDirection::kInput : PinDirection::kOutput);
    pin.set_net_name(name == "in" || name == "buf:A" ? "n1" : "n2");
  }
  for (auto name : {"n1", "n2"}) {
    auto& net = db.network().nets()[name];
    net.set_net_name(name);
    net.get_pin_name_list() = std::string(name) == "n1" ? std::vector<std::string>{"in", "buf:A"} : std::vector<std::string>{"buf:Y", "out"};
  }
  auto& cell = db.liberty().get_cell_map()["BUF"];
  cell.get_port_map()["A"].set_is_input(true);
  cell.get_port_map()["Y"].set_is_output(true);
  TimingCellArc cell_arc;
  cell_arc.set_source_port("A");
  cell_arc.set_sink_port("Y");
  TimingArc table;
  table.set_sense(TimingArcSense::kPositive);
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    table.get_delay_table_map()[edge].set_value_list({2.});
    table.get_slew_table_map()[edge].set_value_list({.4});
  }
  cell_arc.get_timing_arc_list().push_back(table);
  cell.get_cell_arc_list().push_back(cell_arc);
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.);
  clock.set_fall_edge(5.);
  std::vector<TimingIoDelay> inputs, outputs;
  for (auto edge : {TransType::kRise, TransType::kFall}) {
    TimingIoDelay io;
    io.set_clock_name("clk");
    io.set_trans_type(edge);
    io.set_delay(3.);
    inputs.push_back(io);
    io.set_delay(1.);
    outputs.push_back(io);
    for (auto mode : {AnalysisType::kMax, AnalysisType::kMin}) {
      db.sdc().get_port_constraint_map()["in"].set_input_transition(mode, edge, .2);
      db.sdc().timingDerates().set(mode, false, edge, DerateDelay::kCell, mode == AnalysisType::kMax ? 1.1 : .9);
      db.sdc().timingDerates().set(mode, true, edge, DerateDelay::kCell, 1.7);
    }
  }
  db.sdc().get_port_constraint_map()["in"].set_input_delays(inputs, false);
  db.sdc().get_port_constraint_map()["out"].set_output_delays(outputs, false);
  DelayCalculator delay(db, config);
  for (int repeat = 0; repeat != 2; ++repeat) {
    TimingAnalyzer(db, delay).analyze();
    for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        const auto& paths = db.graph().points().at("out").get_path_state_map().at(mode).at(PathSourceType::kInput).at(edge);
        if (paths.empty())
          throw std::runtime_error("no propagated paths");
        for (const auto& [tag, path] : paths)
          near(path.get_arrival(), mode == AnalysisType::kMax ? 5.2 : 4.8, "data derate or external delay changed");
        near(db.graph().points().at("buf:Y").get_data_slew_map().at(mode).at(edge), .4, "derate changed slew");
      }
    for (auto& arc : db.graph().arcs())
      if (arc.get_type() == ArcType::kCell)
        near(arc.get_graph_delay_map().at(AnalysisType::kMax).at(TransType::kRise).at(TransType::kRise), 2., "nominal cache contaminated");
  }
  // A negative nominal arc gets later under a late derate, never more negative.
  for (auto edge : {TransType::kRise, TransType::kFall})
    cell.get_cell_arc_list().front().get_timing_arc_list().front().get_delay_table_map()[edge].set_value_list({-2.});
  TimingAnalyzer(db, delay).analyze();
  for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
    for (auto edge : {TransType::kRise, TransType::kFall})
      for (const auto& [tag, path] : db.graph().points().at("out").get_path_state_map().at(mode).at(PathSourceType::kInput).at(edge))
        near(path.get_arrival(), mode == AnalysisType::kMax ? 1.2 : .8, "negative nominal derate reversed early/late ordering");
  Logger::destroyInst();
}
