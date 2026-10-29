#pragma once

#include "ClockTreeSchema.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ecc::geometry {

struct ClockTreeSnapshot
{
  ClockTreeMetaRecord meta;
  std::vector<ClockRecord> clocks;
  std::vector<ClockNodeRecord> nodes;
  std::vector<ClockEdgeRecord> edges;
  std::vector<ClockTimingRecord> timing;
  std::string strings;
  std::vector<uint32_t> roots;
};

uint32_t add_clock_tree_string(ClockTreeSnapshot& snapshot, const std::string& value);
void set_string_reference(ClockTreeSnapshot& snapshot, uint32_t& offset, uint32_t& size, const std::string& value);

}  // namespace ecc::geometry
