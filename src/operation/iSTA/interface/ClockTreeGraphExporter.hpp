// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include "ClockTreeData.hpp"

namespace ista {

class Database;

// Convert the committed clock-propagation state into an owned graph. The
// caller must keep the STA database alive until this function returns.
ClockTreeGraphData exportClockTreeGraph(const Database& database);

}  // namespace ista
