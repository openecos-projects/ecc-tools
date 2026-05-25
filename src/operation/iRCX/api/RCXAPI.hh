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

inline constexpr unsigned kDefaultThreadCount = 64U;
inline constexpr double kDefaultOperatingTemperature = 25.0F;

struct RCXInitOptions
{
  unsigned thread_number = kDefaultThreadCount;
  double operating_temperature = kDefaultOperatingTemperature;
};

class RCXAPI
{
 public:
  static auto getInst() -> RCXAPI&
  {
    static RCXAPI inst;
    return inst;
  }

  static auto init(const RCXInitOptions& options = {}) -> void;
  static auto init(const std::string& config_file) -> void;
  static auto resetAPI() -> void;

  static auto readCorner(const std::string& corner_name,
                         const char* itf_file,
                         const char* captab_file) -> bool;
  static auto readMapping(const char* mapping_file) -> bool;

  static auto adaptDB() -> bool;

  static auto checkShortOpen() -> bool;
  static auto buildTopology() -> bool;
  static auto buildEnvironment() -> bool;
  static auto buildProcessVariation() -> bool;
  static auto extractParasitics() -> bool;
  static auto run() -> void;
  static auto runRCX() -> void;

  static auto report(const std::string& output_dir) -> void;

  [[nodiscard]] static auto runSuccess() -> bool;
  [[nodiscard]] static auto reportSuccess() -> bool;

  RCXAPI(const RCXAPI& other) = delete;
  RCXAPI(RCXAPI&& other) = delete;
  auto operator=(const RCXAPI& other) -> RCXAPI& = delete;
  auto operator=(RCXAPI&& other) -> RCXAPI& = delete;

 private:
  RCXAPI() = default;
  ~RCXAPI() = default;
};

}  // namespace ircx
