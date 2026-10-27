// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ista {
class Database;
struct TimingData;
struct SparseTimingSnapshot;

// Extend electrical slew propagation without creating arrival paths or checks.
void appendSparseElectricalEdges(const TimingData& snapshot, Database& database, SparseTimingSnapshot& view);
}  // namespace ista
