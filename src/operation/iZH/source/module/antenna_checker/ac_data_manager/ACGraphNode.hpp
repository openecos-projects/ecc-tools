// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ACShape.hpp"
#include "ZHHeader.hpp"

namespace izh {

class ACGraphNode
{
 public:
  int id = -1;
  int time = 0;
  bool is_conductor = false;
  bool is_routing = false;
  bool is_cut = false;
  bool is_side = false;
  double declared_area = 0.0;
  double gate_area = 0.0;
  double diff_area = 0.0;
  bool provides_diff = false;
  std::vector<ACShape> shapes;
};

}  // namespace izh
