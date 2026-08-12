#pragma once

#include "GeometryPayload.h"
#include "GeometryTypes.h"
#include "OwnerRef.h"

#include <cstdint>

namespace ecc::geometry {

struct GeometryEmitOptions
{
  uint32_t flags = 0;
};

class GeometrySink
{
 public:
  virtual ~GeometrySink() = default;

  virtual ShapeId emit_rect(LayerId layer, Rect32 rect, OwnerRef owner, GeometryEmitOptions options) = 0;
  virtual ShapeId emit_line(LayerId layer, LinePayload line, OwnerRef owner, GeometryEmitOptions options) = 0;
  virtual ShapeId emit_point(LayerId layer, PointPayload point, OwnerRef owner, GeometryEmitOptions options) = 0;
};

}  // namespace ecc::geometry
