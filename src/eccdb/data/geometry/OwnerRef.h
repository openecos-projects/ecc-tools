#pragma once

#include "GeometryTypes.h"

#include <string_view>

namespace ecc::geometry {

enum class OwnerType : uint8_t
{
  kNone = 0,
  kDie = 1,
  kCore = 2,
  kRow = 3,
  kInstanceBBox = 4,
  kInstanceHalo = 5,
  kNetWireSegment = 6,
  kSpecialWireSegment = 7,
  kVia = 8,
  kPinPortShape = 9,
  kBlockage = 10,
  kFill = 11,
  kRegion = 12,
  kSlot = 13,
  kTrackGrid = 14,
  kGCellGrid = 15,
  kObs = 16,
  kInstancePinPortShape = 17,
  kIoPinPortShape = 18,
};

struct OwnerRef
{
  OwnerType type = OwnerType::kNone;
  uint8_t reserved0 = 0;
  uint16_t flags = 0;
  OwnerId owner_id = 0;
  uint32_t path0 = 0;
  uint32_t path1 = 0;
  uint32_t path2 = 0;
  uint32_t path3 = 0;
  NameId name_id = 0;
};

inline std::string_view owner_type_label(OwnerType type)
{
  switch (type) {
    case OwnerType::kNone:
      return "none";
    case OwnerType::kDie:
      return "die";
    case OwnerType::kCore:
      return "core";
    case OwnerType::kRow:
      return "row";
    case OwnerType::kInstanceBBox:
      return "instance_bbox";
    case OwnerType::kInstanceHalo:
      return "instance_halo";
    case OwnerType::kNetWireSegment:
      return "net_wire_segment";
    case OwnerType::kSpecialWireSegment:
      return "special_wire_segment";
    case OwnerType::kVia:
      return "via";
    case OwnerType::kPinPortShape:
      return "pin_port_shape";
    case OwnerType::kBlockage:
      return "blockage";
    case OwnerType::kFill:
      return "fill";
    case OwnerType::kRegion:
      return "region";
    case OwnerType::kSlot:
      return "slot";
    case OwnerType::kTrackGrid:
      return "track_grid";
    case OwnerType::kGCellGrid:
      return "gcell_grid";
    case OwnerType::kObs:
      return "obs";
    case OwnerType::kInstancePinPortShape:
      return "instance_pin_port_shape";
    case OwnerType::kIoPinPortShape:
      return "io_pin_port_shape";
  }

  return "unknown";
}

}  // namespace ecc::geometry
