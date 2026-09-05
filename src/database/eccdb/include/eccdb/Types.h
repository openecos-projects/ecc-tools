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
#include <compare>
#include <cstdint>
#include <functional>
#include <limits>

namespace eccdb {

// Object IDs are lightweight database-local identities. They remain valid
// until the entity is destroyed or its owning Database is destroyed. A packed
// value from one Database must not be used with another Database.
template <typename Tag>
class ObjectId
{
 public:
  ObjectId() noexcept = default;
  explicit ObjectId(uint64_t value) noexcept : _value(value) {}

  [[nodiscard]] explicit operator bool() const noexcept { return _value != kInvalid; }
  [[nodiscard]] uint64_t packed() const noexcept { return _value; }

  friend bool operator==(const ObjectId&, const ObjectId&) = default;
  friend auto operator<=>(const ObjectId&, const ObjectId&) = default;

 private:
  static constexpr uint64_t kInvalid = std::numeric_limits<uint64_t>::max();
  uint64_t _value = kInvalid;
};

using InstanceId = ObjectId<struct InstanceTag>;
using InstancePinId = ObjectId<struct InstancePinTag>;
using IoPinId = ObjectId<struct IoPinTag>;
using NetId = ObjectId<struct NetTag>;
// DEF design VIA. Technology-defined LEF vias use TechViaId.
using ViaId = ObjectId<struct ViaTag>;
using WireId = ObjectId<struct WireTag>;
using CellMasterId = ObjectId<struct CellMasterTag>;
using MasterTermId = ObjectId<struct MasterTermTag>;
using LayerId = ObjectId<struct LayerTag>;
using RoutingLayerId = ObjectId<struct RoutingLayerTag>;
using TechViaId = ObjectId<struct TechViaTag>;
using TechRuleId = ObjectId<struct TechRuleTag>;
using DesignRuleId = ObjectId<struct DesignRuleTag>;
using ViaRuleId = ObjectId<struct ViaRuleTag>;

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

enum class Orientation : uint8_t
{
  kN,
  kS,
  kE,
  kW,
  kFN,
  kFS,
  kFE,
  kFW
};

enum class PlacementStatus : uint8_t
{
  kUnplaced,
  kPlaced,
  kFixed,
  kCover
};

enum class InstanceSource : uint8_t
{
  kNone,
  kNetlist,
  kDist,
  kUser,
  kTiming
};

enum class SignalUse : uint8_t
{
  kNone,
  kSignal,
  kAnalog,
  kPower,
  kGround,
  kClock,
  kTieOff,
  kScan,
  kReset
};

enum class IoDirection : uint8_t
{
  kNone,
  kInput,
  kOutput,
  kInOut,
  kFeedThru
};

enum class NetSource : uint8_t
{
  kNone,
  kNetlist,
  kDist,
  kUser,
  kTiming,
  kTest
};

enum class NetPattern : uint8_t
{
  kNone,
  kBalanced,
  kSteiner,
  kTrunk,
  kWiredLogic
};

enum class WireStatus : uint8_t
{
  kRouted,
  kFixed,
  kCover,
  kShield,
  kNoShield
};

}  // namespace eccdb

namespace std {

template <typename Tag>
struct hash<eccdb::ObjectId<Tag>>
{
  [[nodiscard]] size_t operator()(eccdb::ObjectId<Tag> id) const noexcept
  {
    return hash<uint64_t>{}(id.packed());
  }
};

}  // namespace std
