#pragma once

#include "GeometryTypes.h"

#include <cstdint>

namespace ecc::geometry {

enum class GeometryEditOp : uint8_t
{
  kMoveShape = 1,
  kResizeRect = 2,
  kReplaceLine = 3,
};

enum class GeometryEditStatus : uint8_t
{
  kAccepted = 1,
  kAdjustedAccepted = 2,
  kRejected = 3,
  kConflict = 4,
};

struct GeometryEditCommand
{
  uint64_t command_id = 0;
  ShapeId shape_id = 0;
  ShapeVersion expected_version = 0;
  GeometryEditOp op = GeometryEditOp::kMoveShape;
  uint8_t reserved0 = 0;
  uint16_t flags = 0;
  Rect32 requested_bbox;
  uint64_t payload_offset = 0;
  uint32_t payload_size = 0;
  uint32_t reserved1 = 0;
};

struct GeometryEditResult
{
  uint64_t command_id = 0;
  ShapeId shape_id = 0;
  ShapeVersion new_version = 0;
  GeometryEditStatus status = GeometryEditStatus::kRejected;
  uint8_t reserved0 = 0;
  uint16_t flags = 0;
  Rect32 committed_bbox;
  uint32_t message_offset = 0;
  uint32_t message_size = 0;
};

}  // namespace ecc::geometry
