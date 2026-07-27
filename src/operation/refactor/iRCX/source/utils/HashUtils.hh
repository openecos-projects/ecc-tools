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
 * @file HashUtils.hh
 * @brief Hash helpers for pair keys and project geometry types.
 */
#pragma once

#include <functional>
#include <utility>

#include "Types.hh"

namespace ircx {
namespace hash {

inline auto combine(Size seed,
                    Size value) -> Size
{
  const Size kMagic =
      sizeof(Size) == 8
          ? static_cast<Size>(0x9e3779b97f4a7c15ull)
          : static_cast<Size>(0x9e3779b9ul);
  return seed ^ (value + kMagic + (seed << 6) + (seed >> 2));
}

template <class T, class H = std::hash<T>>
inline auto append(Size& seed,
                   const T& v) -> void
{
  seed = combine(seed, static_cast<Size>(H{}(v)));
}

// ============================================================
// Generic pair keys
// ============================================================

template <class T>
struct DirectedPairKey
{
  T first{};
  T second{};

  DirectedPairKey() = default;
  DirectedPairKey(T a,
                  T b) : first(std::move(a)), second(std::move(b)) {}

  auto operator==(const DirectedPairKey& o) const -> bool
  {
    return first == o.first && second == o.second;
  }
};

template <class T, class Less = std::less<T>>
struct UndirectedPairKey
{
  T first{};
  T second{};

  UndirectedPairKey() = default;

  UndirectedPairKey(T a, T b)
  {
    if (Less{}(b, a)) {
      first = std::move(b);
      second = std::move(a);
    } else {
      first = std::move(a);
      second = std::move(b);
    }
  }

  auto operator==(const UndirectedPairKey& o) const -> bool
  {
    return first == o.first && second == o.second;
  }
};

template <class PairKey, class ElemHash = std::hash<decltype(PairKey{}.first)>>
struct PairKeyHasher
{
  auto operator()(const PairKey& k) const -> Size
  {
    Size seed = 0;
    append<decltype(k.first), ElemHash>(seed, k.first);
    append<decltype(k.second), ElemHash>(seed, k.second);
    return seed;
  }
};

// ============================================================
// Project geometry hashers
// ============================================================

struct GtlPointHasher
{
  auto operator()(const GtlPointI& p) const -> Size
  {
    Size seed = 0;
    append(seed, gtl::x(p));
    append(seed, gtl::y(p));
    return seed;
  }
};

struct GtlRectHasher
{
  auto operator()(const GtlRectI& r) const -> Size
  {
    Size seed = 0;
    append(seed, gtl::xl(r));
    append(seed, gtl::yl(r));
    append(seed, gtl::xh(r));
    append(seed, gtl::yh(r));
    return seed;
  }
};

struct LayerPointHasher
{
  auto operator()(const std::pair<Size, GtlPointI>& p) const -> Size
  {
    Size seed = 0;
    append(seed, p.first);
    append(seed, gtl::x(p.second));
    append(seed, gtl::y(p.second));
    return seed;
  }
};

struct LayerRectHasher
{
  auto operator()(const std::pair<Size, GtlRectI>& p) const -> Size
  {
    Size seed = 0;
    append(seed, p.first);
    append(seed, gtl::xl(p.second));
    append(seed, gtl::yl(p.second));
    append(seed, gtl::xh(p.second));
    append(seed, gtl::yh(p.second));
    return seed;
  }
};

}  // namespace hash
}  // namespace ircx
