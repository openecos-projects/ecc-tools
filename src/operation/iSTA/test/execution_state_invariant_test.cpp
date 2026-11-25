// SPDX-License-Identifier: MulanPSL-2.0
//
// P0 of the run_sta execution-side refactor: lock the semantics of the
// per-vertex timing-state structures before their containers change shape:
//   T2  ClockWaveform/ClockState sparse presence + summarize/physicalEdge
//       (locks the enum-grid conversion)
//   T3  getWorstPathState tolerance + ascending-tag tie-break (locks the
//       path-state map repack)
//   T4  Arc::update_timing_arc_delay fallback/zero-slew state machine (locks
//       the arc delay-grid conversion)
//   T5  arc endpoint vertex-index == name-resolution equivalence (locks
//       Graph::sourcePoint/sinkPoint switching to vertex indices)
#include <cmath>
#include <stdexcept>
#include <string>

#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "TimingAnalyzer.hpp"
#include "graph/TimingStateQuery.hpp"

using namespace ista;

namespace {

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

// T2a: absent and stored-zero are distinct, and a write never leaks across
// modes or edges.
void waveformPresence()
{
  ClockState state;
  require(!state.arrival(AnalysisType::kMax, TransType::kRise), "absent arrival read as valid");
  state.arrival_map[AnalysisType::kMax][TransType::kRise] = 0.0;
  require(state.arrival(AnalysisType::kMax, TransType::kRise) == 0.0, "stored zero read as absent");
  require(!state.arrival(AnalysisType::kMax, TransType::kFall), "write leaked to the other edge");
  require(!state.arrival(AnalysisType::kMin, TransType::kRise), "write leaked to the other mode");
  require(!state.slew(AnalysisType::kMax, TransType::kRise), "write leaked to another waveform field");
}

// T2b: summarize() merges source waveforms into the envelope picking the per
// mode/edge winner, and the predecessor bookkeeping follows the winning source.
void summarizeEnvelope()
{
  ClockState state;
  auto& rise = state.waveforms[TransType::kRise];
  rise.physical_arrival_map[AnalysisType::kMax][TransType::kRise] = 2.0;
  rise.physical_slew_map[AnalysisType::kMax][TransType::kRise] = 0.2;
  rise.arrival_map[AnalysisType::kMax][TransType::kRise] = 2.5;
  rise.slew_map[AnalysisType::kMax][TransType::kRise] = 0.25;
  rise.predecessor_map[AnalysisType::kMax][TransType::kRise] = "rise_src";
  auto& fall = state.waveforms[TransType::kFall];
  fall.physical_arrival_map[AnalysisType::kMax][TransType::kRise] = 3.0;
  fall.physical_slew_map[AnalysisType::kMax][TransType::kRise] = 0.15;
  fall.arrival_map[AnalysisType::kMax][TransType::kRise] = 1.5;
  fall.slew_map[AnalysisType::kMax][TransType::kRise] = 0.1;
  fall.predecessor_map[AnalysisType::kMax][TransType::kRise] = "fall_src";
  state.summarize();
  // Max envelope picks the larger value per field, per source waveform.
  require(state.physical_arrival_map.at(AnalysisType::kMax).at(TransType::kRise) == 3.0, "physical arrival envelope lost the worse value");
  require(state.physical_slew_map.at(AnalysisType::kMax).at(TransType::kRise) == 0.2, "physical slew envelope lost the worse value");
  require(state.arrival_map.at(AnalysisType::kMax).at(TransType::kRise) == 2.5, "arrival envelope lost the worse value");
  require(state.slew_map.at(AnalysisType::kMax).at(TransType::kRise) == 0.25, "slew envelope lost the worse value");
  require(state.predecessor_map.at(AnalysisType::kMax).at(TransType::kRise) == "rise_src", "predecessor did not follow the arrival winner");
  // The envelope carries nothing the source waveforms never had.
  require(!state.arrival(AnalysisType::kMin, TransType::kRise), "envelope fabricated a min value");
  require(!state.arrival(AnalysisType::kMax, TransType::kFall), "envelope fabricated a fall value");
}

// T2c: physicalEdge extracts exactly one edge of the physical bookkeeping.
void physicalEdgeExtraction()
{
  ClockWaveform wave;
  wave.physical_arrival_map[AnalysisType::kMax][TransType::kRise] = 1.0;
  wave.physical_arrival_map[AnalysisType::kMax][TransType::kFall] = 2.0;
  wave.physical_predecessor_map[AnalysisType::kMax][TransType::kFall] = "pred_fall";
  wave.predecessor_vertex_map[AnalysisType::kMax][TransType::kRise] = 7;
  wave.arrival_map[AnalysisType::kMax][TransType::kRise] = 9.0;  // effective, not physical
  const ClockWaveform rise = wave.physicalEdge(TransType::kRise);
  require(rise.physicalArrival(AnalysisType::kMax, TransType::kRise) == 1.0, "physicalEdge lost the selected edge");
  require(!rise.physicalArrival(AnalysisType::kMax, TransType::kFall), "physicalEdge kept the other edge");
  require(!rise.physicalArrival(AnalysisType::kMin, TransType::kRise), "physicalEdge fabricated the other mode");
  require(rise.physical_predecessor_map.empty(), "physicalEdge kept the other edge's predecessor");
  require(rise.predecessor_vertex_map.at(AnalysisType::kMax).at(TransType::kRise) == 7, "physicalEdge lost the vertex link");
  require(rise.arrival_map.empty(), "physicalEdge copied effective values");
}

// T2d: clearing the effective envelope and republishing from the physical
// values leaves no stale effective entries (the publishClock pattern).
void clearAndRepublish()
{
  ClockState state;
  state.physical_arrival_map[AnalysisType::kMax][TransType::kRise] = 1.0;
  state.arrival_map[AnalysisType::kMax][TransType::kRise] = 2.0;
  state.arrival_map[AnalysisType::kMin][TransType::kRise] = 0.5;  // stale effective-only entry
  state.arrival_map.clear();
  state.arrival_map = state.physical_arrival_map;
  require(state.arrival(AnalysisType::kMax, TransType::kRise) == 1.0, "republish lost the physical value");
  require(!state.arrival(AnalysisType::kMin, TransType::kRise), "republish kept a stale effective entry");
}

// T3: worst-path selection iterates the tag map in ascending tag id and a
// candidate only replaces the incumbent when it beats it by more than
// kTimingTolerance (1e-6) — so within-tolerance ties keep the lowest tag id,
// and the comparison is always against the current incumbent, not the best
// arrival seen so far.
void pathStateTieBreak()
{
  Database db;
  auto& point = db.graph().points()["out"];
  TimingStateQuery query(db);
  auto& states = query.getPathStateMap(point, AnalysisType::kMax, PathSourceType::kRegister, TransType::kRise);
  // Inserted in scrambled order; iteration is by tag id regardless.
  states[9].set_arrival(1.0 + 0.5E-6);
  states[2].set_arrival(1.0);
  states[7].set_arrival(1.0 + 1.5E-6);
  // Iteration order 2 -> 7 -> 9: incumbent 1.0; 1.0+1.5e-6 > 1.0+1e-6 wins.
  // The states carry no tag field set above; identify by arrival instead.
  const TimingPathState* worst = query.getWorstPathState(point, AnalysisType::kMax, PathSourceType::kRegister, TransType::kRise);
  require(worst != nullptr && std::abs(worst->get_arrival() - (1.0 + 1.5E-6)) < 1E-12, "strictly-better candidate did not win");

  // Within tolerance, the first state in ascending tag order survives even
  // against a better value arriving later.
  auto& states2 = query.getPathStateMap(point, AnalysisType::kMax, PathSourceType::kRegister, TransType::kFall);
  states2[4].set_arrival(2.0);
  states2[1].set_arrival(2.0 + 0.5E-6);
  const TimingPathState* tie = query.getWorstPathState(point, AnalysisType::kMax, PathSourceType::kRegister, TransType::kFall);
  require(tie != nullptr && std::abs(tie->get_arrival() - (2.0 + 0.5E-6)) < 1E-12, "within-tolerance tie broke past the first tag");

  // kMin mirrors with inverted comparison.
  auto& states3 = query.getPathStateMap(point, AnalysisType::kMin, PathSourceType::kRegister, TransType::kRise);
  states3[3].set_arrival(1.0);
  states3[8].set_arrival(1.0 - 1.5E-6);
  const TimingPathState* best_min = query.getWorstPathState(point, AnalysisType::kMin, PathSourceType::kRegister, TransType::kRise);
  require(best_min != nullptr && std::abs(best_min->get_arrival() - (1.0 - 1.5E-6)) < 1E-12, "min-mode strict improvement lost");

  // Non-finite arrivals are skipped.
  auto& states4 = query.getPathStateMap(point, AnalysisType::kMin, PathSourceType::kRegister, TransType::kFall);
  states4[1].set_arrival(-std::numeric_limits<double>::infinity());
  states4[2].set_arrival(3.0);
  require(query.getWorstPathState(point, AnalysisType::kMin, PathSourceType::kRegister, TransType::kFall)->get_arrival() == 3.0,
          "non-finite arrival won worst-path selection");
}

// T4: Arc::update_timing_arc_delay — the fallback/zero-slew state machine.
double arcDelayAt(Arc& arc, int32_t variant, AnalysisType mode, TransType in, TransType out)
{
  return arc.get_timing_arc_delay_map().at(variant).at(mode).at(in).at(out);
}

void arcDelayStateMachine()
{
  {
    Arc arc;
    // Initialization write stores and marks the key as fallback.
    arc.update_timing_arc_delay(0, AnalysisType::kMax, TransType::kRise, TransType::kRise, 5.0, true);
    require(arcDelayAt(arc, 0, AnalysisType::kMax, TransType::kRise, TransType::kRise) == 5.0, "initialization write not stored");
    // Further initialization writes fall back to the monotonic rule while the
    // key is still fallback-marked (max keeps the larger).
    arc.update_timing_arc_delay(0, AnalysisType::kMax, TransType::kRise, TransType::kRise, 7.0, true);
    arc.update_timing_arc_delay(0, AnalysisType::kMax, TransType::kRise, TransType::kRise, 3.0, true);
    require(arcDelayAt(arc, 0, AnalysisType::kMax, TransType::kRise, TransType::kRise) == 7.0, "init-phase monotonic rule changed");
    // The first real (non-init) write replaces the fallback unconditionally.
    arc.update_timing_arc_delay(0, AnalysisType::kMax, TransType::kRise, TransType::kRise, 4.0, false);
    require(arcDelayAt(arc, 0, AnalysisType::kMax, TransType::kRise, TransType::kRise) == 4.0, "real write did not replace the fallback");
    // After replacement, initialization writes are ignored entirely.
    arc.update_timing_arc_delay(0, AnalysisType::kMax, TransType::kRise, TransType::kRise, 9.0, true);
    require(arcDelayAt(arc, 0, AnalysisType::kMax, TransType::kRise, TransType::kRise) == 4.0, "late initialization write leaked in");
    // Real writes follow the monotonic rule: max keeps the larger.
    arc.update_timing_arc_delay(0, AnalysisType::kMax, TransType::kRise, TransType::kRise, 6.0, false);
    arc.update_timing_arc_delay(0, AnalysisType::kMax, TransType::kRise, TransType::kRise, 2.0, false);
    require(arcDelayAt(arc, 0, AnalysisType::kMax, TransType::kRise, TransType::kRise) == 6.0, "max monotonic rule changed");
  }
  {
    Arc arc;
    // A zero-slew probe is replaced by the first real-slew evaluation
    // regardless of the monotonic direction (min mode: the probe is smaller).
    arc.update_timing_arc_delay(1, AnalysisType::kMin, TransType::kFall, TransType::kFall, 2.0, false, true);
    require(arcDelayAt(arc, 1, AnalysisType::kMin, TransType::kFall, TransType::kFall) == 2.0, "zero-slew probe not stored");
    arc.update_timing_arc_delay(1, AnalysisType::kMin, TransType::kFall, TransType::kFall, 5.0, false, false);
    require(arcDelayAt(arc, 1, AnalysisType::kMin, TransType::kFall, TransType::kFall) == 5.0, "real evaluation did not replace the probe");
    // A probe arriving after a real value is an ordinary candidate (min rule).
    arc.update_timing_arc_delay(1, AnalysisType::kMin, TransType::kFall, TransType::kFall, 3.0, false, true);
    require(arcDelayAt(arc, 1, AnalysisType::kMin, TransType::kFall, TransType::kFall) == 3.0, "late probe bypassed the min rule");
    // Variants and edges are independent keys.
    require(arc.get_timing_arc_delay_map().count(0) == 0, "delay leaked across variants");
  }
}

// T5: for arcs with resolved vertex indices, vertex lookup and name lookup
// must reach the same TimingPoint; names-only arcs keep the name path.
void vertexIndexEquivalence()
{
  Database db;
  Config config;
  for (const auto* name : {"in", "out"}) {
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_pin_name(name);
    pin.set_is_port(true);
    pin.set_net_name("wire");
    pin.set_direction(std::string(name) == "in" ? PinDirection::kInput : PinDirection::kOutput);
  }
  auto& net = db.network().nets()["wire"];
  net.set_net_name("wire");
  net.set_pin_name_list({"in", "out"});
  auto& clock = db.sdc().get_clock_map()["V"];
  clock.set_clock_name("V");
  clock.set_period(10);
  clock.set_rise_edge(0);
  clock.set_fall_edge(2);
  db.sdc().get_port_constraint_map()["in"].set_input_transition(.05);
  DelayCalculator delay(db, config);
  TimingAnalyzer(db, delay).analyze();

  auto& graph = db.graph();
  std::size_t checked = 0;
  for (const auto& arc : graph.arcs()) {
    if (arc.get_source_vertex_idx() >= 0) {
      require(&graph.sourcePoint(arc) == &graph.vertices()[arc.get_source_vertex_idx()].get_timing_point(),
              "source vertex index and source pin name resolve to different points");
      ++checked;
    }
    if (arc.get_sink_vertex_idx() >= 0) {
      require(&graph.sinkPoint(arc) == &graph.vertices()[arc.get_sink_vertex_idx()].get_timing_point(),
              "sink vertex index and sink pin name resolve to different points");
      ++checked;
    }
  }
  require(checked > 0, "no arc carried a resolved vertex index; equivalence unchecked");

  // A names-only arc (no vertex indices, the test-fixture pattern) still
  // resolves through the name path.
  Arc bare;
  bare.set_type(ArcType::kNet);
  bare.set_source_pin("in");
  bare.set_sink_pin("out");
  require(&graph.sourcePoint(bare) == &graph.driverPoint("in"), "names-only arc lost name-based source resolution");
  require(&graph.sinkPoint(bare) == &graph.points().at("out"), "names-only arc lost name-based sink resolution");
}

}  // namespace

int main()
{
  waveformPresence();
  summarizeEnvelope();
  physicalEdgeExtraction();
  clearAndRepublish();
  pathStateTieBreak();
  arcDelayStateMachine();
  vertexIndexEquivalence();
}
