// SPDX-License-Identifier: MulanPSL-2.0
#include "EndpointQualification.hpp"

#include <cmath>

#include "advance/Database.hpp"
#include "advance/TimingArc.hpp"
#include "advance/TimingCheckArc.hpp"
#include "advance/TimingPoint.hpp"

namespace ista {
namespace {

TimingMaxQualification excluded(TimingMaxQualificationReason reason)
{
  return {{false, false}, {reason, reason}};
}

bool hasPathSpecificMaxConstraints(Database& database)
{
  auto& constraints = database.get_timing_constraint();
  for (const auto& exception : constraints.get_path_exception_list()) {
    if (exception.get_type() != TimingExceptionType::kMinDelay && exception.get_setup()) return true;
  }
  for (const auto& group : constraints.get_clock_group_list()) {
    if (!group.get_allow_paths()) return true;
  }
  return false;
}

bool hasCaptureClock(Database& database, const std::string& pin_name, TransType transition)
{
  const auto point = database.get_timing_point_map().find(pin_name);
  if (point == database.get_timing_point_map().end()) return false;
  const auto& clocks = database.get_timing_constraint().get_clock_map();
  for (const auto& [name, state] : point->second.get_clock_state_map()) {
    if (!clocks.contains(name)) continue;
    const auto analysis = state.arrival_map.find(AnalysisType::kMin);
    if (analysis == state.arrival_map.end()) continue;
    const auto arrival = analysis->second.find(transition);
    if (arrival != analysis->second.end() && std::isfinite(arrival->second)) return true;
  }
  return false;
}

}  // namespace

TimingMaxQualification qualifyMaxOutput(Database& database, const std::string& pin_name)
{
  auto result = excluded(TimingMaxQualificationReason::kNoOutputConstraint);
  auto& constraints = database.get_timing_constraint();
  const auto port = constraints.get_port_constraint_map().find(pin_name);
  if (port == constraints.get_port_constraint_map().end() || !port->second.get_has_output_delay_max()) return result;
  if (hasPathSpecificMaxConstraints(database)) return excluded(TimingMaxQualificationReason::kUnsupportedConstraint);
  for (std::size_t edge = 0; edge < 2; ++edge) {
    const auto transition = edge == 0 ? TransType::kRise : TransType::kFall;
    const auto delays = port->second.get_output_delays(AnalysisType::kMax, transition);
    for (const auto* delay : delays) {
      if (!constraints.get_clock_map().contains(delay->get_clock_name())) {
        result.reason[edge] = TimingMaxQualificationReason::kUnsupportedConstraint;
        continue;
      }
      result.valid[edge] = true;
      result.reason[edge] = TimingMaxQualificationReason::kValid;
    }
    if (port->second.get_output_delay_list().empty()) {
      result.valid[edge] = constraints.get_clock_map().contains(port->second.get_clock_name());
      result.reason[edge] = result.valid[edge] ? TimingMaxQualificationReason::kValid
                                             : TimingMaxQualificationReason::kUnsupportedConstraint;
    }
  }
  return result;
}

TimingMaxQualification qualifyMaxCheck(Database& database, TimingCheckArc& check, TimingArc& arc)
{
  if (check.get_check_type() != TimingCheckType::kSetup && check.get_check_type() != TimingCheckType::kRecovery) {
    return excluded(TimingMaxQualificationReason::kNotMaxCheck);
  }
  if (!hasCaptureClock(database, check.get_clock_port(), check.get_clock_trans_type())) {
    return excluded(TimingMaxQualificationReason::kNoCaptureClockState);
  }
  if (hasPathSpecificMaxConstraints(database)) return excluded(TimingMaxQualificationReason::kUnsupportedConstraint);
  auto result = excluded(TimingMaxQualificationReason::kInactiveCheck);
  for (std::size_t edge = 0; edge < 2; ++edge) {
    const auto transition = edge == 0 ? TransType::kRise : TransType::kFall;
    result.valid[edge] = arc.get_check_table_map().contains(transition);
    if (result.valid[edge]) result.reason[edge] = TimingMaxQualificationReason::kValid;
  }
  return result;
}

}  // namespace ista
