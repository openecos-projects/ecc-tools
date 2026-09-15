// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ZHHeader.hpp"

namespace izh {

class ACAntennaRule
{
 public:
  std::string layer_name;
  int layer_order = -1;
  bool is_routing = true;
  double thickness_um = 0.0;
  double area_ratio = -1.0;
  double cum_area_ratio = -1.0;
  double area_factor = -1.0;
  bool area_factor_diffuse_only = false;
  double side_area_ratio = -1.0;
  double cum_side_area_ratio = -1.0;
  double side_area_factor = -1.0;
  bool side_area_factor_diffuse_only = false;
  double gate_plus_diff = -1.0;
  double area_minus_diff = -1.0;
  double diff_area_ratio = -1.0;
  double cum_diff_area_ratio = -1.0;
  double diff_side_area_ratio = -1.0;
  double cum_diff_side_area_ratio = -1.0;
  std::vector<std::pair<double, double>> diff_area_ratio_pwl;
  std::vector<std::pair<double, double>> cum_diff_area_ratio_pwl;
  std::vector<std::pair<double, double>> diff_side_area_ratio_pwl;
  std::vector<std::pair<double, double>> cum_diff_side_area_ratio_pwl;
  std::vector<std::pair<double, double>> area_diff_reduce_pwl;
  bool cum_routing_plus_cut = false;
};

}  // namespace izh
