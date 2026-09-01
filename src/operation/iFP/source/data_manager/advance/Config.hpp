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
#pragma once

#include "DieMode.hpp"
#include "FPHeader.hpp"
#include "PGGlobalConnect.hpp"
#include "PGRail.hpp"
#include "PGLayerPair.hpp"
#include "PGStripe.hpp"

namespace ifp {

class Config
{
 public:
  Config() = default;
  ~Config() = default;
  /////////////////////////////////////////////
  // **********        FP         ********** //
  std::string temp_directory_path;                  // required
  int32_t thread_number;                            // optional
  double macro_placement_halo;                      // optional
  double macro_routing_halo;                        // optional
  DieMode die_mode;                                 // optional
  std::string die_site_name;                        // optional
  double die_aspect_ratio;                          // optional
  double die_utilization;                           // optional
  double die_width_micron;                          // optional
  double die_height_micron;                         // optional
  double die_margin_left_micron;                    // optional
  double die_margin_right_micron;                   // optional
  double die_margin_top_micron;                     // optional
  double die_margin_bottom_micron;                  // optional
  std::vector<std::string> io_pin_layer_name_list;  // optional
  std::vector<PGGlobalConnect> pg_connect_list;     // optional
  std::vector<PGRail> pg_rail_list;                 // optional
  std::vector<PGStripe> pg_stripe_list;             // optional
  std::vector<PGLayerPair> pg_layer_pair_list;      // optional
  std::string tapcell_name;                         // optional
  double tap_distance_micron;                       // optional
  std::string left_endcap_name;                     // optional
  std::string right_endcap_name;                    // optional
  std::vector<std::string> top_endcap_name_list;    // optional
  std::vector<std::string> bottom_endcap_name_list; // optional
  std::vector<std::string> top_boundary_tap_name_list;    // optional
  std::vector<std::string> bottom_boundary_tap_name_list; // optional
  double boundary_tap_rule_micron;                        // optional
  /////////////////////////////////////////////
  // **********        FP         ********** //
  std::string log_file_path;  // building
  // **********    DataManager    ********** //
  std::string dm_temp_directory_path;  // building
  // **********     DieBuilder    ********** //
  std::string db_temp_directory_path;  // building
  // **********     IOPlacer      ********** //
  std::string ip_temp_directory_path;  // building
  // **********    MacroPlacer    ********** //
  std::string mp_temp_directory_path;  // building
  // **********   PDNGenerator    ********** //
  std::string pg_temp_directory_path;  // building
  // **********     PhyPlacer     ********** //
  std::string pp_temp_directory_path;  // building
  /////////////////////////////////////////////
};

}  // namespace ifp
