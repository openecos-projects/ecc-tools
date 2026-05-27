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

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

namespace ircx {

template <typename T>
struct IntervalRange {
  T a0{};
  T a1{};
};

template <typename T>
inline void normalizeInterval(T& a0, T& a1)
{
  if (a0 > a1) {
    std::swap(a0, a1);
  }
}

template <typename T>
inline bool isValidInterval(T a0, T a1)
{
  return a0 < a1;
}

template <typename T>
inline bool intervalOverlaps(T a0, T a1, T b0, T b1)
{
  normalizeInterval(a0, a1);
  normalizeInterval(b0, b1);
  return std::max(a0, b0) < std::min(a1, b1);
}

template <typename T>
inline T intervalOverlapBegin(T a0, T a1, T b0, T b1)
{
  normalizeInterval(a0, a1);
  normalizeInterval(b0, b1);
  return std::max(a0, b0);
}

template <typename T>
inline T intervalOverlapEnd(T a0, T a1, T b0, T b1)
{
  normalizeInterval(a0, a1);
  normalizeInterval(b0, b1);
  return std::min(a1, b1);
}

template <typename T>
inline IntervalRange<T> intervalIntersection(T a0, T a1, T b0, T b1)
{
  return {intervalOverlapBegin(a0, a1, b0, b1),
          intervalOverlapEnd(a0, a1, b0, b1)};
}

template <typename T>
inline T midpoint(T coord0, T coord1)
{
  return coord0 + (coord1 - coord0) / 2;
}

template <typename Interval>
inline std::vector<Interval> subtractInterval(
    const std::vector<Interval>& intervals,
    std::remove_cvref_t<decltype(std::declval<Interval>().a0)> cut_a0,
    std::remove_cvref_t<decltype(std::declval<Interval>().a1)> cut_a1)
{
  using Coord = std::remove_cvref_t<decltype(std::declval<Interval>().a0)>;

  std::vector<Interval> next;
  normalizeInterval(cut_a0, cut_a1);

  for (const auto& interval : intervals) {
    if (!intervalOverlaps(interval.a0, interval.a1, cut_a0, cut_a1)) {
      next.push_back(interval);
      continue;
    }

    const Interval left{interval.a0, static_cast<Coord>(std::min(interval.a1, cut_a0))};
    if (isValidInterval(left.a0, left.a1)) {
      next.push_back(left);
    }

    const Interval right{static_cast<Coord>(std::max(interval.a0, cut_a1)), interval.a1};
    if (isValidInterval(right.a0, right.a1)) {
      next.push_back(right);
    }
  }

  return next;
}

template <typename Interval, typename Mergeable>
inline std::vector<Interval> clipIntervals(
    const std::vector<Interval>& intervals,
    std::remove_cvref_t<decltype(std::declval<Interval>().a0)> clip_a0,
    std::remove_cvref_t<decltype(std::declval<Interval>().a1)> clip_a1,
    Mergeable mergeable)
{
  std::vector<Interval> clipped;
  if (!isValidInterval(clip_a0, clip_a1)) {
    return clipped;
  }

  for (const auto& interval : intervals) {
    const auto a0 = std::max(clip_a0, interval.a0);
    const auto a1 = std::min(clip_a1, interval.a1);
    if (!isValidInterval(a0, a1)) {
      continue;
    }

    if (!clipped.empty() &&
        clipped.back().a1 == a0 &&
        mergeable(clipped.back(), interval)) {
      clipped.back().a1 = a1;
      continue;
    }

    Interval out = interval;
    out.a0 = a0;
    out.a1 = a1;
    clipped.push_back(std::move(out));
  }

  return clipped;
}

}  // namespace ircx
