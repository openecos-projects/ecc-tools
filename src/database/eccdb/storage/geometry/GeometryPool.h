#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "geometry/GeometryHandle.h"
#include "geometry/GeometryInput.h"

namespace eccdb {

// Native storage is lossless and remains the default. Rectangularized storage
// intentionally preserves the legacy iDB Boost.Polygon conversion behavior.
enum class PolygonStorageMode : uint8_t
{
  kNative,
  kRectangularized
};

struct GeometryPoolOptions
{
  PolygonStorageMode polygon_mode = PolygonStorageMode::kNative;
};

// Physical records below belong to GeometryPool persistence. Business
// components retain only GeometryHandle and never depend on these ranges.
struct GeometryPoolRange
{
  uint32_t offset = 0;
  uint32_t count = 0;
};

struct GeometryPoolPolygon
{
  GeometryPoolRange points;
};

struct GeometryPoolEntry
{
  GeometryPoolRange rectangles;
  GeometryPoolRange polygons;
};

struct GeometryPoolCheckpoint
{
  uint32_t entry_count = 0;
  uint32_t rectangle_count = 0;
  uint32_t point_count = 0;
  uint32_t polygon_count = 0;
};

// Append-only geometry storage shared as an implementation by TechStore
// and LibraryStore. Each database owns a separate pool instance.
class GeometryPool
{
 public:
  explicit GeometryPool(GeometryPoolOptions options = {});

  [[nodiscard]] const GeometryPoolOptions& options() const noexcept;
  [[nodiscard]] GeometryPoolCheckpoint checkpoint() const noexcept;
  void rollback(GeometryPoolCheckpoint checkpoint);

  [[nodiscard]] GeometryHandle appendRectangles(std::span<const Rect> rectangles);
  [[nodiscard]] GeometryHandle appendPolygons(std::span<const GeometryPolygonInput> polygons);
  [[nodiscard]] GeometryHandle append(const GeometryInput& input);
  [[nodiscard]] GeometryHandle append(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons);
  // Used by importers after they have validated the parser boundary. This
  // avoids repeating per-rectangle validation for very large LEF geometry.
  [[nodiscard]] GeometryHandle appendTrusted(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons);

  [[nodiscard]] std::span<const Rect> rectangles(GeometryHandle handle) const;
  [[nodiscard]] uint32_t polygonCount(GeometryHandle handle) const;
  [[nodiscard]] std::span<const Point> polygonPoints(GeometryHandle handle, uint32_t polygon_index) const;
  [[nodiscard]] bool empty(GeometryHandle handle) const;
  [[nodiscard]] uint32_t shapeCount(GeometryHandle handle) const;
  [[nodiscard]] Rect bounds(GeometryHandle handle) const;

  [[nodiscard]] uint32_t entryCount() const noexcept;
  [[nodiscard]] uint32_t rectangleCount() const noexcept;
  [[nodiscard]] uint32_t pointCount() const noexcept;
  [[nodiscard]] uint32_t polygonCount() const noexcept;

  [[nodiscard]] std::span<const GeometryPoolEntry> serializedEntries() const noexcept;
  [[nodiscard]] std::span<const Rect> serializedRectangles() const noexcept;
  [[nodiscard]] std::span<const Point> serializedPoints() const noexcept;
  [[nodiscard]] std::span<const GeometryPoolPolygon> serializedPolygons() const noexcept;

  void restoreSerialized(GeometryPoolOptions options, std::vector<GeometryPoolEntry> entries, std::vector<Rect> rectangles,
                         std::vector<Point> points, std::vector<GeometryPoolPolygon> polygons);

 private:
  [[nodiscard]] const GeometryPoolEntry& entry(GeometryHandle handle) const;
  static void validateInput(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons);
  [[nodiscard]] static Rect boundsFor(std::span<const Point> points);
  [[nodiscard]] GeometryHandle appendRaw(std::span<const Rect> rectangles, std::span<const GeometryPolygonInput> polygons);

  std::vector<GeometryPoolEntry> _entries;
  std::vector<Rect> _rectangles;
  std::vector<Point> _points;
  std::vector<GeometryPoolPolygon> _polygons;
  GeometryPoolOptions _options;
};

}  // namespace eccdb
