// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "geometry/GeometryHitTestService.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <limits>
#include <span>
#include <stdexcept>

namespace eccdb {
namespace {

struct XY { double x; double y; };

double distanceSquared(XY p, XY a, XY b)
{
  const auto dx = b.x - a.x;
  const auto dy = b.y - a.y;
  const auto length = dx * dx + dy * dy;
  const auto t = length == 0 ? 0 : std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.0, 1.0);
  const auto x = p.x - (a.x + t * dx);
  const auto y = p.y - (a.y + t * dy);
  return x * x + y * y;
}

template <class Points>
bool polygonContains(const Points& points, XY p, double tolerance)
{
  if (points.empty()) return false;
  bool inside = false;
  for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
    const XY a{static_cast<double>(points[j].x), static_cast<double>(points[j].y)};
    const XY b{static_cast<double>(points[i].x), static_cast<double>(points[i].y)};
    if (distanceSquared(p, a, b) <= tolerance * tolerance) return true;
    if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y)) inside = !inside;
  }
  return inside;
}

std::array<XY,4> segmentPolygon(const GeometryPayloadSegment& value)
{
  const double dx = static_cast<double>(value.p1.x) - value.p0.x;
  const double dy = static_cast<double>(value.p1.y) - value.p0.y;
  const auto length = std::hypot(dx, dy);
  const auto lower = std::max(value.width, 0) / 2;
  const auto upper = std::max(value.width, 0) - lower;
  if (dx == 0 || dy == 0) {
    // Integer Manhattan bounds match canonical storage, including odd widths
    // and zero-length patches. Resolved caps include SHAPE truncation.
    double left = std::min(value.p0.x, value.p1.x) - lower;
    double right = std::max(value.p0.x, value.p1.x) + upper;
    double bottom = std::min(value.p0.y, value.p1.y) - lower;
    double top = std::max(value.p0.y, value.p1.y) + upper;
    if (dx != 0) {
      left = dx > 0 ? value.p0.x - value.begin_cap : value.p1.x - value.end_cap;
      right = dx > 0 ? value.p1.x + value.end_cap : value.p0.x + value.begin_cap;
    } else if (dy != 0) {
      bottom = dy > 0 ? value.p0.y - value.begin_cap : value.p1.y - value.end_cap;
      top = dy > 0 ? value.p1.y + value.end_cap : value.p0.y + value.begin_cap;
    }
    return {{{left,bottom}, {right,bottom}, {right,top}, {left,top}}};
  }
  const auto ux = dx / length;
  const auto uy = dy / length;
  const auto half = std::max(value.width, 0) / 2.0;
  const XY begin{value.p0.x - ux * value.begin_cap, value.p0.y - uy * value.begin_cap};
  const XY end{value.p1.x + ux * value.end_cap, value.p1.y + uy * value.end_cap};
  return {{{begin.x - uy * half, begin.y + ux * half},
                                 {end.x - uy * half, end.y + ux * half},
                                 {end.x + uy * half, end.y - ux * half},
                                 {begin.x + uy * half, begin.y - ux * half}}};
}

bool segmentIntersectsBox(XY a, XY b, Rect box)
{
  double lo = 0, hi = 1;
  const auto clip = [&](double start, double delta, double minimum, double maximum) {
    if (delta == 0) return start >= minimum && start <= maximum;
    auto t0 = (minimum - start) / delta;
    auto t1 = (maximum - start) / delta;
    if (t0 > t1) std::swap(t0, t1);
    lo = std::max(lo, t0);
    hi = std::min(hi, t1);
    return lo <= hi;
  };
  return clip(a.x, b.x - a.x, box.ll_x, box.ur_x) && clip(a.y, b.y - a.y, box.ll_y, box.ur_y);
}

template <class Points>
bool polygonIntersectsBox(const Points& points, Rect box)
{
  if (points.empty()) return false;
  if (polygonContains(points, {double(box.ll_x), double(box.ll_y)}, 0)) return true;
  for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
    if (segmentIntersectsBox({double(points[j].x), double(points[j].y)},
                             {double(points[i].x), double(points[i].y)}, box)) return true;
  }
  return false;
}

}  // namespace

std::optional<uint64_t> hitTestGeometry(const GeometryPayloadBatch& batch, Point point, int32_t tolerance,
                                       std::span<const uint64_t> layer_order)
{
  if (tolerance < 0) throw std::invalid_argument("negative hit tolerance");
  const XY p{double(point.x), double(point.y)};
  std::optional<uint32_t> best;
  std::size_t best_layer = 0;
  const auto accept = [&](uint32_t header, uint64_t layer) {
    if (header >= batch.headers.size()) return;
    const auto found = std::find(layer_order.begin(), layer_order.end(), layer);
    const auto rank = found == layer_order.end() ? std::size_t{0} : static_cast<std::size_t>(found - layer_order.begin()) + 1;
    if (!best || rank > best_layer || (rank == best_layer && batch.headers[header].id < batch.headers[*best].id)) {
      best = header;
      best_layer = rank;
    }
  };
  for (const auto& value : batch.segments)
    if (polygonContains(segmentPolygon(value), p, tolerance)) accept(value.header_index, batch.headers[value.header_index].layer);
  for (const auto& value : batch.rectangles) {
    const auto& r = value.rectangle;
    const double x = std::clamp(p.x, double(r.ll_x), double(r.ur_x));
    const double y = std::clamp(p.y, double(r.ll_y), double(r.ur_y));
    if ((p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) <= double(tolerance) * tolerance)
      accept(value.header_index, value.layer);
  }
  for (const auto& value : batch.polygons) {
    const auto points = std::span<const Point>(batch.polygon_points).subspan(value.point_offset, value.point_count);
    if (polygonContains(points, p, tolerance)) accept(value.header_index, value.layer);
  }
  for (const auto& value : batch.points) {
    // VIRTUAL routing points are serialization markers, not metal.
    if ((value.flags & 2u) == 0 && std::hypot(p.x - value.point.x, p.y - value.point.y) <= tolerance)
      accept(value.header_index, value.layer);
  }
  return best ? std::optional<uint64_t>{batch.headers[*best].id} : std::nullopt;
}

std::vector<uint64_t> intersectGeometry(const GeometryPayloadBatch& batch, Rect box)
{
  box = box.normalized();
  std::vector<bool> matched(batch.headers.size(), false);
  for (const auto& value : batch.segments)
    if (polygonIntersectsBox(segmentPolygon(value), box)) matched[value.header_index] = true;
  for (const auto& value : batch.rectangles)
    if (value.rectangle.intersects(box)) matched[value.header_index] = true;
  for (const auto& value : batch.polygons) {
    const auto points = std::span<const Point>(batch.polygon_points).subspan(value.point_offset, value.point_count);
    if (polygonIntersectsBox(points, box)) matched[value.header_index] = true;
  }
  for (const auto& value : batch.points)
    if ((value.flags & 2u) == 0 && value.point.x >= box.ll_x && value.point.x <= box.ur_x
        && value.point.y >= box.ll_y && value.point.y <= box.ur_y) matched[value.header_index] = true;
  std::vector<uint64_t> result;
  for (std::size_t index = 0; index < matched.size(); ++index)
    if (matched[index]) result.push_back(batch.headers[index].id);
  return result;
}

}  // namespace eccdb
