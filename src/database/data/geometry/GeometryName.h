#pragma once

#include "OwnerRef.h"

#include <cstdint>

namespace ecc::geometry {

struct GeometryNameRecord
{
  OwnerType owner_type = OwnerType::kNone;
  uint8_t reserved0 = 0;
  uint16_t flags = 0;
  OwnerId owner_id = 0;
  uint64_t name_offset = 0;
  uint32_t name_size = 0;
  uint32_t reserved1 = 0;
};

}  // namespace ecc::geometry
