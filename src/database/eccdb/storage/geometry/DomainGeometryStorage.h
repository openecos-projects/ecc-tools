// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include "geometry/DomainGeometryComponents.h"
#include "geometry/GeometryInput.h"
#include "geometry/GeometryRegistry.h"

namespace eccdb {

// A small typed facade over the canonical EnTT geometry registry.  It is
// deliberately not a pool: every rectangle and polygon gets its own EnTT
// entity and therefore its own generation-checked GeometryShapeId.  Callers
// keep ordered vectors of these IDs on their owning Tech/Library component.
class DomainGeometryStorage
{
 public:
  using ShapeIds = std::vector<GeometryShapeId>;

  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _registry; }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _registry; }

  [[nodiscard]] GeometryShapeId createRectangle(GeometryOwner owner, Rect rectangle)
  {
    rectangle = rectangle.normalized();
    if (!rectangle.isValid() || !rectangle.hasArea()) {
      throw std::invalid_argument("geometry rectangle must have area");
    }
    return _registry.create(owner, GeometryRectangle{.rectangle = rectangle});
  }

  [[nodiscard]] GeometryShapeId createPolygon(GeometryOwner owner, std::vector<Point> points)
  {
    if (points.size() < 3u) {
      throw std::invalid_argument("geometry polygon requires at least three points");
    }
    const auto box = bounds(points);
    if (!box.hasArea()) {
      throw std::invalid_argument("geometry polygon has no area");
    }
    return _registry.create(owner, GeometryPolygon{.points = std::move(points)});
  }

  [[nodiscard]] ShapeIds append(GeometryOwner owner, const GeometryInput& input)
  {
    validate(input);
    return appendUnchecked(owner, input);
  }

  // Importers use this after validating the parser boundary.  The method is
  // still transactional: if one primitive allocation throws, all entities
  // created by this call are destroyed before the exception is propagated.
  [[nodiscard]] ShapeIds appendTrusted(GeometryOwner owner, const GeometryInput& input)
  {
    return appendUnchecked(owner, input);
  }

  void destroy(std::span<const GeometryShapeId> ids) noexcept
  {
    for (const auto id : ids) {
      static_cast<void>(_registry.destroy(id));
    }
  }

  [[nodiscard]] bool contains(GeometryShapeId id) const noexcept { return _registry.contains(id); }

  [[nodiscard]] const GeometryRectangle& rectangle(GeometryShapeId id) const
  {
    return _registry.get<const GeometryRectangle>(checked(id, GeometryShapeKind::kRectangle));
  }

  [[nodiscard]] const GeometryPolygon& polygon(GeometryShapeId id) const
  {
    return _registry.get<const GeometryPolygon>(checked(id, GeometryShapeKind::kPolygon));
  }

  [[nodiscard]] Rect bounds(GeometryShapeId id) const
  {
    const auto& metadata = _registry.shape(id);
    switch (metadata.kind) {
      case GeometryShapeKind::kRectangle:
        return rectangle(id).rectangle;
      case GeometryShapeKind::kPolygon:
        return bounds(polygon(id).points);
      default:
        throw std::invalid_argument("geometry ID is not a rectangle or polygon");
    }
  }

  [[nodiscard]] Rect bounds(std::span<const GeometryShapeId> ids) const
  {
    Rect result{};
    bool initialized = false;
    for (const auto id : ids) {
      const auto value = bounds(id);
      result = initialized ? result.united(value) : value;
      initialized = true;
    }
    return result;
  }

  [[nodiscard]] std::span<const Point> polygonPoints(GeometryShapeId id) const { return polygon(id).points; }

  [[nodiscard]] uint32_t shapeCount(std::span<const GeometryShapeId> ids) const noexcept
  {
    return static_cast<uint32_t>(ids.size());
  }

 private:
  [[nodiscard]] static Rect bounds(std::span<const Point> points)
  {
    if (points.empty()) {
      return {};
    }
    Rect result{points.front().x, points.front().y, points.front().x, points.front().y};
    for (const auto point : points) {
      result.ll_x = std::min(result.ll_x, point.x);
      result.ll_y = std::min(result.ll_y, point.y);
      result.ur_x = std::max(result.ur_x, point.x);
      result.ur_y = std::max(result.ur_y, point.y);
    }
    return result;
  }

  static void validate(const GeometryInput& input)
  {
    for (const auto rectangle : input.rects) {
      const auto normalized = rectangle.normalized();
      if (!normalized.isValid() || !normalized.hasArea()) {
        throw std::invalid_argument("geometry rectangle must have area");
      }
    }
    for (const auto& polygon : input.polygons) {
      if (polygon.points.size() < 3u || !bounds(polygon.points).hasArea()) {
        throw std::invalid_argument("geometry polygon must have area");
      }
    }
  }

  [[nodiscard]] ShapeIds appendUnchecked(GeometryOwner owner, const GeometryInput& input)
  {
    ShapeIds result;
    result.reserve(input.rects.size() + input.polygons.size());
    try {
      for (const auto rectangle : input.rects) {
        result.push_back(_registry.create(owner, GeometryRectangle{.rectangle = rectangle.normalized()}));
      }
      for (const auto& polygon : input.polygons) {
        result.push_back(_registry.create(owner, GeometryPolygon{.points = polygon.points}));
      }
      return result;
    } catch (...) {
      destroy(result);
      throw;
    }
  }

  [[nodiscard]] GeometryShapeId checked(GeometryShapeId id, GeometryShapeKind kind) const
  {
    if (!_registry.contains(id) || _registry.shape(id).kind != kind) {
      throw std::out_of_range("invalid geometry ID or kind");
    }
    return id;
  }

  mutable GeometryRegistry _registry;
};

}  // namespace eccdb
