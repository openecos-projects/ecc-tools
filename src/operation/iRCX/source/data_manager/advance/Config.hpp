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

#include "Corner.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class Config
{
 public:
  Config() = default;
  ~Config() = default;
  /////////////////////////////////////////////
  // **********        RCX        ********** //
  std::string config_file_path;       // required
  int32_t thread_number;              // required
  std::string output_directory_path;  // optional
  bool report_geometry;               // optional
  std::string mapping_file_path;      // required
  std::vector<Corner> corner_list;    // required
  /////////////////////////////////////////////
  // **********        RCX        ********** //
  std::string temp_directory_path;  // building
  std::string log_file_path;        // building
  // **********    DataManager    ********** //
  std::string dm_temp_directory_path;  // building
  // ********** TopoBuilder  ********** //
  std::string tb_temp_directory_path;  // building
  // ********** EnvBuilder ******** //
  std::string eb_temp_directory_path;  // building
  // ********** VarProcessor ******** //
  std::string vp_temp_directory_path;  // building
  // ********** ResExtractor ****** //
  std::string re_temp_directory_path;  // building
  // ********** CapExtractor **** //
  std::string ce_temp_directory_path;  // building
  // **********     SPEFWriter    ********** //
  std::string sw_temp_directory_path;  // building
  /////////////////////////////////////////////
};

}  // namespace ircx
