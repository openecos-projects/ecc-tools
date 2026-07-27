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

#include "LVSHeader.hpp"

namespace ilvs {

class Config
{
 public:
  Config() = default;
  ~Config() = default;
  /////////////////////////////////////////////
  // **********       LVS        ********** //
  std::string temp_directory_path;  // required
  int32_t thread_number;            // optional
  /////////////////////////////////////////////
  // **********       LVS        ********** //
  std::string log_file_path;  // building
  // **********   DataManager    ********** //
  std::string dm_temp_directory_path;  // building
  // ******** NetlistExtractor   ********** //
  std::string ne_temp_directory_path;  // building
  // *********  SnapshotIO       ********** //
  std::string sio_temp_directory_path;  // building
  // **********   LVSChecker     ********** //
  std::string lc_temp_directory_path;  // building
  // **********   LVSReporter    ********** //
  std::string lr_temp_directory_path;  // building
  /////////////////////////////////////////////
};

}  // namespace ilvs
