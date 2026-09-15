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
 * @file Utility.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Common timing utility implementation.
 */

#include "Utility.hh"

#include <cstddef>
#include <fstream>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#ifdef __linux__
#include <unistd.h>
#endif

namespace icts {

auto Utility::getElapsedSeconds(std::chrono::steady_clock::time_point start_time) -> double
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
}

auto Utility::currentRssMb() -> std::optional<double>
{
#ifdef __linux__
  std::ifstream statm("/proc/self/statm");
  std::size_t total_pages = 0U;
  std::size_t resident_pages = 0U;
  if (!(statm >> total_pages >> resident_pages)) {
    return std::nullopt;
  }
  const auto page_size_bytes = ::sysconf(_SC_PAGESIZE);
  if (page_size_bytes <= 0) {
    return std::nullopt;
  }
  constexpr double bytes_per_mebibyte = 1024.0 * 1024.0;
  return static_cast<double>(resident_pages) * static_cast<double>(page_size_bytes) / bytes_per_mebibyte;
#else
  return std::nullopt;
#endif
}

auto Utility::releaseMemory() -> MemoryReleaseStats
{
  MemoryReleaseStats stats;
#if defined(__linux__) && defined(__GLIBC__)
  const auto rss_before_mb = currentRssMb();
  (void) ::malloc_trim(0);
  const auto rss_after_mb = currentRssMb();
  if (rss_before_mb.has_value() && rss_after_mb.has_value()) {
    stats.supported = true;
    stats.rss_before_mb = *rss_before_mb;
    stats.rss_after_mb = *rss_after_mb;
  }
#endif
  return stats;
}

}  // namespace icts
