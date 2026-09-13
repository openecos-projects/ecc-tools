// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ACViolationType.hpp"
#include "ZHHeader.hpp"

namespace izh {

class ACViolation
{
 public:
  std::string net_name;
  std::string layer_name;
  ACViolationType type = ACViolationType::kNone;
  double ratio = 0.0;
  double threshold = 0.0;
  double lx = 0.0;
  double ly = 0.0;
  double hx = 0.0;
  double hy = 0.0;
  std::string pin_name;
  std::string inst_name;
  int layer_order = -1;
  double gate_area = 0.0;
  double diff_area = 0.0;
  double metal_area = 0.0;
  double cut_area = 0.0;
};

}  // namespace izh
