// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

namespace izh {

class ACSegmentTreeNode
{
 public:
  int cover = 0;
  long double length = 0.0L;
  int interval_num = 0;
  bool left_covered = false;
  bool right_covered = false;
};

}  // namespace izh
