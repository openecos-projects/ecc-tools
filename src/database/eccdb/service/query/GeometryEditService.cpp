#include "query/GeometryEditService.h"

#include <stdexcept>

#include "api/internal/DatabaseState.h"
#include "design/DesignStore.h"

namespace eccdb {

namespace {

GeometryShapeId toShapeId(uint64_t value)
{
  return GeometryShapeId{static_cast<GeometryEntity>(value)};
}

}  // namespace

void GeometryEditService::moveShape(uint64_t id, int32_t dx, int32_t dy)
{
  if (!_database) throw std::logic_error("GeometryEditService has no database");
  _database->state().design().geometryStorage().moveShape(toShapeId(id), dx, dy);
}

bool GeometryEditService::destroyShape(uint64_t id)
{
  if (!_database) return false;
  return _database->state().design().geometryStorage().destroy(toShapeId(id));
}

}  // namespace eccdb
