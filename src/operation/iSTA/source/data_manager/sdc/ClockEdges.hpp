// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <string_view>

#include "common/AnalysisType.hpp"
namespace ista {
class Database;
double launchClockEdge(Database& database, std::string_view start, std::string_view clock);
double clockEdgeSeparation(double launch_period, double capture_period, double launch_edge, double capture_edge, AnalysisType type,
                           bool allow_coincident_setup = false);
}  // namespace ista
