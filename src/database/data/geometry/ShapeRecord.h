#pragma once

#include "GeometryTypes.h"

#include <cstdint>

namespace ecc::geometry {

struct ShapeRecord
{
  ShapeId id = 0;
  ShapeVersion version = 0;
  LayerId layer_id = 0;
  ShapeKind kind = ShapeKind::kRect;
  ShapeState state = ShapeState::kAlive;
  uint16_t flags = 0;
  uint32_t owner_index = 0;
  uint64_t payload_offset = 0;
  uint32_t payload_size = 0;
  uint32_t style_class = 0;
  Rect32 bbox;
};

}  // namespace ecc::geometry
