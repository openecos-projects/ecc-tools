#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/GeometryTypes.h"

namespace eccdb {

struct DesignRoot
{
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
  std::vector<Point> boundary;
};

}  // namespace eccdb
