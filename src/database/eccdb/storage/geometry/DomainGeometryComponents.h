#pragma once

#include <cstdint>
#include <vector>

#include "common/GeometryTypes.h"
#include "geometry/GeometryEntity.h"

namespace eccdb {

// Geometry is stored as one EnTT entity per physical primitive.  These
// components intentionally contain only the primitive payload; owner and
// source metadata live in the common GeometryShape component.
struct GeometryRectangle
{
  Rect rectangle;
};

struct GeometryPolygon
{
  std::vector<Point> points;
};

// DEF DIEAREA and the operational core boundary preserve their source order.
// Two opposite corners represent a rectangle; otherwise points form a polygon.
struct GeometryBoundary
{
  std::vector<Point> points;
};

template <>
struct GeometryPayloadTraits<GeometryRectangle>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kRectangle;
};

template <>
struct GeometryPayloadTraits<GeometryPolygon>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kPolygon;
};

template <>
struct GeometryPayloadTraits<GeometryBoundary>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kBoundary;
};

}  // namespace eccdb
