// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstddef>
#include <string>

namespace ista {
// Identifies one arc set, including distinct sets with identical port names.
// Library reload/reordering requires graph rebuilding; vector relocation does not.
struct LibertyArcId
{
  std::string cell;
  std::size_t index;
};
}  // namespace ista
