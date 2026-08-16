#include "StoreGeometrySink.h"

namespace ecc::geometry {

StoreGeometrySink::StoreGeometrySink(GeometryStore& store) : _store(store)
{
}

ShapeId StoreGeometrySink::emit_rect(LayerId layer, Rect32 rect, OwnerRef owner, GeometryEmitOptions options)
{
  return _store.add_rect(layer, rect, owner, options.flags);
}

ShapeId StoreGeometrySink::emit_line(LayerId layer, LinePayload line, OwnerRef owner, GeometryEmitOptions options)
{
  return _store.add_line(layer, line, owner, options.flags);
}

ShapeId StoreGeometrySink::emit_point(LayerId layer, PointPayload point, OwnerRef owner, GeometryEmitOptions options)
{
  return _store.add_point(layer, point, owner, options.flags);
}

}  // namespace ecc::geometry
