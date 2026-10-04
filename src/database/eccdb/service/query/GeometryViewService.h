#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "eccdb/Database.h"

namespace eccdb {

// Renderer-facing query/edit service. The canonical geometry remains owned by
// DesignGeometryStorage; this service only exposes a batch boundary for GUI
// and native renderer clients.
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

class GeometryViewService
{
 public:
  explicit GeometryViewService(Database& database) noexcept : _database(&database) {}

  [[nodiscard]] GeometryShapeBatch query(Rect box) const;
  [[nodiscard]] std::optional<uint64_t> hitTest(Point point, int32_t tolerance = 0) const;
  [[nodiscard]] bool contains(uint64_t id) const;
  [[nodiscard]] uint64_t geometryEpoch() const;

  void moveShape(uint64_t id, int32_t dx, int32_t dy);
  [[nodiscard]] bool destroyShape(uint64_t id);

 private:
  Database* _database;
};

}  // namespace eccdb
