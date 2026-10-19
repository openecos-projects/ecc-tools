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
  for (const auto& entry : point->second.get_clock_state_map()) {
    if (hasCaptureClockState(database, pin_name, entry.first, transition)) return true;
  }
  return false;
}

}  // namespace

bool hasCaptureClockState(Database& database, const std::string& pin_name, const std::string& clock_name, TransType transition)
{
  if (!database.get_timing_constraint().get_clock_map().contains(clock_name))
    return false;
  const auto point = database.get_timing_point_map().find(pin_name);
  if (point == database.get_timing_point_map().end())
    return false;
  const auto clock = point->second.get_clock_state_map().find(clock_name);
  if (clock == point->second.get_clock_state_map().end())
    return false;
  // A clock state reached only through the clock's own source path is not a capture
  // path: the data endpoint is launched by that clock, never captured by it, so it
  // must not qualify a setup/recovery check or a sparse capture row.
  if (clock->second.is_source_path_only)
    return false;
  const auto analysis = clock->second.arrival_map.find(AnalysisType::kMin);
  if (analysis == clock->second.arrival_map.end())
    return false;
  const auto arrival = analysis->second.find(transition);
  return arrival != analysis->second.end() && std::isfinite(arrival->second);
}

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
