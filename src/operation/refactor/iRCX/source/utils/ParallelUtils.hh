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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file ParallelUtils.hh
 * @brief Shared OpenMP thread-count helpers for iRCX tools.
 */
#pragma once

#include <algorithm>
#include <limits>
#include <omp.h>

#include "Types.hh"

namespace ircx {
namespace parallel {

inline auto cappedWorkItems(Size work_items) -> int
{
  constexpr auto max_int = static_cast<Size>(std::numeric_limits<int>::max());
  return work_items > max_int ? std::numeric_limits<int>::max() : static_cast<int>(work_items);
}

inline auto threadCount(Size work_items,
                        int requested_threads) -> int
{
  if (work_items == 0) {
    return 1;
  }

  int threads = requested_threads > 0 ? requested_threads : 1;
  threads = std::min(threads, omp_get_max_threads());
  return std::min(threads, cappedWorkItems(work_items));
}

inline auto threadCount(Size work_items) -> int
{
  return threadCount(work_items, omp_get_max_threads());
}

inline auto requestedThreadCount(Size work_items,
                                 int requested_threads) -> int
{
  if (work_items == 0) {
    return 1;
  }

  const int threads = requested_threads > 0 ? requested_threads : 1;
  return std::min(threads, cappedWorkItems(work_items));
}

}  // namespace parallel
}  // namespace ircx
