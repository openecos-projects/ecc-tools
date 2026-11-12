// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <vector>

#include "TimingData.hpp"

namespace ista {

class LibertyLibrary;
enum class TimingTimeUnit;
enum class TimingCapacitiveUnit;

class LibertyDataExporter
{
 public:
  static std::vector<LibertyCellData> exportCells(LibertyLibrary& library, TimingTimeUnit time_unit, TimingCapacitiveUnit cap_unit);
};

}  // namespace ista
