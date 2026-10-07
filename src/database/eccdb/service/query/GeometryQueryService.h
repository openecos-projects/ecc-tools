#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "eccdb/Database.h"
#include "design/geometry/DesignGeometrySpatialIndex.h"

namespace eccdb {

// Native, GUI-independent geometry query boundary. GeometryStorage remains
// the source of truth; this service only returns viewport-sized snapshots.
struct GeometryShapeRecord
{
  uint64_t id = UINT64_MAX;
  uint8_t kind = 0;
  uint8_t owner_domain = 0;
  uint64_t owner = UINT64_MAX;
  uint64_t net = UINT64_MAX;
  uint64_t wire = UINT64_MAX;
  uint64_t layer = UINT64_MAX;
  int32_t ll_x = 0;
  int32_t ll_y = 0;
  int32_t ur_x = 0;
  int32_t ur_y = 0;
  uint32_t flags = 0;
  uint32_t source_path = UINT32_MAX;
};

struct GeometryShapeBatch
{
  std::vector<GeometryShapeRecord> records;
  uint64_t geometry_epoch = 0;
};

// A lightweight spatial result used by GUI/model adapters. It has the same
// stable metadata as GeometryShapeRecord but is explicitly a reference batch:
// exact typed geometry is fetched later by ShapeId. BBox is only an index
// result and must not be used as the canonical shape payload.
struct GeometryShapeRef
{
  uint64_t id = UINT64_MAX;
  uint8_t kind = 0;
  uint8_t owner_domain = 0;
  uint64_t owner = UINT64_MAX;
  uint64_t net = UINT64_MAX;
  uint64_t wire = UINT64_MAX;
  uint64_t layer = UINT64_MAX;
  Rect bounds{};
  uint32_t flags = 0;
  uint32_t source_path = UINT32_MAX;
};

struct GeometryShapeRefBatch
{
  std::vector<GeometryShapeRef> records;
  uint64_t geometry_epoch = 0;
};

// A detached, renderer-facing snapshot of the lazy geometry overview.  Tile
// records contain only aggregate metadata; canonical GeometryShape objects
// remain in DesignGeometryStorage and are never copied into this batch.
struct GeometryOverviewLayerRecord
{
  uint64_t layer = UINT64_MAX;
  uint32_t shape_count = 0;
  Rect bounds{};
  std::array<uint64_t, 4> occupancy{};
};

struct GeometryOverviewTileRecord
{
  uint8_t level = 0;
  int64_t tile_x = 0;
  int64_t tile_y = 0;
  Rect bounds{};
  Rect occupied{};
  uint32_t shape_count = 0;
  uint64_t covered_area = 0;
  std::array<uint64_t, 4> occupancy{};
  std::vector<GeometryOverviewLayerRecord> layers;
  uint64_t geometry_epoch = 0;
};

struct GeometryOverviewBatch
{
  std::vector<GeometryOverviewTileRecord> tiles;
  uint8_t level = 0;
  int64_t base_tile_size = 4096;
  uint64_t geometry_epoch = 0;
};

class GeometryQueryService
{
 public:
  explicit GeometryQueryService(Database& database) noexcept : _database(&database) {}

  [[nodiscard]] GeometryShapeBatch query(Rect box) const;
  [[nodiscard]] GeometryShapeRefBatch queryRefs(Rect box, const GeometryQueryFilter& filter = {}) const;
  void warmupIndex() const;
  [[nodiscard]] bool indexReady() const;
  [[nodiscard]] std::size_t indexedShapeCount() const;
  // Preferred bounds for camera fitting. This reads DIEAREA or floorplan
  // core/row bounds and never builds the geometry index or materializes Shape
  // records. An empty optional means the design has no usable floorplan.
  [[nodiscard]] std::optional<Rect> designBounds() const;
  // Returns sparse LOD tiles for overview rendering.  This is a derived
  // summary and intentionally does not expose or duplicate shape payloads.
  [[nodiscard]] GeometryOverviewBatch queryOverview(Rect box, uint8_t level = 0) const;
  // Install a detached overview summary into the runtime cache after its
  // producer has validated the source identity. The epoch check is repeated
  // here so a sidecar cannot resurrect stale geometry after an edit.
  [[nodiscard]] bool seedOverview(const GeometryOverviewBatch& batch) const;
  [[nodiscard]] std::optional<uint64_t> hitTest(Point point, int32_t tolerance = 0,
                                             const GeometryQueryFilter& filter = {}) const;
  // Return spatial candidates directly from the R-tree.  This is deliberately
  // a broad-phase query: it intersects the canonical Shape BBoxes and does
  // not materialize exact segment/polygon payloads.  Interactive marquee
  // callers may use it for large selections, while small selections keep the
  // exact primitive predicate below.
  [[nodiscard]] std::vector<uint64_t> queryShapeIds(Rect box,
                                                    const GeometryQueryFilter& filter = {}) const;
  [[nodiscard]] std::vector<uint64_t> intersectShapes(Rect box, const GeometryQueryFilter& filter = {}) const;
  [[nodiscard]] bool contains(uint64_t id) const;
  [[nodiscard]] uint64_t geometryEpoch() const;

 private:
  Database* _database;
};

}  // namespace eccdb
