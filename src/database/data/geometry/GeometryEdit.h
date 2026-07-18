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

enum class GeometryEditDiagnostic : uint16_t
{
  kNone = 0,
  kShapeUnavailable = 1,
  kVersionConflict = 2,
  kUnsupportedOperation = 3,
  kBackendUpdateFailed = 4,
  kStoreUpdateFailed = 5,
  kUnsupportedOwner = 6,
  kInstanceListUnavailable = 7,
  kInstanceOwnerMismatch = 8,
  kInstanceNotFound = 9,
  kOwnerPathUnavailable = 10,
  kUnsupportedTransform = 11,
  kInstancePlacementLocked = 12,
  kInstanceMoveOutsideLayoutBounds = 13,
};

constexpr uint16_t kGeometryEditDiagnosticMask = 0x00ff;

inline GeometryEditDiagnostic geometry_edit_diagnostic(uint16_t flags)
{
  return static_cast<GeometryEditDiagnostic>(flags & kGeometryEditDiagnosticMask);
}

inline uint16_t set_geometry_edit_diagnostic(uint16_t flags, GeometryEditDiagnostic diagnostic)
{
  return static_cast<uint16_t>((flags & ~kGeometryEditDiagnosticMask)
                               | (static_cast<uint16_t>(diagnostic) & kGeometryEditDiagnosticMask));
}

inline const char* geometry_edit_diagnostic_message(GeometryEditDiagnostic diagnostic)
{
  switch (diagnostic) {
    case GeometryEditDiagnostic::kNone:
      return "";
    case GeometryEditDiagnostic::kShapeUnavailable:
      return "shape is missing, deleted, or not a rectangle";
    case GeometryEditDiagnostic::kVersionConflict:
      return "shape version changed before commit";
    case GeometryEditDiagnostic::kUnsupportedOperation:
      return "edit operation is not supported for this owner";
    case GeometryEditDiagnostic::kBackendUpdateFailed:
      return "idb owner update failed; original geometry was preserved";
    case GeometryEditDiagnostic::kStoreUpdateFailed:
      return "geometry store update failed after idb owner update";
    case GeometryEditDiagnostic::kUnsupportedOwner:
      return "owner type is read-only for this edit path";
    case GeometryEditDiagnostic::kInstanceListUnavailable:
      return "design instance list is unavailable";
    case GeometryEditDiagnostic::kInstanceOwnerMismatch:
      return "shape is not the requested instance bbox or resize is unsupported";
    case GeometryEditDiagnostic::kInstanceNotFound:
      return "instance owner was not found in design";
    case GeometryEditDiagnostic::kOwnerPathUnavailable:
      return "owner path does not resolve to an editable idb object";
    case GeometryEditDiagnostic::kUnsupportedTransform:
      return "shape uses an orientation or transform unsupported by this edit path";
    case GeometryEditDiagnostic::kInstancePlacementLocked:
      return "instance placement status is fixed or cover; move is not allowed";
    case GeometryEditDiagnostic::kInstanceMoveOutsideLayoutBounds:
      return "instance move leaves placement core or die bounds";
  }
  return "unknown edit diagnostic";
}

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

inline GeometryEditDiagnostic geometry_edit_diagnostic(const GeometryEditResult& result)
{
  return geometry_edit_diagnostic(result.flags);
}

}  // namespace ecc::geometry
