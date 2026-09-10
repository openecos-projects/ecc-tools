// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "IdbGeometry.h"

namespace izh {

class ACPinShape
{
 public:
  int order = -1;
  bool routing = false;
  bool cut = false;
  idb::IdbRect rect;
};

}  // namespace izh
