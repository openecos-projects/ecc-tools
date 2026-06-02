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
 * @file CTSAPI.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief iCTS API
 */
#pragma once

#include <memory>
#include <string>

namespace ieda_feature {
struct CTSSummary;
}  // namespace ieda_feature

namespace icts {

#define CTS_API_INST (icts::CTSAPI::getInst())

class Flow;
struct CTSRuntime;

class CTSAPI
{
 public:
  static auto getInst() -> CTSAPI&
  {
    static CTSAPI inst;
    return inst;
  }

  // CTS CLI
  static auto runCTS() -> void;
  static auto report(const std::string& save_dir) -> void;

  // Flow API
  static auto resetAPI() -> void;
  static auto init(const std::string& config_file, const std::string& work_dir = "") -> void;

  // Feature API
  static auto outputSummary() -> ieda_feature::CTSSummary;
  CTSAPI(const CTSAPI& other) = delete;
  CTSAPI(CTSAPI&& other) = delete;
  auto operator=(const CTSAPI& other) -> CTSAPI& = delete;
  auto operator=(CTSAPI&& other) -> CTSAPI& = delete;

 private:
  CTSAPI();
  ~CTSAPI();

  auto runtime() -> CTSRuntime&;
  auto flow() -> Flow&;

  std::unique_ptr<CTSRuntime> _runtime;
  std::unique_ptr<Flow> _flow;
};

}  // namespace icts
