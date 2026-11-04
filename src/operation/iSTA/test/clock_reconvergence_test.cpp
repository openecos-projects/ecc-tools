// SPDX-License-Identifier: MulanPSL-2.0
#include <stdexcept>
#include <string>

#include "ClockReconvergenceCalculator.hpp"
#include "graph/TimingStateQuery.hpp"

namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void inoutSource()
{
  using namespace ista;
  Database db;
  auto& graph = db.graph();
  for (const auto* name : {"clk", "launch", "capture"}) {
    auto& pin = db.network().pins()[name];
    pin.set_is_port(true);
    pin.set_load_vertex_idx(graph.vertices().size());
    auto& vertex = graph.vertices().emplace_back();
    vertex.set_pin_name(name);
    vertex.set_timing_point(&graph.points()[name]);
  }
  auto& pin = db.network().pins().at("clk");
  pin.set_direction(PinDirection::kInout);
  pin.set_driver_vertex_idx(graph.vertices().size());
  auto& driver = graph.vertices().emplace_back();
  driver.set_pin_name("clk");
  driver.set_is_driver(true);
  driver.set_timing_point(&graph.driverPoints()["clk"]);
  auto& clock = db.sdc().get_clock_map()["core"];
  clock.set_source_list({"clk"});
  clock.set_is_propagated(true);
  for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
    auto& wave = graph.driverPoints().at("clk").get_clock_state("core").waveforms[TransType::kFall];
    wave.arrival_map[mode][TransType::kFall] = mode == AnalysisType::kMin ? 1. : 2.;
    wave.physical_slew_map[mode][TransType::kFall] = .1;
    for (const auto* name : {"launch", "capture"}) {
      auto& sink = graph.points().at(name).get_clock_state("core").waveforms[TransType::kFall];
      sink.arrival_map[mode][TransType::kRise] = wave.arrival_map.at(mode).at(TransType::kFall) + .2;
      sink.predecessor_map[mode][TransType::kRise] = "clk";
      sink.predecessor_vertex_map[mode][TransType::kRise] = pin.get_driver_vertex_idx();
      sink.predecessor_trans_type_map[mode][TransType::kRise] = TransType::kFall;
    }
  }
  std::string common;
  const auto correction = ClockReconvergenceCalculator(db).correction("launch", TransType::kRise, "core", "capture", TransType::kRise, "core",
                                                                      AnalysisType::kMax, common, TransType::kFall, TransType::kFall);
  require(correction == 1. && common == "clk", "inout common source lost its phase-specific driver CPPR");
  require(clockSourceTransitions(db, "clk", "core", AnalysisType::kMax, TransType::kFall) == std::vector<TransType>{TransType::kFall},
          "inout clock source query ignored its driver");
  require(TimingStateQuery(db).clockPathHasVariation("launch", "core", TransType::kRise, TransType::kFall),
          "clock variation query lost the inout common source");
  TimingIoDelay delay;
  delay.set_reference_pin("clk");
  require(resolveIoDelays(db, {&delay}).front().get_clock_name() == "core", "inout reference pin lost its clock");
  clock.set_is_propagated(false);
  require(isIdealClockPin(db, "clk"), "inout driver clock was not recognized as ideal");
  clock.set_is_propagated(true);
  require(!isIdealClockPin(db, "clk"), "propagated inout clock was exempted from electrical checks");
}
}  // namespace

