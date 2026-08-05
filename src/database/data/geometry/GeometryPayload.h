#pragma once

#include "GeometryTypes.h"

#include <cstdint>

namespace ecc::geometry {

struct PointPayload
{
  Point32 point;
  uint32_t symbol_id = 0;
  uint32_t flags = 0;
};

struct LinePayload
{
  Point32 begin;
  Point32 end;
  int32_t width = 0;
  uint32_t flags = 0;
};

struct RectPayload
{
  Rect32 rect;
};

}  // namespace ecc::geometry
