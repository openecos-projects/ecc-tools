// SPDX-License-Identifier: MulanPSL-2.0
#include "StructuralTimingStateBuilder.hpp"
#include "StartpointInitializer.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Database.hpp"
#include "graph/Arc.hpp"
#include "graph/Graph.hpp"
#include "graph/TimingPoint.hpp"
#include "graph/TimingStateQuery.hpp"
#include "liberty/LibertyCell.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "liberty/TimingArc.hpp"
#include "liberty/TimingCellArc.hpp"
#include "graph_builder/GraphBuilder.hpp"
#include "timing_propagator/clock/ClockNetworkBuilder.hpp"
#include "timing_propagator/data/TransitionFilter.hpp"
#include "network/Network.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
namespace {

constexpr double kModelReferenceArrival = 0.0;
constexpr double kModelReferenceSlew = 0.0;

TransType opposite(TransType transition)
{
  return transition == TransType::kRise ? TransType::kFall : TransType::kRise;
}

std::vector<TransType> cellOutputTransitions(Database& database, Arc& arc, TransType input)
{
  std::set<TransType> result;
  if (arc.libertyArcId()) {
    if (auto* cell_arc = database.liberty().findCellArc(*arc.libertyArcId()); cell_arc != nullptr) {
      for (const TimingArc& timing_arc : cell_arc->get_timing_arc_list()) {
        const auto trigger = timing_arc.get_trigger_trans_type();
        if (trigger != TransType::kNone && trigger != input)
          continue;
        switch (timing_arc.get_sense()) {
          case TimingArcSense::kPositive:
            result.insert(input);
            break;
          case TimingArcSense::kNegative:
            result.insert(opposite(input));
            break;
          case TimingArcSense::kNonUnate:
          default:
            result.insert(TransType::kRise);
            result.insert(TransType::kFall);
            break;
        }
      }
    }
  }
  if (result.empty()) {
    result.insert(TransType::kRise);
    result.insert(TransType::kFall);
  }
  return {result.begin(), result.end()};
}

void propagate(Database& database, ExceptionThroughIndex& through_index)
{
  ExceptionMatcher matcher(database.sdc(), database.network());
  TimingStateQuery path_query(database);
  for (const auto vertex_index : database.graph().vertexOrder()) {
    auto& vertex = database.graph().vertices()[vertex_index];
    for (const auto arc_index : vertex.get_outgoing_arc_list()) {
      Arc& arc = database.graph().arcs()[arc_index];
      if (path_query.isDisableArc(arc) || path_query.shouldStopDataPropagation(arc))
        continue;
      auto& source = database.graph().sourcePoint(arc);
      auto& sink = database.graph().sinkPoint(arc);
      for (const auto analysis : {AnalysisType::kMax, AnalysisType::kMin}) {
        for (const auto source_type : {PathSourceType::kInput, PathSourceType::kRegister}) {
          for (const auto input_transition : {TransType::kRise, TransType::kFall}) {
            if (!allowsTransition(database.network(), database.graph().simValues(), arc.get_source_pin(), input_transition,
                                  arc.get_type() == ArcType::kNet))
              continue;
            const auto source_analysis = source.get_path_state_map().find(analysis);
            if (source_analysis == source.get_path_state_map().end())
              continue;
            const auto source_type_it = source_analysis->second.find(source_type);
            if (source_type_it == source_analysis->second.end())
              continue;
            const auto path_it = source_type_it->second.find(input_transition);
            if (path_it == source_type_it->second.end())
              continue;
            const auto outputs = arc.get_type() == ArcType::kCell ? cellOutputTransitions(database, arc, input_transition)
                                                                   : std::vector<TransType>{input_transition};
            for (const auto& [source_tag, source_state] : path_it->second) {
              if (source_state.get_is_clock_source_path())
                continue;
              for (const auto output_transition : outputs) {
                if (!allowsTransition(database.network(), database.graph().simValues(), arc.get_sink_pin(), output_transition,
                                      arc.get_type() == ArcType::kCell))
                  continue;
                const auto progress = matcher.advanceState(through_index, source_state.get_exception_state_list(), arc.get_sink_pin(), output_transition,
                                                           arc.get_type() == ArcType::kNet ? arc.get_source_pin() : "");
                const auto tag = matcher.makePathStateTag(source_state.get_clock_name(), source_state.get_launch_clock_trans_type(), progress,
                                                          source_state.get_is_clock_source_path(), source_state.crprPathTag(),
                                                          source_state.get_launch_clock_edge_index());
                auto& target_map = sink.get_path_state_map()[analysis][source_type][output_transition];
                if (target_map.contains(tag))
                  continue;
                TimingPathState target = source_state;
                target.set_arrival(kModelReferenceArrival);
                target.set_slew(kModelReferenceSlew);
                target.set_predecessor(arc.get_source_pin());
                target.set_predecessor_arc_idx(arc_index);
                target.set_predecessor_arc_delay(0.0);
                target.set_predecessor_path_state_tag(source_tag);
                target.set_exception_state_list(progress);
                target.set_path_state_tag(tag);
                target.set_trans_type(output_transition);
                target.set_predecessor_trans_type(input_transition);
                target_map.emplace(tag, std::move(target));
              }
            }
          }
        }
      }
    }
  }
}

}  // namespace

void StructuralTimingStateBuilder::build(Database& database, DelayCalculator& delay)
{
  database.graph().clearTiming();
  ClockNetworkBuilder(database).build();
  for (auto& [name, point] : database.graph().points())
    for (auto& [clock, state] : point.get_clock_state_map())
      state.is_structural = true;
  for (auto& [name, point] : database.graph().driverPoints())
    for (auto& [clock, state] : point.get_clock_state_map())
      state.is_structural = true;
  ExceptionThroughIndex through_index(database.sdc(), database.network());
  // Reuse the same boundary seeding as complete STA. In particular, min
  // input delays and ideal clock latency/transition must not become zeros.
  // This evaluates only necessary boundary queries, never the data graph.
  StartpointInitializer(database, delay).seed(database.graph().startpoints());
  propagate(database, through_index);
}

}  // namespace ista
