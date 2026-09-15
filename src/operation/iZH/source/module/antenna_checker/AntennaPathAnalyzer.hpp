// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ACModel.hpp"
#include "ACViolation.hpp"
#include "IdbDesign.h"
#include "IdbNet.h"
#include "IdbPins.h"

namespace izh {

class AntennaPathAnalyzer
{
 public:
  static void checkNet(ACModel& ac_model, idb::IdbDesign* design, idb::IdbNet* net, std::vector<ACViolation>& out_violations);
  static void readPinAntennaInfo(ACModel& ac_model, idb::IdbPin* pin, bool instance_pin, double& gate_area, double& diff_area,
                                 bool& provides_diff);
};

}  // namespace izh
