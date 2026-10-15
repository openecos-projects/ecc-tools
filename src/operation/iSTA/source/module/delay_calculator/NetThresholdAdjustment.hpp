// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <array>
#include <string>

namespace ista {
class Database;

// Driver-library load slew -> receiver-library slew. Delay shifts are
// coefficients of the unconverted load slew, matching OpenSTA thresholdAdjust.
// Columns: rise delay, fall delay, rise slew scale, fall slew scale.
std::array<double, 4> netThresholdAdjustment(Database& database, const std::string& source, const std::string& sink);
}  // namespace ista
