// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "Logger.hpp"
#include "TimingAnalyzer.hpp"
#include "timing_propagator/RequiredPropagator.hpp"
#include "timing_checker/ClockReconvergenceCalculator.hpp"
#include "timing_propagator/data/DataArrivalPropagator.hpp"
#include "timing_propagator/data/StartpointInitializer.hpp"
using namespace ista;
static void near(double actual, double expected, const char* message)
{
  if (actual != expected && (!std::isfinite(actual) || !std::isfinite(expected) || std::abs(actual - expected) > 1e-9))
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
// A far-behind launch must not fan out through the entire data cone. Its
// exclusive upstream pin still needs the exact capture requirement after pruning.
static void dominatedLaunch(double downstream = 0., bool opposite_capture = false)
{
  Database db;
  Config config;
  auto& graph = db.graph();
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.);
  clock.set_fall_edge(5.);
  std::vector<std::string> starts{"a:CK", "b:CK", "c:CK"};
  for (const auto& name : {std::string("a:CK"), std::string("b:CK"), std::string("c:CK"), std::string("end")}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_load_vertex_idx(graph.vertices().size());
    pin.set_instance_name(name.substr(0, 1));
    auto& point = graph.points()[name];
    auto& vertex = graph.vertices().emplace_back();
    vertex.set_pin_name(name);
    vertex.set_timing_point(&point);
    graph.vertexOrder().push_back(graph.vertices().size() - 1);
    if (name == "end") {
      pin.set_is_port(true);
      pin.set_direction(PinDirection::kOutput);
      continue;
    }
    auto& instance = db.network().instances()[pin.get_instance_name()];
    instance.set_is_sequential(true);
    instance.set_clock_pin_name(name);
    instance.set_output_pin_name(name.substr(0, 1) + ":Q");
    point.set_is_clock_point(true);
    point.setLaunchClock("clk");
    auto& state = point.get_clock_state("clk");
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      state.waveforms[edge].arrival_map[AnalysisType::kMin][edge] = name == "a:CK" ? 1. : name == "b:CK" ? 1.2 : 0.;
      state.waveforms[edge].arrival_map[AnalysisType::kMax][edge] = name == "a:CK" ? 2. : name == "b:CK" ? 1.3 : .05;
    }
    state.summarize();
    Arc arc;
    arc.set_type(ArcType::kCell);
    arc.set_source_pin(name);
    arc.set_sink_pin("end");
    arc.set_source_vertex_idx(graph.vertices().size() - 1);
    arc.set_sink_vertex_idx(3);
    for (auto mode : {AnalysisType::kMax, AnalysisType::kMin})
      for (auto edge : {TransType::kRise, TransType::kFall})
        arc.get_input_output_delay_map()[mode][edge][edge] = .1;
    vertex.get_outgoing_arc_list().push_back(graph.arcs().size());
    graph.arcs().push_back(arc);
  }
  graph.endpoints().push_back("end");
  std::vector<TimingIoDelay> outputs;
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      TimingIoDelay io;
      io.set_analysis_type(mode);
      io.set_clock_name("clk");
      io.set_trans_type(edge);
      if (opposite_capture)
        io.set_clock_trans_type(edge);
      io.set_reference_pin("a:CK");
      io.set_delay(0.);
      outputs.push_back(io);
    }
  db.sdc().get_port_constraint_map()["end"].set_output_delays(outputs, false);
  if (downstream > 0.) {
    auto& pin = db.network().pins()["other"];
    pin.set_full_name("other");
    pin.set_is_port(true);
    pin.set_direction(PinDirection::kOutput);
    pin.set_load_vertex_idx(4);
    auto& vertex = graph.vertices().emplace_back();
    vertex.set_pin_name("other");
    vertex.set_timing_point(&graph.points()["other"]);
    graph.vertexOrder().push_back(4);
    graph.endpoints().push_back("other");
    Arc arc;
    arc.set_type(ArcType::kNet);
    arc.set_source_pin("end");
    arc.set_sink_pin("other");
    arc.set_source_vertex_idx(3);
    arc.set_sink_vertex_idx(4);
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall})
        arc.get_input_output_delay_map()[mode][edge][edge] = downstream;
    graph.vertices()[3].get_outgoing_arc_list().push_back(graph.arcs().size());
    graph.arcs().push_back(arc);
    for (auto& io : outputs)
      io.set_reference_pin("b:CK");
    db.sdc().get_port_constraint_map()["other"].set_output_delays(outputs, false);
  }
  clock.set_is_propagated(true);
  if (!TimingStateQuery(db).canPrunePathStates())
    throw std::runtime_error("single-capture fixture unexpectedly disabled pruning");
  db.sdc().clockPinConstraints()["a:CK"]["clk"].propagated = true;
  if (TimingStateQuery(db).canPrunePathStates())
    throw std::runtime_error("pin-specific clock constraints incorrectly enabled pruning");
  db.sdc().clockPinConstraints().clear();
  db.sdc().get_clock_map()["second"] = clock;
  if (!TimingStateQuery(db).canPrunePathStates())
    throw std::runtime_error("unused clock incorrectly disabled pruning");
  db.sdc().get_clock_map().erase("second");
  DelayCalculator delay(db, config);
  StartpointInitializer(db, delay).seed(starts);
  Database exhaustive = db;
  for (auto& [name, point] : exhaustive.graph().points())
    for (auto& [mode, sources] : point.get_path_state_map())
      for (auto& [source, edges] : sources)
        for (auto& [edge, paths] : edges)
          for (auto& [tag, path] : paths)
            path.setClockPathBound(std::numeric_limits<double>::infinity());
  DelayCalculator exhaustive_delay(exhaustive, config);
  DataArrivalPropagator(exhaustive).propagate();
  PathChecker exhaustive_check(exhaustive, exhaustive_delay);
  RequiredPropagator(exhaustive, exhaustive_check).propagate();
  DataArrivalPropagator(db).propagate();
  const auto& states = graph.points().at("end").get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kRegister).at(TransType::kRise);
  if (states.size() != 2)
    throw std::runtime_error("provably dominated launch was propagated instead of bounded CPPR pruning");
  PathChecker checker(db, delay);
  RequiredPropagator(db, checker).propagate();
  if (downstream == 0. && !opposite_capture) {
    near(graph.points().at("c:CK").get_required(), 10.9, "pruned launch lost its upstream required time");
    near(graph.points().at("c:CK").get_slack(), 10.85, "pruned launch upstream slack is wrong");
    near(graph.points().at("end").get_slack(), 9.6, "CPPR-recovered non-worst arrival was pruned");
  }
  auto worst = [](Database& design, PathChecker& checks, const std::string& endpoint, AnalysisType mode) {
    double result = std::numeric_limits<double>::infinity();
    for (const auto& [source, edges] : design.graph().points().at(endpoint).get_path_state_map().at(mode))
      for (const auto& [edge, paths] : edges)
        for (const auto& [tag, path] : paths)
          if (const auto end = checks.checkPath(path, endpoint, mode))
            result = std::min(result, end->slack());
    if (!std::isfinite(result))
      throw std::runtime_error("fixture has no checked setup/hold path");
    return result;
  };
  for (const auto& endpoint : graph.endpoints())
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      near(worst(db, checker, endpoint, mode), worst(exhaustive, exhaustive_check, endpoint, mode), "bounded/exhaustive setup or hold slack differ");
  for (const auto& [name, point] : graph.points()) {
    const auto& reference = exhaustive.graph().points().at(name);
    near(point.get_required(), reference.get_required(), "bounded/exhaustive pin required differ");
    near(point.get_slack(), reference.get_slack(), "bounded/exhaustive pin slack differ");
  }
  // Reusing a propagator after a clock update must recompute its correction.
  // With opposite_capture, the data edges have different capture clock edges.
  RequiredPropagator required(db, checker);
  for (const double late : {2.25, 1.75}) {
    for (Database* design : {&db, &exhaustive}) {
      auto& state = design->graph().points().at("a:CK").get_clock_state("clk");
      state.waveforms.at(TransType::kRise).arrival_map[AnalysisType::kMax][TransType::kRise] = late;
      state.waveforms.at(TransType::kFall).arrival_map[AnalysisType::kMax][TransType::kFall] = late - .125;
      state.summarize();
    }
    required.propagate();
    RequiredPropagator(exhaustive, exhaustive_check).propagate();
    if (opposite_capture) {
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        bool checked = false;
        for (const auto& [tag, path] : graph.points().at("end").get_path_state_map().at(AnalysisType::kMax).at(PathSourceType::kRegister).at(edge)) {
          if (path.get_crpr_clock_pin() != "a:CK")
            continue;
          const auto end = checker.checkPath(path, "end", AnalysisType::kMax);
          if (!end || end->check().capture_transition != edge || end->check().capture_pin_transition != edge)
            throw std::runtime_error("opposite-edge fixture did not select its capture waveform");
          near(end->check().reconvergence, late - (edge == TransType::kRise ? 1. : 1.125), "capture edges lost their distinct correction");
          checked = true;
        }
        if (!checked)
          throw std::runtime_error("opposite-edge fixture has no retained launch path");
      }
    }
    for (const auto& [name, point] : graph.points()) {
      const auto& reference = exhaustive.graph().points().at(name);
      near(point.get_required(), reference.get_required(), "clock update retained a stale required correction");
      near(point.get_slack(), reference.get_slack(), "clock update retained a stale slack correction");
    }
  }
}
// A witness can lose almost one comparison tolerance at each downstream merge.
static void cumulativeMergeTolerance(bool hold)
{
  Database db;
  Config config;
  auto& graph = db.graph();
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.);
  clock.set_fall_edge(5.);
  clock.set_is_propagated(true);
  const std::vector<std::string> names{"a:CK", "b:CK", "u", "m1", "m2", "m3", "m4", "m5", "m6", "end"};
  for (const auto& name : names) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_load_vertex_idx(graph.vertices().size());
    pin.set_instance_name(name.substr(0, 1));
    auto& point = graph.points()[name];
    auto& vertex = graph.vertices().emplace_back();
    vertex.set_pin_name(name);
    vertex.set_timing_point(&point);
    graph.vertexOrder().push_back(graph.vertices().size() - 1);
    if (name == "end") {
      pin.set_is_port(true);
      pin.set_direction(PinDirection::kOutput);
    }
    if (name == "a:CK" || name == "b:CK") {
      auto& instance = db.network().instances()[pin.get_instance_name()];
      instance.set_is_sequential(true);
      instance.set_clock_pin_name(name);
      instance.set_output_pin_name(name.substr(0, 1) + ":Q");
      point.set_is_clock_point(true);
      point.setLaunchClock("clk");
      auto& state = point.get_clock_state("clk");
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        const double early = name == "a:CK" ? .9 : .900005;
        const double late = name == "a:CK" ? 1. : .900005;
        state.waveforms[edge].arrival_map[AnalysisType::kMin][edge] = hold ? 2. - late : early;
        state.waveforms[edge].arrival_map[AnalysisType::kMax][edge] = hold ? 2. - early : late;
      }
      state.summarize();
    }
  }
  auto add_arc = [&](std::size_t from, std::size_t to, double delay) {
    if (hold) {
      // Mirror the max fixture's ordering while retaining positive arc delays.
      auto offset = [](std::size_t vertex) { return vertex < 2 ? 0. : .00002 + (vertex - 2) * .2; };
      delay = offset(to) - offset(from) - delay;
    }
    Arc arc;
    arc.set_type(ArcType::kCell);
    arc.set_source_pin(names[from]);
    arc.set_sink_pin(names[to]);
    arc.set_source_vertex_idx(from);
    arc.set_sink_vertex_idx(to);
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall})
        arc.get_input_output_delay_map()[mode][edge][edge] = delay;
    graph.vertices()[from].get_outgoing_arc_list().push_back(graph.arcs().size());
    graph.vertices()[to].get_incoming_arc_list().push_back(graph.arcs().size());
    graph.arcs().push_back(arc);
  };
  add_arc(0, 2, .00001);
  add_arc(1, 2, 0.);
  for (std::size_t i = 3; i < names.size(); ++i) {
    add_arc(0, i, .00001 + (i - 2) * .1 - (i - 2) * .0000009);
    add_arc(i - 1, i, .1);
  }
  graph.endpoints().push_back("end");
  std::vector<TimingIoDelay> outputs;
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      TimingIoDelay io;
      io.set_analysis_type(mode);
      io.set_clock_name("clk");
      io.set_trans_type(edge);
      io.set_reference_pin("a:CK");
      io.set_delay(0.);
      outputs.push_back(io);
    }
  db.sdc().get_port_constraint_map()["end"].set_output_delays(outputs, false);
  std::vector<std::string> starts{"a:CK", "b:CK"};
  DelayCalculator delay(db, config);
  StartpointInitializer(db, delay).seed(starts);
  Database exhaustive = db;
  for (auto& [name, point] : exhaustive.graph().points())
    for (auto& [mode, sources] : point.get_path_state_map())
      for (auto& [source, edges] : sources)
        for (auto& [edge, paths] : edges)
          for (auto& [tag, path] : paths)
            path.setClockPathBound(std::numeric_limits<double>::infinity());
  DataArrivalPropagator(db).propagate();
  DataArrivalPropagator(exhaustive).propagate();
  DelayCalculator exhaustive_delay(exhaustive, config);
  PathChecker checker(db, delay), exhaustive_checker(exhaustive, exhaustive_delay);
  auto worst = [hold](Database& design, PathChecker& checker) {
    double slack = std::numeric_limits<double>::infinity();
    const auto mode = hold ? AnalysisType::kMin : AnalysisType::kMax;
    for (const auto& [edge, states] : design.graph().points().at("end").get_path_state_map().at(mode).at(PathSourceType::kRegister))
      for (const auto& [tag, state] : states)
        if (const auto end = checker.checkPath(state, "end", mode))
          slack = std::min(slack, end->slack());
    if (!std::isfinite(slack))
      throw std::runtime_error("tolerance fixture has no checked setup/hold path");
    return slack;
  };
  near(worst(db, checker), worst(exhaustive, exhaustive_checker), "successive tolerance merges lost the CPPR-critical launch");
}

int main()
{
  Logger::initInst();
  cumulativeMergeTolerance(false);
  cumulativeMergeTolerance(true);
  dominatedLaunch();
  dominatedLaunch(.25);  // Both capture contexts are needed for different launches.
  dominatedLaunch(2.);   // One capture frontier safely dominates the other.
  dominatedLaunch(0., true);
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
