#include "ClockTreeSnapshot.h"

namespace ecc::geometry {

uint32_t add_clock_tree_string(ClockTreeSnapshot& snapshot, const std::string& value)
{
  const uint32_t offset = static_cast<uint32_t>(snapshot.strings.size());
  snapshot.strings.append(value);
  return offset;
}

void set_string_reference(ClockTreeSnapshot& snapshot, uint32_t& offset, uint32_t& size, const std::string& value)
{
  if (value.empty()) {
    offset = 0;
    size = 0;
    return;
  }
  offset = add_clock_tree_string(snapshot, value);
  size = static_cast<uint32_t>(value.size());
}

}  // namespace ecc::geometry
