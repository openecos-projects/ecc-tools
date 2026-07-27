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
 * @file itfrCallBacks.hpp
 * @brief Legacy ITF parser data structure implementation detail.
 */
#pragma once

#include "itfrReader.hpp"

namespace itf
{
  
class itfrCallBacks {
 public:
  // function
  static void reset();
  static void clear();

  // members
  itfrStringCbFnType technology_cb = nullptr;
  itfrStringCbFnType process_foundry_cb = nullptr;
  itfrDoubleCbFnType process_node_cb = nullptr;
  itfrStringCbFnType process_type_cb = nullptr;
  itfrDoubleCbFnType process_version_cb = nullptr;
  itfrStringCbFnType process_corner_cb = nullptr;
  itfrStringCbFnType reference_direction_cb = nullptr;
  itfrDoubleCbFnType global_temperature_cb = nullptr;
  itfrDoubleCbFnType background_er_cb = nullptr;
  itfrDoubleCbFnType half_node_scale_factor_cb = nullptr;
  itfrIntegerCbFnType use_si_density_cb = nullptr;
  itfrDoubleCbFnType drop_factor_lateral_spacing_cb = nullptr;
  itfrConductorCbFnType conductor_cb = nullptr;
  itfrDielectricCbFnType dielectric_cb = nullptr;
  itfrViaCbFnType via_cb = nullptr;
};

extern itfrCallBacks* itfCallbacks;

} // namespace itf
