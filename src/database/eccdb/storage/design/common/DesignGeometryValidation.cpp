#include "design/common/DesignGeometryValidation.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {
namespace {

bool polygonHasArea(const std::vector<Point>& polygon)
{
  __int128 doubled_area = 0;
  for (std::size_t index = 0; index < polygon.size(); ++index) {
    const auto current = polygon[index];
    const auto next = polygon[(index + 1u) % polygon.size()];
    doubled_area += static_cast<__int128>(current.x) * next.y - static_cast<__int128>(next.x) * current.y;
  }
  return doubled_area != 0;
}

bool inClosedRange(int32_t value, int32_t first, int32_t second)
{
  return value >= std::min(first, second) && value <= std::max(first, second);
}

bool orthogonalSegmentsIntersect(Point first_begin, Point first_end, Point second_begin, Point second_end)
{
  const bool first_vertical = first_begin.x == first_end.x;
  const bool second_vertical = second_begin.x == second_end.x;
  if (first_vertical && second_vertical) {
    return first_begin.x == second_begin.x
           && std::max(std::min(first_begin.y, first_end.y), std::min(second_begin.y, second_end.y))
                  <= std::min(std::max(first_begin.y, first_end.y), std::max(second_begin.y, second_end.y));
  }
  if (!first_vertical && !second_vertical) {
    return first_begin.y == second_begin.y
           && std::max(std::min(first_begin.x, first_end.x), std::min(second_begin.x, second_end.x))
                  <= std::min(std::max(first_begin.x, first_end.x), std::max(second_begin.x, second_end.x));
  }
  if (first_vertical) {
    return inClosedRange(first_begin.x, second_begin.x, second_end.x) && inClosedRange(second_begin.y, first_begin.y, first_end.y);
  }
  return inClosedRange(second_begin.x, first_begin.x, first_end.x) && inClosedRange(first_begin.y, second_begin.y, second_end.y);
}

}  // namespace

Rect validateDesignOrthogonalBoundary(std::span<const Point> boundary, std::string_view object_name)
{
  const auto invalid = [&](std::string_view reason) { throw std::invalid_argument(std::string(object_name) + " " + std::string(reason)); };
  if (boundary.size() == 2u) {
    if (boundary[0].x > boundary[1].x || boundary[0].y > boundary[1].y) {
      invalid("rectangle must have ordered corners");
    }
    return Rect{boundary[0].x, boundary[0].y, boundary[1].x, boundary[1].y};
  }
  if (boundary.size() < 4u || boundary.front() == boundary.back()) {
    invalid("polygon requires at least four non-repeated vertices");
  }

  Rect bounds{boundary.front().x, boundary.front().y, boundary.front().x, boundary.front().y};
  __int128 doubled_area = 0;
  for (std::size_t index = 0; index < boundary.size(); ++index) {
    const auto current = boundary[index];
    const auto next = boundary[(index + 1u) % boundary.size()];
    const auto after_next = boundary[(index + 2u) % boundary.size()];
    if (current == next || (current.x != next.x && current.y != next.y)) {
      invalid("polygon edges must be non-zero and Manhattan");
    }
    const bool current_vertical = current.x == next.x;
    const bool next_vertical = next.x == after_next.x;
    if (current_vertical == next_vertical) {
      invalid("polygon must alternate horizontal and vertical edges");
    }
    doubled_area += static_cast<__int128>(current.x) * next.y - static_cast<__int128>(next.x) * current.y;
    bounds = bounds.united(Rect{current.x, current.y, current.x, current.y});
  }
  if (doubled_area == 0) {
    invalid("polygon must have non-zero area");
  }

  for (std::size_t first = 0; first < boundary.size(); ++first) {
    const auto first_next = (first + 1u) % boundary.size();
    for (std::size_t second = first + 1u; second < boundary.size(); ++second) {
      const auto second_next = (second + 1u) % boundary.size();
      if (first == second || first_next == second || second_next == first) {
        continue;
      }
      if (orthogonalSegmentsIntersect(boundary[first], boundary[first_next], boundary[second], boundary[second_next])) {
        invalid("polygon must not self-intersect");
      }
    }
  }
  return bounds;
}

void validateDesignShapeSet(const DesignShapeSet& shapes, bool require_nonempty)
{
  if (require_nonempty && shapes.rectangles.empty() && shapes.polygons.empty()) {
    throw std::invalid_argument("design geometry requires at least one shape");
  }
  for (const auto rectangle : shapes.rectangles) {
    if (!rectangle.isValid() || !rectangle.hasArea()) {
      throw std::invalid_argument("design rectangle must have area");
    }
  }
  for (const auto& polygon : shapes.polygons) {
    if (polygon.size() < 3u || !polygonHasArea(polygon)) {
      throw std::invalid_argument("design polygon must have at least three non-collinear points");
    }
  }
}

void validateDesignLayerGeometry(const TechRegistry& technology, std::span<const DesignLayerGeometry> geometry, bool require_nonempty)
{
  if (require_nonempty && geometry.empty()) {
    throw std::invalid_argument("design layer geometry requires at least one layer clause");
  }

  const auto& registry = technology.registry();
  for (const auto& clause : geometry) {
    if (!clause.layer || !registry.valid(clause.layer.entity()) || !registry.all_of<TechLayerInfo>(clause.layer.entity())) {
      throw std::invalid_argument("design geometry references an invalid technology layer");
    }
    validateDesignShapeSet(clause.shapes);
  }
}

}  // namespace eccdb
