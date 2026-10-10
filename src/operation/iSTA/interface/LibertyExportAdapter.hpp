// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <vector>

#include "advance/TimingCapacitiveUnit.hpp"
#include "advance/TimingTimeUnit.hpp"
#include "TimingExportAdapter.hpp"

namespace ista {

class TimingLibrary;

class LibertyExportAdapter
{
 public:
  static std::vector<TimingLibCellSnapshot> exportCells(TimingLibrary& library, TimingTimeUnit time_unit,
                                                        TimingCapacitiveUnit cap_unit);
};

}  // namespace ista
