// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ista {

class Database;
class DelayCalculator;

// Seed the PI/CK electrical boundary, then populate path identities and
// exception progress without full-graph numeric propagation. The resulting states are
// consumed only by the model exporter; TimingAnalyzer::analyze() clears them
// before a complete STA run.
class StructuralTimingStateBuilder
{
 public:
  static void build(Database& database, DelayCalculator& delay);
};

}  // namespace ista
