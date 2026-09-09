#pragma once

#include <cstdint>
#include <limits>

namespace eccdb {

// Identifies one immutable geometry group inside a database-owned
// GeometryPool. It does not identify an individual rectangle or polygon.
struct GeometryHandle
{
  uint32_t index = std::numeric_limits<uint32_t>::max();

  [[nodiscard]] bool valid() const noexcept { return index != std::numeric_limits<uint32_t>::max(); }
};

[[nodiscard]] inline bool operator==(GeometryHandle lhs, GeometryHandle rhs) noexcept
{
  return lhs.index == rhs.index;
}

[[nodiscard]] inline bool operator!=(GeometryHandle lhs, GeometryHandle rhs) noexcept
{
  return !(lhs == rhs);
}

}  // namespace eccdb
