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
 * @file Monitor.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-29
 * @brief iRT-style runtime resource monitor for iCTS.
 */

#pragma once

#include <string>

namespace icts {

class Monitor final
{
 public:
  Monitor();
  ~Monitor() = default;

  auto getStatsInfo() -> std::string;
  auto getElapsedTime() const -> std::string;
  auto getCPUTime() const -> std::string;
  auto getUsageMemory() const -> std::string;

 private:
  void updateStats();
  static auto getCurrentElapsedTime() -> double;
  static auto getCurrentCPUTime() -> double;
  static auto getCurrentUsageMemory() -> double;
  static auto formatSeconds(double seconds) -> std::string;
  static auto formatMemory(double megabytes) -> std::string;

  double _initial_elapsed_time = 0.0;
  double _initial_cpu_time = 0.0;
  double _initial_usage_memory = 0.0;
};

}  // namespace icts
