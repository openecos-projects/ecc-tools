// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include <vector>

#include "ACModel.hpp"
#include "ACRunSummary.hpp"
#include "ACThresholdPick.hpp"
#include "ACViolation.hpp"
#include "AntennaGraph.hpp"

namespace imj {

// EccDB adapter-only graph kernel. The legacy iDB checker retains its
// established numerical implementation as an independent differential
// reference.
class AntennaKernel
{
 public:
  static void initializeRuleIndex(ACModel& model);
  static void checkNet(ACModel& model, const AntennaGraphInput& input, std::vector<ACViolation>& violations);
  static AntennaRunSummary summary(const ACModel& model);
  static void unionArea(const std::vector<AntennaRect>& rects, int micron_dbu, double& area_um);
  static void unionAreaPerimeter(const std::vector<AntennaRect>& rects, int micron_dbu, double& area_um, double& perimeter_um);
  static ACThresholdPick pickThreshold(double plain_ratio, double diff_ratio,
                                       const std::vector<std::pair<double, double>>& diff_pwl, double diff_area,
                                       bool diff_connected);
};

}  // namespace imj
