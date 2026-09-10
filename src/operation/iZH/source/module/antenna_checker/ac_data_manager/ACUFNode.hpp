// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

namespace izh {

class ACUFNode
{
 public:
  int parent = -1;
  int rank = 0;
  double gate_area = 0.0;
  double diff_area = 0.0;
  bool diff_connected = false;
  double cum_area_num = 0.0;
  double cum_side_num = 0.0;
  double cum_cut_num = 0.0;
};

}  // namespace izh
