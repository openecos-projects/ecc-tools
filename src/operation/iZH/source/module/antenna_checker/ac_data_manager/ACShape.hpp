// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "IdbGeometry.h"

namespace izh {

class ACShape
{
 public:
  int layer_order = -1;
  idb::IdbRect rect;
};

}  // namespace izh
