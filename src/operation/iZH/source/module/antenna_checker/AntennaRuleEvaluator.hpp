// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ACModel.hpp"
#include "ACThresholdPick.hpp"
#include "IdbDesign.h"

namespace izh {

class AntennaRuleEvaluator
{
 public:
  static void initLayers(ACModel& ac_model, idb::IdbDesign* design);
  static const ACAntennaRule* pickRule(const ACModel& ac_model, int layer_order, bool routing);
  static ACThresholdPick pickThreshold(double plain_ratio, double diff_ratio, const std::vector<std::pair<double, double>>& diff_pwl,
                                       double diff_area, bool diff_connected);
};

}  // namespace izh
