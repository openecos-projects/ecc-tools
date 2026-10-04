#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "eccdb/Database.h"

namespace eccdb {

// Native, GUI-independent geometry query boundary. GeometryStorage remains
// the source of truth; this service only returns viewport-sized snapshots.
struct GeometryShapeRecord
{
  uint64_t id = UINT64_MAX;
  uint8_t kind = 0;
  uint8_t owner_domain = 0;
  uint64_t owner = UINT64_MAX;
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

class GeometryQueryService
{
 public:
  explicit GeometryQueryService(Database& database) noexcept : _database(&database) {}

  [[nodiscard]] GeometryShapeBatch query(Rect box) const;
  [[nodiscard]] std::optional<uint64_t> hitTest(Point point, int32_t tolerance = 0) const;
  [[nodiscard]] bool contains(uint64_t id) const;
  [[nodiscard]] uint64_t geometryEpoch() const;

 private:
  Database* _database;
};

}  // namespace eccdb
