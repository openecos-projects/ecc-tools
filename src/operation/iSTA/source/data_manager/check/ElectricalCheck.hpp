// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <cmath>
#include <string>

#include "common/AnalysisType.hpp"
#include "sdc/ElectricalLimits.hpp"

namespace ista {
// One worst evaluation per physical pin and rule, across modes, edges and
// clock domains. The winning context remains available in reports.
struct ElectricalCheck
{
  ElectricalRule rule;
  std::string pin;
  std::string net;
  std::string clock;
  AnalysisType analysis;
  TransType edge;
  bool clock_path;
  double actual;
  double limit;
  double slack() const { return limit - actual; }
  // Constraint classification uses the original quantities, not report rounding
  // or the tolerance used by timing algorithms (which has different units).
  bool violated() const { return std::isfinite(actual) && std::isfinite(limit) && actual > limit; }
};
}  // namespace ista
