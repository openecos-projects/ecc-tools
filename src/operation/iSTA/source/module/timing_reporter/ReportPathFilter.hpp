// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

namespace ista {

// Value-only report predicate. It does not query or modify shared STA state.
struct ReportSlackFilter
{
  bool has_less = false;
  double less = 0.0;
  bool has_greater = false;
  double greater = 0.0;
  bool implicit_negative_only = false;

  bool accepts(double slack) const noexcept
  {
    if (has_less && !(slack < less))
      return false;
    if (has_greater && !(slack > greater))
      return false;
    if (implicit_negative_only && !(slack < 0.0))
      return false;
    return true;
  }
};

}  // namespace ista
