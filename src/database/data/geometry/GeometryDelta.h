#pragma once

#include "GeometryTypes.h"

#include <cstdint>

namespace ecc::geometry {

enum class GeometryDeltaOp : uint8_t
{
  kNone = 0,
  kInsert = 1,
  kUpdate = 2,
  kDelete = 3,
};

struct GeometryDeltaEvent
{
  uint64_t sequence_id = 0;
  uint64_t command_id = 0;
  GeometryDeltaOp op = GeometryDeltaOp::kNone;
  uint8_t reserved0 = 0;
  uint16_t reserved1 = 0;
  uint32_t reserved2 = 0;
  ShapeId shape_id = 0;
  ShapeVersion old_version = 0;
  ShapeVersion new_version = 0;
  Rect32 old_bbox;
  Rect32 new_bbox;
};

}  // namespace ecc::geometry
