// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

namespace izh {

class ACThresholdPick
{
 public:
  bool available = false;
  double threshold = 0.0;
  bool is_diff = false;
};

}  // namespace izh
