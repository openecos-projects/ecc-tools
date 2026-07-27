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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file itfrData.hpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#pragma once 

#include <stdio.h>
#include <string>

#include "itfiConductor.hpp"
#include "itfiDielectric.hpp"
#include "itfiVia.hpp"

namespace itf
{

// refers to StarRC User Guide. Version F-2011.06, June 2011     # 2022-12-06 # 
// Refers to StarRC User Guide. Version U-2022.12, December 2022.
// 2024-01-18: add some feature in 14 nm.
class itfrData {
 public:
  // constructor
  itfrData();
  ~itfrData();

  // function
  static void reset();
  static void clear();
  void initRead();

  // members
  std::string itf_file;
  FILE* log_file;

  std::string process_name;
  std::string process_foundry;
  double process_node;
  std::string process_type;
  double process_version;
  std::string process_corner;
  std::string reference_direction;
  float global_temperature;
  float background_er;  // Relative permittivity
  float half_node_scale_factor;
  float drop_factor_lateral_spacing;  // Units: microns

  itfiDielectric dielectric;
  itfiConductor conductor;
  itfiVia via;

  unsigned use_si_density : 1;
  unsigned has_open_log_file : 1;
  unsigned has_global_temperature : 1;
  unsigned has_background_er : 1;
  unsigned has_half_node_scale_factor : 1;
  unsigned has_use_si_density : 1;
  unsigned has_drop_factor_lateral_spacing : 1;
};

extern itfrData* itfData;

} // namespace itf
