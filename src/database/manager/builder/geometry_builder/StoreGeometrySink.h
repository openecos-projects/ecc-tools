#pragma once

#include "GeometrySink.h"
#include "GeometryStore.h"

namespace ecc::geometry {

class StoreGeometrySink final : public GeometrySink
{
 public:
  explicit StoreGeometrySink(GeometryStore& store);

  ShapeId emit_rect(LayerId layer, Rect32 rect, OwnerRef owner, GeometryEmitOptions options) override;
  ShapeId emit_line(LayerId layer, LinePayload line, OwnerRef owner, GeometryEmitOptions options) override;
  ShapeId emit_point(LayerId layer, PointPayload point, OwnerRef owner, GeometryEmitOptions options) override;

 private:
  GeometryStore& _store;
};

}  // namespace ecc::geometry
