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

#include <string>

namespace ircx {

#define RCX_API_INST (ircx::RCXAPI::getInst())

class RCXAPI
{
 public:
  static auto getInst() -> RCXAPI&
  {
    static RCXAPI inst;
    return inst;
  }

  static auto init(const std::string& config_file) -> bool;
  static auto run() -> bool;
  static auto report() -> bool;

  // init
  static auto readCorner(const std::string& corner_name,
                         const char* itf_file,
                         const char* captab_file) -> bool;
  static auto readMapping(const char* mapping_file) -> bool;

  // run
  static auto adaptDB() -> bool;
  static auto extract() -> bool;

  RCXAPI(const RCXAPI& other) = delete;
  RCXAPI(RCXAPI&& other) = delete;
  auto operator=(const RCXAPI& other) -> RCXAPI& = delete;
  auto operator=(RCXAPI&& other) -> RCXAPI& = delete;

 private:
  RCXAPI() = default;
  ~RCXAPI() = default;
};

}  // namespace ircx
