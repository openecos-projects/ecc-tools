// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <algorithm>
#include <cstdint>

namespace eccdb {

struct Point
{
  int32_t x = 0;
  int32_t y = 0;

  template <typename Visitor>
  void visitFields(Visitor& visitor)
  {
    visitor("x", x);
    visitor("y", y);
  }

  template <typename Visitor>
  void visitFields(Visitor& visitor) const
  {
    visitor("x", x);
    visitor("y", y);
  }

  friend bool operator==(const Point&, const Point&) = default;
};

struct Rect
{
  int32_t ll_x = 0;
  int32_t ll_y = 0;
  int32_t ur_x = 0;
  int32_t ur_y = 0;

  [[nodiscard]] bool isValid() const noexcept { return ll_x <= ur_x && ll_y <= ur_y; }
  [[nodiscard]] bool hasArea() const noexcept { return ll_x < ur_x && ll_y < ur_y; }
  [[nodiscard]] int64_t width() const noexcept { return static_cast<int64_t>(ur_x) - ll_x; }
  [[nodiscard]] int64_t height() const noexcept { return static_cast<int64_t>(ur_y) - ll_y; }
  [[nodiscard]] int64_t area() const noexcept { return hasArea() ? width() * height() : 0; }

  [[nodiscard]] Point center() const noexcept
  {
    return Point{ll_x + static_cast<int32_t>(width() / 2), ll_y + static_cast<int32_t>(height() / 2)};
  }

  [[nodiscard]] Rect normalized() const noexcept
  {
    return Rect{.ll_x = std::min(ll_x, ur_x),
                .ll_y = std::min(ll_y, ur_y),
                .ur_x = std::max(ll_x, ur_x),
                .ur_y = std::max(ll_y, ur_y)};
  }

  [[nodiscard]] Rect offset(int32_t dx, int32_t dy) const noexcept
  {
    return Rect{.ll_x = ll_x + dx, .ll_y = ll_y + dy, .ur_x = ur_x + dx, .ur_y = ur_y + dy};
  }

  [[nodiscard]] Rect enlarged(int32_t delta) const noexcept
  {
    return enlarged(delta, delta, delta, delta);
  }

  [[nodiscard]] Rect enlarged(int32_t left, int32_t bottom, int32_t right, int32_t top) const noexcept
  {
    return Rect{.ll_x = ll_x - left,
                .ll_y = ll_y - bottom,
                .ur_x = ur_x + right,
                .ur_y = ur_y + top};
  }

  [[nodiscard]] bool contains(Point point, bool boundary = true) const noexcept
  {
    if (!isValid()) return false;
    if (boundary) {
      return point.x >= ll_x && point.x <= ur_x && point.y >= ll_y && point.y <= ur_y;
    }
    return point.x > ll_x && point.x < ur_x && point.y > ll_y && point.y < ur_y;
  }

  [[nodiscard]] bool contains(Rect rect, bool boundary = true) const noexcept
  {
    if (!isValid() || !rect.isValid()) return false;
    if (boundary) {
      return rect.ll_x >= ll_x && rect.ur_x <= ur_x && rect.ll_y >= ll_y && rect.ur_y <= ur_y;
    }
    return rect.ll_x > ll_x && rect.ur_x < ur_x && rect.ll_y > ll_y && rect.ur_y < ur_y;
  }

  [[nodiscard]] bool intersects(Rect rect, bool boundary = true) const noexcept
  {
    if (!isValid() || !rect.isValid()) return false;
    if (boundary) {
      return !(ur_x < rect.ll_x || rect.ur_x < ll_x || ur_y < rect.ll_y || rect.ur_y < ll_y);
    }
    return !(ur_x <= rect.ll_x || rect.ur_x <= ll_x || ur_y <= rect.ll_y || rect.ur_y <= ll_y);
  }

  [[nodiscard]] Rect united(Rect rect) const noexcept
  {
    if (!isValid()) return rect;
    if (!rect.isValid()) return *this;
    return Rect{.ll_x = std::min(ll_x, rect.ll_x),
                .ll_y = std::min(ll_y, rect.ll_y),
                .ur_x = std::max(ur_x, rect.ur_x),
                .ur_y = std::max(ur_y, rect.ur_y)};
  }

  template <typename Visitor>
  void visitFields(Visitor& visitor)
  {
    visitor("ll_x", ll_x);
    visitor("ll_y", ll_y);
    visitor("ur_x", ur_x);
    visitor("ur_y", ur_y);
  }

  template <typename Visitor>
  void visitFields(Visitor& visitor) const
  {
    visitor("ll_x", ll_x);
    visitor("ll_y", ll_y);
    visitor("ur_x", ur_x);
    visitor("ur_y", ur_y);
  }

  friend bool operator==(const Rect&, const Rect&) = default;
};

enum class GeometryLayerKind : uint8_t
{
  kNone,
  kRouting,
  kCut,
  kOther
};

}  // namespace eccdb
