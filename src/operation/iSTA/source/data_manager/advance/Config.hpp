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

#include "STAHeader.hpp"

namespace ista {

class Config
{
 public:
  Config() = default;
  ~Config() = default;
  /////////////////////////////////////////////
  // **********        STA        ********** //
  std::string temp_directory_path;          // required
  int32_t thread_number;                    // optional
  int32_t path_report_number = 1;           // optional, report_timing -max_paths
  int32_t endpoint_path_report_number = 1;  // optional, report_timing -nworst
  int32_t output_timing_reports = 1;
  int32_t output_timing_features = 1;
  int32_t timing_path_limit = 20;
  std::string timing_corner;
  bool is_path_report_number_specified = false;
  bool has_timing_report_slack_lesser_than = false;
  bool has_timing_report_slack_greater_than = false;
  double timing_report_slack_lesser_than = 0.0;
  double timing_report_slack_greater_than = 0.0;
  /////////////////////////////////////////////
  // **********        STA        ********** //
  std::string log_file_path;  // building
  // **********    DataManager    ********** //
  std::string dm_temp_directory_path;  // building
  // **********   GraphBuilder    ********** //
  std::string gb_temp_directory_path;  // building
  // ********* DelayCalculator   ********* //
  std::string dc_temp_directory_path;  // building
  // ******** ClockPropagator    ********* //
  std::string cp_temp_directory_path;  // building
  // ********* TimingPropagator   ********* //
  std::string tp_temp_directory_path;  // building
  // ********* PowerPropagator    ********* //
  std::string pp_temp_directory_path;  // building
  // ********** TimingAnalyzer   ********* //
  std::string ta_temp_directory_path;  // building
  // ********** PowerAnalyzer    ********* //
  std::string pa_temp_directory_path;  // building
  // ******* TimingCharacterizer ******* //
  std::string tc_temp_directory_path;  // building
  // **********  TimingReporter   ********** //
  std::string tr_temp_directory_path;  // building
  // **********  PowerReporter    ********** //
  std::string pr_temp_directory_path;  // building
  // ************  SDFWriter  ************* //
  std::string sw_temp_directory_path;  // building
  /////////////////////////////////////////////
};

}  // namespace ista
