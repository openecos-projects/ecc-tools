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

#include "EMIRHeader.hpp"

namespace iemir {

class Config
{
 public:
  Config() = default;
  ~Config() = default;
  // getter

  // setter

  // function

 public:
  /////////////////////////////////////////////
  // **********       EMIR        ********** //
  std::string temp_directory_path;
  std::string instance_power_file_path;
  int32_t thread_number;
  /////////////////////////////////////////////
  // **********       EMIR        ********** //
  std::string log_file_path;
  // **********    DataManager    ********** //
  std::string dm_temp_directory_path;
  // **********   GraphBuilder    ********** //
  std::string gb_temp_directory_path;
  // **********    IRAnalyzer     ********** //
  std::string ia_temp_directory_path;
  // **********    EMAnalyzer     ********** //
  std::string ea_temp_directory_path;
  // **********   EMIRReporter    ********** //
  std::string er_temp_directory_path;
  /////////////////////////////////////////////
};

}  // namespace iemir
