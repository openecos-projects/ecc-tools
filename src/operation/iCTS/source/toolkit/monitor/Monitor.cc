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
 * @file Monitor.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-29
 * @brief iRT-style runtime resource monitor implementation for iCTS.
 */

#include "Monitor.hh"

#include <sys/resource.h>
#include <sys/time.h>

#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>

#include "Logger.hh"

namespace icts {

Monitor::Monitor()
{
  updateStats();
}

auto Monitor::getStatsInfo() -> std::string
{
  const auto stats_info = std::string{" (elapsed = "} + getElapsedTime() + ", cpu = " + getCPUTime() + ", mem = " + getUsageMemory() + ") ";
  updateStats();
  return stats_info;
}

auto Monitor::getElapsedTime() const -> std::string
{
  return formatSeconds(getCurrentElapsedTime() - _initial_elapsed_time);
}

auto Monitor::getCPUTime() const -> std::string
{
  return formatSeconds(getCurrentCPUTime() - _initial_cpu_time);
}

auto Monitor::getUsageMemory() const -> std::string
{
  return formatMemory(getCurrentUsageMemory() - _initial_usage_memory);
}

void Monitor::updateStats()
{
  _initial_elapsed_time = getCurrentElapsedTime();
  _initial_cpu_time = getCurrentCPUTime();
  _initial_usage_memory = getCurrentUsageMemory();
}

auto Monitor::getCurrentElapsedTime() -> double
{
  timeval value{};
  if (gettimeofday(&value, nullptr) != 0) {
    CTSLOG.error(Loc::current(), "Unable to sample CTS elapsed time.");
  }
  return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) / 1000000.0;
}

auto Monitor::getCurrentCPUTime() -> double
{
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    CTSLOG.error(Loc::current(), "Unable to sample CTS process CPU time.");
  }
  return static_cast<double>(usage.ru_utime.tv_sec) + static_cast<double>(usage.ru_utime.tv_usec) / 1000000.0 + static_cast<double>(usage.ru_stime.tv_sec)
         + static_cast<double>(usage.ru_stime.tv_usec) / 1000000.0;
}

auto Monitor::getCurrentUsageMemory() -> double
{
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    CTSLOG.error(Loc::current(), "Unable to sample CTS peak memory.");
  }
  return static_cast<double>(std::invoke(&rusage::ru_maxrss, usage)) / 1000.0;
}

auto Monitor::formatSeconds(double seconds) -> std::string
{
  const auto rounded_seconds = std::llround(seconds);
  const auto hours = rounded_seconds / 3600;
  const auto minutes = (rounded_seconds % 3600) / 60;
  const auto remaining_seconds = rounded_seconds % 60;

  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(2) << hours << ":" << std::setw(2) << minutes << ":" << std::setw(2) << remaining_seconds;
  return stream.str();
}

auto Monitor::formatMemory(double megabytes) -> std::string
{
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(2) << megabytes << "MB";
  return stream.str();
}

}  // namespace icts
