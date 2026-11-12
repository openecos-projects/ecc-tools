// SPDX-License-Identifier: MulanPSL-2.0
#include "sdc/ClockEdges.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Database.hpp"
namespace ista {
double launchClockEdge(Database& database, std::string_view start, std::string_view clock)
{
  const auto definition = database.sdc().get_clock_map().find(std::string(clock));
  if (definition == database.sdc().get_clock_map().end()) {
    return 0.0;
  }
  return database.network().launchClockTransition(start) == TransType::kFall ? definition->second.get_fall_edge() : definition->second.get_rise_edge();
}

double clockEdgeSeparation(double launch_period, double capture_period, double launch_edge, double capture_edge, AnalysisType type, bool allow_coincident_setup)
{
  // All relative edge positions repeat modulo the greatest common period.
  // Use a floating Euclidean algorithm to retain rational divide/multiply ratios.
  double interval = std::max(launch_period, capture_period);
  double remainder = std::min(launch_period, capture_period);
  const double tolerance = interval * 1e-10;
  if (!std::isfinite(interval) || remainder <= 0) {
    throw std::invalid_argument("invalid clock period");
  }
  for (int iteration = 0; remainder > tolerance && iteration < 128; ++iteration) {
    const double next = std::fmod(interval, remainder);
    interval = remainder;
    remainder = std::min(next, remainder - next);
  }
  double separation = std::fmod(capture_edge - launch_edge, interval);
  if (separation < 0) {
    separation += interval;
  }
  if (separation < tolerance || interval - separation < tolerance) {
    separation = 0;
  }
  if (type == AnalysisType::kMax) {
    return separation == 0 && !allow_coincident_setup ? interval : separation;
  }
  return separation == 0 ? 0 : separation - interval;
}
}  // namespace ista
