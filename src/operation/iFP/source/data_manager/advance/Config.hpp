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
#include "IOPlacerMode.hpp"
#include "PGGlobalConnect.hpp"
#include "PGLayerPair.hpp"
#include "PGRail.hpp"
#include "PGStripe.hpp"
#include "PlacementMode.hpp"
#include "PlacementOrientation.hpp"

namespace ifp {

struct PGRingConfig
{
  std::vector<std::string> nets;
  std::string horizontal_layer;
  std::string vertical_layer;
  double width_micron = 0.0;
  double spacing_micron = 0.0;
  double offset_micron = 0.0;
};

struct PGIOCoreConnectConfig
{
  // IO-core routes stay on the target stripe layer. Normal IO cells use
  // direct extensions and straight bridges; Manhattan forms are restricted
  // to cells whose outermost rail has no legal core-range access point.
  bool enabled = false;
  // Empty means the resolved core POWER/GROUND nets.
  std::vector<std::string> nets;
  // Empty means the layers declared by the core stripe definitions.
  std::vector<std::string> pin_layers;
  bool fail_on_unresolved = false;
  // Electrical requirement; configurable.
  int32_t min_contacts_per_port = 1;
  // Internal routing policy; not part of the JSON schema.
  bool select_outermost_pin = true;
  bool connect_all_legal_stripes = true;
  bool require_each_pin_layer = false;
  int32_t max_contacts_per_port = 0;
  // Limits are per PORT; zero means no artificial cap.
  int32_t max_routes_per_stripe = 0;
  bool allow_direct_extension = true;
  bool allow_bridge = true;
  // Zero uses the technology IO spacing.  A positive value is an additional
  // lower bound expressed in microns and is converted to DBU at use time.
  double bridge_min_spacing_micron = 0.0;
};

class Config
{
 public:
  Config() = default;
  ~Config() = default;
  /////////////////////////////////////////////
  // **********        FP         ********** //
  std::string temp_directory_path;                         // required
  int32_t thread_number;                                   // optional
  double macro_placement_halo;                             // optional
  double macro_routing_halo;                               // optional
  std::string input_macro_path;                            // optional; non-empty loads macro placement file
  DieMode die_mode;                                        // optional
  std::string die_site_name;                               // optional
  double die_aspect_ratio;                                 // optional
  double die_utilization;                                  // optional
  double die_width_micron;                                 // optional
  double die_height_micron;                                // optional
  double die_margin_left_micron;                           // optional
  double die_margin_right_micron;                          // optional
  double die_margin_top_micron;                            // optional
  double die_margin_bottom_micron;                         // optional
  IOPlacerMode io_placer_mode;                             // required
  PlacementMode io_pin_placement_mode;                     // required
  std::vector<std::string> io_pin_layer_name_list;         // optional
  std::string input_io_pin_path;                           // optional
  PlacementMode io_cell_mode = PlacementMode::kAuto;       // io_cell.mode
  std::string io_cell_file;                                // io_cell.file_path
  std::string io_cell_definition_file;                     // io_cell.definition_file_path
  double io_cell_power_spacing_micron = 1000.0;            // io_cell.power.target_group_spacing
  double io_cell_corner_clearance_micron = 260.0;          // io_cell.power.corner_clearance_micron
  bool io_cell_auto_resize = true;                         // io_cell.auto_resize.enable
  std::vector<PGGlobalConnect> pg_connect_list;            // optional
  std::string core_power_net;
  std::string core_ground_net;
  PGRingConfig pg_ring;
  PGIOCoreConnectConfig pg_io_core_connect;
  std::vector<std::string> pg_required_net_list;
  bool pg_connect_pads = false;
  bool pg_require_connected_pins = true;
  std::vector<std::string> pg_access_layer_list;
  std::vector<PGRail> pg_rail_list;                        // optional
  std::vector<PGStripe> pg_stripe_list;                    // optional
  std::vector<PGLayerPair> pg_layer_pair_list;             // optional
  std::string tapcell_name;                                // optional
  double tap_distance_micron;                              // optional
  std::string left_endcap_name;                            // optional
  std::string right_endcap_name;                           // optional
  std::vector<std::string> top_endcap_name_list;           // optional
  std::vector<std::string> bottom_endcap_name_list;        // optional
  std::vector<std::string> top_boundary_tap_name_list;     // optional
  std::vector<std::string> bottom_boundary_tap_name_list;  // optional
  double boundary_tap_rule_micron;                         // optional
  std::map<std::string, std::map<PlacementOrientation, PlacementOrientation>> phy_orientations;
  std::map<std::string, std::string> corner_cell_names;
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
