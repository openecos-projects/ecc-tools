// SPDX-License-Identifier: MulanPSL-2.0
#include <limits>
#include <stdexcept>

#include "ReportPathFilter.hpp"

namespace {
using ista::ReportSlackFilter;

void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void checkStrictBounds()
{
  const ReportSlackFilter bounded{true, 1.0, true, -1.0, false};
  require(bounded.accepts(0.0), "interior slack rejected");
  require(!bounded.accepts(1.0), "lesser-than bound became inclusive");
  require(!bounded.accepts(-1.0), "greater-than bound became inclusive");
  require(!bounded.accepts(2.0) && !bounded.accepts(-2.0), "out-of-range slack accepted");
  const ReportSlackFilter contradictory{true, -1.0, true, 1.0, false};
  require(!contradictory.accepts(0.0), "contradictory bounds accepted slack");
}

void checkImplicitNegativeOnly()
{
  const ReportSlackFilter implicit{false, 0.0, false, 0.0, true};
  require(implicit.accepts(-0.5), "implicit negative slack rejected");
  require(!implicit.accepts(0.0) && !implicit.accepts(-0.0), "zero slack passed implicit negative filter");
  require(!implicit.accepts(0.5), "positive slack passed implicit negative filter");
  const ReportSlackFilter explicit_upper{true, 1.0, false, 0.0, false};
  require(explicit_upper.accepts(0.5), "explicit upper bound retained implicit negative filter");
  const ReportSlackFilter explicit_lower{false, 0.0, true, -1.0, false};
  require(explicit_lower.accepts(0.5), "explicit lower bound retained implicit negative filter");
}

void checkNonFiniteSlack()
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const ReportSlackFilter unbounded{};
  require(unbounded.accepts(nan) && unbounded.accepts(inf) && unbounded.accepts(-inf), "unbounded filter changed legacy non-finite acceptance");
  const ReportSlackFilter upper{true, 1.0, false, 0.0, false};
  require(!upper.accepts(nan) && !upper.accepts(inf) && upper.accepts(-inf), "upper-bound non-finite behavior changed");
  const ReportSlackFilter lower{false, 0.0, true, -1.0, false};
  require(!lower.accepts(nan) && lower.accepts(inf) && !lower.accepts(-inf), "lower-bound non-finite behavior changed");
  const ReportSlackFilter implicit{false, 0.0, false, 0.0, true};
  require(!implicit.accepts(nan) && !implicit.accepts(inf) && implicit.accepts(-inf), "implicit-filter non-finite behavior changed");
}
}  // namespace

int main()
{
  checkStrictBounds();
  checkImplicitNegativeOnly();
  checkNonFiniteSlack();
}
