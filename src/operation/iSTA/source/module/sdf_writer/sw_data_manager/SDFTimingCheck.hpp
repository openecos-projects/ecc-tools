// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// ***************************************************************************************
#pragma once

namespace ista {

inline bool adjustSDFHoldTimingCheckDelay(double minimum_setup_delay, double& minimum_hold_delay, double& maximum_hold_delay)
{
  const double minimum_valid_hold_delay = -minimum_setup_delay;
  bool adjusted = false;

  if (minimum_hold_delay < minimum_valid_hold_delay) {
    minimum_hold_delay = minimum_valid_hold_delay;
    adjusted = true;
  }
  if (maximum_hold_delay < minimum_valid_hold_delay) {
    maximum_hold_delay = minimum_valid_hold_delay;
    adjusted = true;
  }
  return adjusted;
}

}  // namespace ista
