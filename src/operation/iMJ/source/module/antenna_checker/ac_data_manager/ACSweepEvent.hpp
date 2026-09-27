// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "MJHeader.hpp"

namespace imj {

class ACSweepEvent
{
 public:
  int64_t x = 0;
  int y1 = 0;
  int y2 = 0;
  int delta = 0;
};

}  // namespace imj
