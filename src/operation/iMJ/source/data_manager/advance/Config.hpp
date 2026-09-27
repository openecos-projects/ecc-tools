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

#include "MJHeader.hpp"

namespace imj {

class Config
{
 public:
  Config() = default;
  ~Config() = default;

  /////////////////////////////////////////////
  // **********        MJ         ********** //
  std::string temp_directory_path;  // required
  /////////////////////////////////////////////
  // **********        MJ         ********** //
  std::string log_file_path;  // building
  // **********    DataManager    ********** //
  std::string dm_temp_directory_path;  // building
  // **********  AntennaChecker   ********** //
  std::string ac_temp_directory_path;  // building
  // **********   DefFlattener    ********** //
  std::string df_temp_directory_path;  // building
  // **********  FillerInserter   ********** //
  std::string fi_temp_directory_path;  // building
  // **********  MetalInserter    ********** //
  std::string mi_temp_directory_path;  // building
  /////////////////////////////////////////////
};

}  // namespace imj
