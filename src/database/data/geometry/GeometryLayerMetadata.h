#pragma once

#include "GeometryTypes.h"

#include <cstdint>
#include <string>

namespace ecc::geometry {

struct GeometryLayerMetadata
{
  LayerId layer_id = 0;
  uint32_t order = 0;
  std::string name;
  std::string type = "unknown";
  std::string direction = "unknown";
  int32_t width = 0;
  int32_t pitch_x = 0;
  int32_t pitch_y = 0;
  int32_t min_spacing = 0;
  int32_t min_area = 0;
  int32_t min_step = 0;
  int32_t cut_spacing = 0;
  std::string enclosure_below;
  std::string enclosure_above;
  uint32_t lef58_rule_count = 0;
};

}  // namespace ecc::geometry
