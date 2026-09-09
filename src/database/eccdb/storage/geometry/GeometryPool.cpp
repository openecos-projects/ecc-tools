#include "geometry/GeometryPool.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "geometry/PolygonRectDecomposer.h"

namespace eccdb {
namespace {

template <typename Value>
GeometryPoolRange appendValues(std::vector<Value>& target, std::span<const Value> values)
{
  const auto existing = target.size();
  const auto appended = values.size();
  if (existing > std::numeric_limits<uint32_t>::max() || appended > std::numeric_limits<uint32_t>::max()
      || appended > std::numeric_limits<uint32_t>::max() - existing) {
    throw std::overflow_error("geometry pool range exceeds uint32_t");
  }
  const auto result = GeometryPoolRange{.offset = static_cast<uint32_t>(existing), .count = static_cast<uint32_t>(appended)};
  target.insert(target.end(), values.begin(), values.end());
  return result;
}

void validateRange(GeometryPoolRange range, std::size_t size, const char* kind)
{
  if (range.offset > size || range.count > size - range.offset) {
    throw std::out_of_range(std::string{"invalid geometry "} + kind + " range");
  }
}

template <typename Value>
std::span<const Value> valuesInRange(const std::vector<Value>& values, GeometryPoolRange source, const char* kind)
{
  validateRange(source, values.size(), kind);
  if (source.count == 0) {
    return {};
  }
  return std::span<const Value>{values.data() + source.offset, source.count};
}

}  // namespace

GeometryPool::GeometryPool(GeometryPoolOptions options) : _options(options)
{
}

const GeometryPoolOptions& GeometryPool::options() const noexcept
{
  return _options;
}

GeometryPoolCheckpoint GeometryPool::checkpoint() const noexcept
{
  return GeometryPoolCheckpoint{.entry_count = static_cast<uint32_t>(_entries.size()),
                                .rectangle_count = static_cast<uint32_t>(_rectangles.size()),
                                .point_count = static_cast<uint32_t>(_points.size()),
                                .polygon_count = static_cast<uint32_t>(_polygons.size())};
}

void GeometryPool::rollback(GeometryPoolCheckpoint checkpoint)
{
  if (checkpoint.entry_count > _entries.size() || checkpoint.rectangle_count > _rectangles.size() || checkpoint.point_count > _points.size()
      || checkpoint.polygon_count > _polygons.size()) {
    throw std::out_of_range("geometry checkpoint is outside this pool");
  }
  _entries.resize(checkpoint.entry_count);
  _rectangles.resize(checkpoint.rectangle_count);
  _points.resize(checkpoint.point_count);
  _polygons.resize(checkpoint.polygon_count);
}

GeometryHandle GeometryPool::appendRectangles(std::span<const Rect> rectangles)
{
  return append(rectangles, {});
}

GeometryHandle GeometryPool::appendPolygons(std::span<const GeometryPolygonInput> polygons)
{
  return append({}, polygons);
}

GeometryHandle GeometryPool::append(const GeometryInput& input)
{
  return append(input.rects, input.polygons);
}

GeometryHandle GeometryPool::append(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons)
{
  validateInput(rectangles, polygons);
  return appendRaw(rectangles, polygons);
}

GeometryHandle GeometryPool::appendTrusted(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons)
{
  return appendRaw(rectangles, polygons);
}

GeometryHandle GeometryPool::appendRaw(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons)
{
  const auto mark = checkpoint();
  try {
    GeometryPoolEntry stored;
    if (_options.polygon_mode == PolygonStorageMode::kRectangularized) {
      // Most LEF library geometry is already rectangular. Append it directly
      // so the OpenDB-style callback path does not allocate a second copy.
      if (polygons.empty()) {
        stored.rectangles = appendValues(_rectangles, rectangles);
      } else {
      std::vector<Rect> converted;
      converted.reserve(rectangles.size());
      converted.insert(converted.end(), rectangles.begin(), rectangles.end());
      for (const auto& polygon : polygons) {
        auto decomposed = decomposePolygonToRectangles(std::span<const Point>{polygon.points});
        converted.insert(converted.end(), decomposed.begin(), decomposed.end());
      }
      stored.rectangles = appendValues(_rectangles, std::span<const Rect>{converted});
      }
    } else {
      stored.rectangles = appendValues(_rectangles, rectangles);

      std::vector<GeometryPoolPolygon> polygon_records;
      polygon_records.reserve(polygons.size());
      for (const auto& polygon : polygons) {
        polygon_records.push_back(GeometryPoolPolygon{.points = appendValues(_points, std::span<const Point>{polygon.points})});
      }
      stored.polygons = appendValues(_polygons, std::span<const GeometryPoolPolygon>{polygon_records});
    }

    if (_entries.size() >= std::numeric_limits<uint32_t>::max()) {
      throw std::overflow_error("geometry pool entry count exceeds uint32_t");
    }
    const GeometryHandle handle{.index = static_cast<uint32_t>(_entries.size())};
    _entries.push_back(stored);
    return handle;
  } catch (...) {
    rollback(mark);
    throw;
  }
}

std::span<const Rect> GeometryPool::rectangles(GeometryHandle handle) const
{
  return valuesInRange(_rectangles, entry(handle).rectangles, "rectangle");
}

uint32_t GeometryPool::polygonCount(GeometryHandle handle) const
{
  return entry(handle).polygons.count;
}

std::span<const Point> GeometryPool::polygonPoints(GeometryHandle handle, uint32_t polygon_index) const
{
  const auto polygon_records = valuesInRange(_polygons, entry(handle).polygons, "polygon");
  if (polygon_index >= polygon_records.size()) {
    throw std::out_of_range("invalid geometry polygon index");
  }
  return valuesInRange(_points, polygon_records[polygon_index].points, "point");
}

bool GeometryPool::empty(GeometryHandle handle) const
{
  const auto& value = entry(handle);
  return value.rectangles.count == 0 && value.polygons.count == 0;
}

uint32_t GeometryPool::shapeCount(GeometryHandle handle) const
{
  const auto& value = entry(handle);
  if (value.rectangles.count > std::numeric_limits<uint32_t>::max() - value.polygons.count) {
    throw std::overflow_error("geometry shape count exceeds uint32_t");
  }
  return value.rectangles.count + value.polygons.count;
}

Rect GeometryPool::bounds(GeometryHandle handle) const
{
  Rect result;
  bool has_bounds = false;
  for (const auto rectangle : rectangles(handle)) {
    result = has_bounds ? result.united(rectangle) : rectangle;
    has_bounds = true;
  }
  for (uint32_t index = 0; index < polygonCount(handle); ++index) {
    const auto polygon_bounds = boundsFor(polygonPoints(handle, index));
    result = has_bounds ? result.united(polygon_bounds) : polygon_bounds;
    has_bounds = true;
  }
  return result;
}

uint32_t GeometryPool::entryCount() const noexcept
{
  return static_cast<uint32_t>(_entries.size());
}

uint32_t GeometryPool::rectangleCount() const noexcept
{
  return static_cast<uint32_t>(_rectangles.size());
}

uint32_t GeometryPool::pointCount() const noexcept
{
  return static_cast<uint32_t>(_points.size());
}

uint32_t GeometryPool::polygonCount() const noexcept
{
  return static_cast<uint32_t>(_polygons.size());
}

std::span<const GeometryPoolEntry> GeometryPool::serializedEntries() const noexcept
{
  return _entries;
}

std::span<const Rect> GeometryPool::serializedRectangles() const noexcept
{
  return _rectangles;
}

std::span<const Point> GeometryPool::serializedPoints() const noexcept
{
  return _points;
}

std::span<const GeometryPoolPolygon> GeometryPool::serializedPolygons() const noexcept
{
  return _polygons;
}

void GeometryPool::restoreSerialized(GeometryPoolOptions options, std::vector<GeometryPoolEntry> entries, std::vector<Rect> rectangles,
                                     std::vector<Point> points, std::vector<GeometryPoolPolygon> polygons)
{
  if (entries.size() > std::numeric_limits<uint32_t>::max() || rectangles.size() > std::numeric_limits<uint32_t>::max()
      || points.size() > std::numeric_limits<uint32_t>::max() || polygons.size() > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("serialized geometry pool exceeds uint32_t");
  }
  for (const auto& value : entries) {
    validateRange(value.rectangles, rectangles.size(), "serialized rectangle");
    validateRange(value.polygons, polygons.size(), "serialized polygon");
  }
  for (const auto& polygon : polygons) {
    validateRange(polygon.points, points.size(), "serialized polygon point");
  }
  _options = options;
  _entries = std::move(entries);
  _rectangles = std::move(rectangles);
  _points = std::move(points);
  _polygons = std::move(polygons);
}

const GeometryPoolEntry& GeometryPool::entry(GeometryHandle handle) const
{
  if (!handle.valid() || handle.index >= _entries.size()) {
    throw std::out_of_range("invalid geometry handle");
  }
  return _entries[handle.index];
}

void GeometryPool::validateInput(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons)
{
  for (const auto rectangle : rectangles) {
    if (!rectangle.isValid() || !rectangle.hasArea()) {
      throw std::invalid_argument("geometry rectangle must have area");
    }
  }
  for (const auto& polygon : polygons) {
    if (polygon.points.size() < 3u) {
      throw std::invalid_argument("geometry polygon requires at least three points");
    }
    if (!boundsFor(std::span<const Point>{polygon.points}).hasArea()) {
      throw std::invalid_argument("geometry polygon has no area");
    }
  }
}

Rect GeometryPool::boundsFor(std::span<const Point> points)
{
  Rect result;
  bool has_bounds = false;
  for (const auto point : points) {
    const Rect point_bounds{.ll_x = point.x, .ll_y = point.y, .ur_x = point.x, .ur_y = point.y};
    result = has_bounds ? result.united(point_bounds) : point_bounds;
    has_bounds = true;
  }
  return result;
}

}  // namespace eccdb
