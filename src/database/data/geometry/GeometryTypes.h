#pragma once

#include <algorithm>
#include <cstdint>

namespace ecc::geometry {

using ShapeId = uint64_t;
using ShapeVersion = uint32_t;
using OwnerId = uint64_t;
using LayerId = uint16_t;
using NameId = uint32_t;
using RecordIndex = uint64_t;

struct Point32
{
  int32_t x = 0;
  int32_t y = 0;
};

struct Rect32
{
  int32_t lx = 0;
  int32_t ly = 0;
  int32_t hx = 0;
  int32_t hy = 0;
};

enum class ShapeKind : uint8_t
{
  kPoint = 1,
  kLine = 2,
  kRect = 3,
};

enum class ShapeState : uint8_t
{
  kAlive = 1,
  kDeleted = 2,
};

inline Rect32 normalize(Rect32 rect)
{
  if (rect.lx > rect.hx) {
    std::swap(rect.lx, rect.hx);
  }
  if (rect.ly > rect.hy) {
    std::swap(rect.ly, rect.hy);
  }
  return rect;
}

inline bool intersects(Rect32 lhs, Rect32 rhs)
{
  lhs = normalize(lhs);
  rhs = normalize(rhs);
  return !(lhs.hx < rhs.lx || rhs.hx < lhs.lx || lhs.hy < rhs.ly || rhs.hy < lhs.ly);
}

}  // namespace ecc::geometry
