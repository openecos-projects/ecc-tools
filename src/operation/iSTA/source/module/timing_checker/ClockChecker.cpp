// SPDX-License-Identifier: MulanPSL-2.0
#include "ClockChecker.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "ClockReconvergenceCalculator.hpp"
#include "graph/TimingStateQuery.hpp"

namespace ista {
namespace {
TransType opposite(TransType edge)
{
  return edge == TransType::kRise ? TransType::kFall : TransType::kRise;
}

bool enabled(const Database& db, const std::string& instance, const LogicExpression& condition)
{
  if (condition.get_is_empty())
    return true;
  std::map<std::string, bool> values;
  for (const auto& term : condition.get_term_list()) {
    if (term.get_operation_type() != LogicOperationType::kPort)
      continue;
    const auto value = db.graph().simValues().value(instance + ":" + term.get_port_name());
    if (value && (*value == TimingCaseValue::kZero || *value == TimingCaseValue::kOne))
      values[term.get_port_name()] = *value == TimingCaseValue::kOne;
  }
  return condition.evaluate_constant(values) != std::optional<bool>(false);
}
}  // namespace

std::vector<ClockCheck> ClockChecker::check()
{
  std::map<std::tuple<std::string, std::string, TimingCheckType, TransType>, ClockCheck> worst;
  const auto& factors = _database.sdc().timingDerates();
  ClockReconvergenceCalculator reconvergence(_database);
  for (const auto& [instance_name, instance] : _database.network().instances()) {
    auto cell = _database.liberty().get_cell_map().find(instance.get_cell_name());
    if (cell == _database.liberty().get_cell_map().end())
      continue;
    const DerateContext context{instance_name, cell->second.get_library_name() + "/" + cell->first, {}};
    for (auto& arc : cell->second.get_sdf_check_arc_list()) {
      const auto type = arc.get_check_type();
      if (type != TimingCheckType::kWidth && type != TimingCheckType::kPeriod)
        continue;
      const auto kind = type == TimingCheckType::kPeriod ? DerateDelay::kPeriod : DerateDelay::kWidth;
      const std::string pin = instance_name + ":" + arc.get_data_port();
      const auto point = _database.graph().points().find(pin);
      if (point == _database.graph().points().end())
        continue;
      const auto logic = _database.graph().simValues().value(pin);
      if (logic && (*logic == TimingCaseValue::kZero || *logic == TimingCaseValue::kOne))
        continue;
      for (const auto& [clock_name, state] : point->second.get_clock_state_map()) {
        const auto definition = _database.sdc().get_clock_map().find(clock_name);
        if (state.is_source_path_only || definition == _database.sdc().get_clock_map().end())
          continue;
        const auto& clock = definition->second;
        if (!(clock.get_period() > 0.))
          continue;
        for (auto edge : {TransType::kRise, TransType::kFall}) {
          const auto closing_edge = type == TimingCheckType::kPeriod ? edge : opposite(edge);
          for (auto start : clockSourceTransitions(_database, pin, clock_name, AnalysisType::kMax, edge))
            for (auto end : clockSourceTransitions(_database, pin, clock_name, AnalysisType::kMin, closing_edge)) {
              const auto& leading_wave = *state.waveform(start);
              const auto& trailing_wave = *state.waveform(end);
              const auto leading = leading_wave.arrival(AnalysisType::kMax, edge), trailing = trailing_wave.arrival(AnalysisType::kMin, closing_edge);
              if (!leading || !trailing)
                continue;
              std::optional<double> required;
              // A single-pin constraint uses the checked edge's slew on both axes.
              // Evaluate both extrema, including non-monotonic table behavior.
              for (auto& table_arc : arc.get_timing_arc_list()) {
                if (!enabled(_database, instance_name, table_arc.get_when_expression()))
                  continue;
                auto table = table_arc.get_check_table_map().find(edge);
                if (table == table_arc.get_check_table_map().end())
                  continue;
                for (auto mode : {AnalysisType::kMin, AnalysisType::kMax}) {
                  const double scale = table_arc.get_time_unit_scale();
                  const double slew = leading_wave.slew(mode, edge).value_or(0.) * scale;
                  const double nominal = table->second.findValue(slew, slew) / scale;
                  const double value = factors.apply(nominal, AnalysisType::kNone, false, edge, kind, context);
                  if (!required || value > *required)
                    required = value;
                }
              }
              if (arc.get_timing_arc_list().empty())
                required = factors.apply(arc.get_check_time(), AnalysisType::kNone, false, edge, kind, context);
              if (!required)
                continue;
              double separation = clock.get_period();
              for (size_t index = start == TransType::kFall ? 1 : 0; index < clock.edgeCount(); index += 2) {
                const size_t step = start == end ? 2 : 1;
                const size_t next = index + step;
                const double trailing_time = clock.edge(next % clock.edgeCount()).time + (next >= clock.edgeCount() ? clock.get_period() : 0);
                separation = std::min(separation, trailing_time - clock.edge(index).time);
              }
              std::string common_pin;
              auto latency = [&](AnalysisType mode, TransType local, TransType source) {
                const auto* wave = state.waveform(source);
                return wave ? wave->arrival(mode, local).value_or(0.) : 0.;
              };
              const bool propagated = clockPinValues(_database, pin, clock_name, AnalysisType::kMax, edge, AnalysisType::kNone, start).propagated
                                      || clockPinValues(_database, pin, clock_name, AnalysisType::kMin, closing_edge, AnalysisType::kNone, end).propagated;
              const double correction
                  = propagated ? reconvergence.correction(pin, edge, clock_name, pin, closing_edge, clock_name, AnalysisType::kMax, common_pin, start, end)
                               : std::min(std::abs(latency(AnalysisType::kMax, edge, start) - latency(AnalysisType::kMin, edge, start)),
                                          std::abs(latency(AnalysisType::kMax, closing_edge, end) - latency(AnalysisType::kMin, closing_edge, end)));
              ClockCheck result{pin,
                                clock_name,
                                type,
                                edge,
                                *required,
                                separation + latency(AnalysisType::kMin, closing_edge, end) - latency(AnalysisType::kMax, edge, start) + correction,
                                factors.factor(AnalysisType::kNone, false, edge, kind, context),
                                correction};
              const auto key = std::tuple{pin, clock_name, type, edge};
              const auto previous = worst.find(key);
              if (previous == worst.end() || result.slack() < previous->second.slack())
                worst[key] = std::move(result);
            }
        }
      }
    }
  }
  std::vector<ClockCheck> results;
  for (auto& [key, check] : worst)
    results.push_back(std::move(check));
  return results;
}
}  // namespace ista
