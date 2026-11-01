// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <algorithm>
#include <string>

#include "graph/TimingStateQuery.hpp"
#include "sdc/ExceptionMatcher.hpp"

namespace ista {
// A forward dominance proof needs one admissible capture for each launch
// clock, and a required time independent of the particular launch latency.
// Unsupported captures keep their complete fanin for that launch domain.
class PathStatePruning
{
 public:
  explicit PathStatePruning(const Database& database) : _database(database), _matcher(database.sdc(), database.network()) {}

  bool canPruneTo(const std::string& endpoint, const std::string& launch) const
  {
    const auto& graph = _database.graph();
    for (const auto& [mode, sources] : graph.points().at(endpoint).get_path_state_map())
      for (const auto& [source, edges] : sources)
        for (const auto& [edge, states] : edges)
          if (!states.empty())
            return false;  // A seeded endpoint can retain a path-specific slew.
    const auto& pin = _database.network().pins().at(endpoint);
    const auto& sdc = _database.sdc();
    for (auto mode : {AnalysisType::kMin, AnalysisType::kMax})
      for (auto edge : {TransType::kRise, TransType::kFall}) {
        const auto capture_mode = mode == AnalysisType::kMax ? AnalysisType::kMin : AnalysisType::kMax;
        std::size_t count = 0;
        bool has_capture = false;
        auto admit = [&](const std::string& clock_pin, const std::string& clock, TransType local) {
          const auto definition = sdc.get_clock_map().find(clock);
          if (definition == sdc.get_clock_map().end())
            return false;
          const auto origins = clock_pin.empty() ? std::vector<TransType>{local}
                                                : clockSourceTransitions(_database, clock_pin, clock, capture_mode, local);
          for (auto origin : origins) {
            has_capture = true;
            // Empty exception progress deliberately evaluates only clock groups.
            if (_matcher.isFalsePath("", launch, endpoint, clock, mode, edge, origin, {}))
              continue;
            if (definition->second.edgeCount() != 2 || hasPathDelay(launch, endpoint, clock, mode, edge, origin) || ++count > 1)
              return false;
          }
          return true;
        };
        if (pin.get_is_port()) {
          const auto port = sdc.get_port_constraint_map().find(endpoint);
          if (port != sdc.get_port_constraint_map().end())
            for (const auto& delay : resolveIoDelays(_database, port->second.get_output_delays(mode, edge))) {
              auto local = delay.get_clock_trans_type();
              if (delay.get_level_sensitive() && mode == AnalysisType::kMin)
                local = local == TransType::kRise ? TransType::kFall : TransType::kRise;
              if (!admit(delay.get_reference_pin(), delay.get_clock_name(), local))
                return false;
            }
        } else {
          const auto instance = _database.network().instances().find(pin.get_instance_name());
          if (instance != _database.network().instances().end())
            for (const auto& arc : instance->second.get_check_arc_list()) {
              const auto type = arc.get_check_type();
              if (arc.get_data_port() != endpoint
                  || !(mode == AnalysisType::kMax ? type == TimingCheckType::kSetup || type == TimingCheckType::kRecovery
                                                  : type == TimingCheckType::kHold || type == TimingCheckType::kRemoval))
                continue;
              bool matches = arc.get_timing_arc_list().empty();
              for (const auto& timing : arc.get_timing_arc_list())
                matches |= timing.get_check_table_map().contains(edge);
              if (!matches)
                continue;
              const auto point = graph.points().find(arc.get_clock_port());
              if (point == graph.points().end())
                return false;
              bool clock_found = false;
              for (const auto& [clock, state] : point->second.get_clock_state_map())
                if (!state.is_source_path_only) {
                  clock_found = true;
                  if (!admit(arc.get_clock_port(), clock, arc.get_clock_trans_type()))
                    return false;
                }
              if (!clock_found)
                return false;
            }
        }
        // Path delays can constrain an endpoint even when no ordinary check
        // exists for this data edge. Its fallback clock may be inferred from
        // other constraints, so treat clock-qualified selectors conservatively.
        if (!has_capture && hasPathDelay(launch, endpoint, "", mode, edge, TransType::kNone, false))
          return false;
      }
    return true;
  }

 private:
  bool hasPathDelay(const std::string& launch, const std::string& endpoint, const std::string& capture, AnalysisType mode,
                    TransType data_edge, TransType capture_edge, bool capture_known = true) const
  {
    for (const auto& exception : _database.sdc().get_path_exception_list()) {
      if (exception.get_type() != (mode == AnalysisType::kMax ? TimingExceptionType::kMaxDelay : TimingExceptionType::kMinDelay)
          || !(data_edge == TransType::kRise ? exception.get_rise() : exception.get_fall()))
        continue;
      const auto& from = exception.get_from_objects();
      // Concrete from selectors may apply to some launches in this domain.
      // Pure clock selectors can exclude the domain without tracing paths.
      if (!from.empty() && std::all_of(from.begin(), from.end(), [](const auto& object) { return object.type == ExceptionObjectType::kClock; })
          && !from.contains({ExceptionObjectType::kClock, launch}))
        continue;
      const auto& to = exception.get_to_objects();
      if (!capture_known
          && std::any_of(to.begin(), to.end(), [](const auto& object) { return object.type == ExceptionObjectType::kClock; }))
        return true;
      if (!_matcher.matchesEndpointSelector(to, endpoint, capture, false))
        continue;
      if (exception.get_to_trans_type() != TransType::kNone) {
        const auto actual = to.contains({ExceptionObjectType::kClock, capture}) ? capture_edge : data_edge;
        if (actual != exception.get_to_trans_type())
          continue;
      }
      return true;
    }
    return false;
  }
  const Database& _database;
  ExceptionMatcher _matcher;
};
}  // namespace ista
