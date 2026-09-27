#pragma once

#include "GeometryTypes.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ecc::geometry {

struct GeometryDrcViolation
{
  Rect32 bbox;
  std::vector<std::string> nets;
  std::vector<std::string> instances;
  std::optional<int64_t> required_size;
};

using GeometryDrcDistribution = std::map<std::string, std::map<std::string, std::vector<GeometryDrcViolation>>>;

}  // namespace ecc::geometry
