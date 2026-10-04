#include "query/GeometryQueryService.h"

#include <stdexcept>

#include "api/internal/DatabaseState.h"
#include "design/DesignStore.h"

namespace eccdb {

namespace {

GeometryShapeId toShapeId(uint64_t value)
{
  return GeometryShapeId{static_cast<GeometryEntity>(value)};
}

GeometryShapeRecord recordFor(const DesignGeometryStorage& geometry, GeometryShapeId id)
{
  const auto view = geometry.shape(id);
  const auto bounds = geometry.bounds(id);
  const auto& metadata = *view.metadata;
  return GeometryShapeRecord{
      .id = id.packed(),
      .kind = static_cast<uint8_t>(metadata.kind),
      .owner_domain = static_cast<uint8_t>(metadata.owner.domain),
      .owner = metadata.owner.value,
      .layer = metadata.layer.packed(),
      .ll_x = bounds.ll_x,
      .ll_y = bounds.ll_y,
      .ur_x = bounds.ur_x,
      .ur_y = bounds.ur_y,
      .flags = metadata.flags,
      .source_path = metadata.source_path,
  };
}

}  // namespace

GeometryShapeBatch GeometryQueryService::query(Rect box) const
{
  if (!_database) throw std::logic_error("GeometryQueryService has no database");
  const auto& design = _database->state().design();
  const auto& geometry = design.geometryStorage();
  GeometryShapeBatch result;
  result.geometry_epoch = geometry.geometryEpoch();
  for (const auto id : design.geometryIndex().query(box.normalized())) {
    if (geometry.contains(id)) result.records.push_back(recordFor(geometry, id));
  }
  return result;
}

std::optional<uint64_t> GeometryQueryService::hitTest(Point point, int32_t tolerance) const
{
  if (tolerance < 0) throw std::invalid_argument("hit-test tolerance must be non-negative");
  const Rect box{point.x - tolerance, point.y - tolerance, point.x + tolerance, point.y + tolerance};
  const auto batch = query(box);
  for (const auto& record : batch.records) {
    const Rect bounds{record.ll_x, record.ll_y, record.ur_x, record.ur_y};
    if (bounds.contains(point)) return record.id;
  }
  return std::nullopt;
}

bool GeometryQueryService::contains(uint64_t id) const
{
  if (!_database) return false;
  return _database->state().design().geometryStorage().contains(toShapeId(id));
}

uint64_t GeometryQueryService::geometryEpoch() const
{
  if (!_database) return 0;
  return _database->state().design().geometryStorage().geometryEpoch();
}

}  // namespace eccdb