int main()
{
  inoutSource();
  using namespace ista;
  Database database;
  auto& graph = database.graph();
  for (const auto* name : {"root", "launch", "capture"}) {
    auto& pin = database.network().pins()[name];
    pin.set_is_port(true);
    pin.set_load_vertex_idx(graph.vertices().size());
    GraphVertex vertex;
    vertex.set_pin_name(name);
    vertex.set_timing_point(&graph.points()[name]);
    graph.vertices().push_back(vertex);
  }
  for (const auto* clock : {"launch_clock", "capture_clock"}) {
    auto& root = graph.points()["root"].get_clock_state(clock);
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      root.arrival_map[AnalysisType::kMin][edge] = 2.0;
      root.arrival_map[AnalysisType::kMax][edge] = edge == TransType::kRise ? 5.0 : 4.0;
    }
    for (const auto* name : {"launch", "capture"}) {
      auto& state = graph.points()[name].get_clock_state(clock);
      for (auto analysis : {AnalysisType::kMin, AnalysisType::kMax}) {
        for (auto edge : {TransType::kRise, TransType::kFall}) {
          state.predecessor_map[analysis][edge] = "root";
          state.predecessor_vertex_map[analysis][edge] = 0;
          state.predecessor_trans_type_map[analysis][edge] = edge;
        }
      }
    }
  }
  // Two sinks sharing a final clock buffer must retain that buffer in CPPR.
  auto& buffer = database.network().instances()["buffer"];
  buffer.set_is_sequential(false);
  buffer.get_pin_name_list() = {"root"};
  auto& root_pin = database.network().pins()["root"];
  root_pin.set_is_port(false);
  root_pin.set_instance_name("buffer");
  root_pin.set_net_name("leaves");
  root_pin.set_direction(PinDirection::kOutput);
  auto& net = database.network().nets()["leaves"];
  net.get_load_pin_list() = {"launch", "capture"};
  for (const auto* name : {"launch", "capture"}) {
    auto& pin = database.network().pins()[name];
    pin.set_is_port(false);
    pin.set_instance_name(name);
    auto& instance = database.network().instances()[name];
    instance.set_is_sequential(true);
    instance.set_clock_pin_name(name);
  }
  auto& source_pin = database.network().pins()["source"];
  source_pin.set_is_port(true);
  source_pin.set_load_vertex_idx(3);
  auto& source_vertex = graph.vertices().emplace_back();
  source_vertex.set_pin_name("source");
  source_vertex.set_timing_point(&graph.points()["source"]);
  for (const auto* clock : {"launch_clock", "capture_clock"})
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        auto& state = graph.points()["root"].get_clock_state(clock);
        state.predecessor_map[mode][edge] = "source";
        state.predecessor_vertex_map[mode][edge] = 3;
        state.predecessor_trans_type_map[mode][edge] = edge;
      }
  ClockReconvergenceCalculator crpr(database);
  std::string common;
  require(crpr.correction("launch", TransType::kRise, "launch_clock", "capture", TransType::kRise, "capture_clock", AnalysisType::kMax, common) == 3.0
              && common == "root",
          "common root min/max correction changed");
  common.clear();
  require(crpr.correction("launch", TransType::kRise, "launch_clock", "capture", TransType::kFall, "capture_clock", AnalysisType::kMax, common) == 2.0
              && common == "root",
          "opposite transitions must use the smaller common-path correction");
  common.clear();
  require(crpr.correction("launch", TransType::kRise, "missing", "capture", TransType::kRise, "capture_clock", AnalysisType::kMin, common) == 0.0,
          "missing clock acquired a predecessor");
  for (const auto* clock : {"launch_clock", "capture_clock"}) {
    database.sdc().get_clock_map()[clock].set_source_list({"source"});
    for (auto& [name, point] : graph.points()) {
      auto& state = point.get_clock_state(clock);
      state.physical_predecessor_map = state.predecessor_map;
      state.physical_predecessor_trans_type_map = state.predecessor_trans_type_map;
    }
  }
  database.sdc().clockPinConstraints()["root"][""].propagated = true;
  require(crpr.correction("launch", TransType::kRise, "launch_clock", "capture", TransType::kRise, "capture_clock", AnalysisType::kMax, common) == 0.0,
          "internal pin propagated clock incorrectly acquired CRPR");
  database.sdc().clockPinConstraints().clear();
  database.sdc().clockPinConstraints()["source"][""].propagated = true;
  require(crpr.correction("launch", TransType::kRise, "launch_clock", "capture", TransType::kRise, "capture_clock", AnalysisType::kMax, common) == 3.0,
          "common clock-source propagated port lost CRPR");
  database.sdc().clockPinConstraints().clear();
  graph.points()["root"].get_clock_state("launch_clock").arrival_map.erase(AnalysisType::kMin);
  require(crpr.correction("launch", TransType::kRise, "launch_clock", "capture", TransType::kRise, "capture_clock", AnalysisType::kMax, common) == 0.0,
          "missing early waveform fabricated a CPPR correction");
}
