#include "geometry/PolygonRectDecomposer.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <boost/polygon/polygon.hpp>

namespace eccdb {
namespace {

void validatePolygon(std::span<const Point> points)
{
  if (points.size() < 3u) {
    throw std::invalid_argument("polygon decomposition requires at least three points");
  }

  Rect bounds;
  bool has_bounds = false;
  for (const auto point : points) {
    const Rect point_bounds{.ll_x = point.x, .ll_y = point.y, .ur_x = point.x, .ur_y = point.y};
    bounds = has_bounds ? bounds.united(point_bounds) : point_bounds;
    has_bounds = true;
  }
  if (!bounds.hasArea()) {
    throw std::invalid_argument("polygon decomposition requires non-zero area bounds");
  }
}

}  // namespace

std::vector<Rect> decomposePolygonToRectangles(std::span<const Point> points)
{
  validatePolygon(points);

  namespace gtl = boost::polygon;
  std::vector<gtl::point_data<int32_t>> boost_points;
  boost_points.reserve(points.size());
  for (const auto point : points) {
    boost_points.emplace_back(point.x, point.y);
  }

  gtl::polygon_90_data<int32_t> polygon;
  gtl::set_points(polygon, boost_points.begin(), boost_points.end());

  std::vector<gtl::rectangle_data<int32_t>> boost_rectangles;
  gtl::get_rectangles(boost_rectangles, polygon);
  if (boost_rectangles.empty()) {
    throw std::invalid_argument("polygon decomposition produced no rectangles");
  }

  std::vector<Rect> rectangles;
  rectangles.reserve(boost_rectangles.size());
  for (const auto& rectangle : boost_rectangles) {
    const Rect result{.ll_x = gtl::xl(rectangle), .ll_y = gtl::yl(rectangle), .ur_x = gtl::xh(rectangle), .ur_y = gtl::yh(rectangle)};
    if (!result.isValid() || !result.hasArea()) {
      throw std::invalid_argument("polygon decomposition produced invalid rectangle");
    }
    rectangles.push_back(result);
  }
  return rectangles;
}

}  // namespace eccdb
