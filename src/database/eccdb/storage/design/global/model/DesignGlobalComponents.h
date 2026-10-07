#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "geometry/GeometryEntity.h"

namespace eccdb {

struct DesignRoot
{
  // Monotonic allocation state for embedded instance terms. This is
  // persistent so an archive restore cannot reuse an ID that was deleted
  // before the checkpoint.
  uint64_t next_instance_term_id = 0;
};

// Required DEF design header values represented on the singleton DesignRoot.
struct DesignInfo
{
  std::string name;
  int32_t database_units_per_micron = 0;
  std::string divider_character = "/";
  std::string bus_bit_characters = "[]";
};

// DEF DIEAREA accepts a rectangle or a rectilinear polygon.
struct DesignDieArea
{
  GeometryShapeId boundary;
};

struct DesignDieAreaInput
{
  std::vector<Point> boundary;
};

[[nodiscard]] inline GeometryOwner designDieAreaOwner(DesignRootId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignDieArea, id);
}

}  // namespace eccdb
