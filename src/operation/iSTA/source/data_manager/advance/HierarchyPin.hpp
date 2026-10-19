// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <string>

#include "PinDirection.hpp"

namespace ista {
// Constraint object only: it must never be inserted into the leaf timing graph.
struct HierarchyPin
{
  std::string instance;
  std::string port;
  PinDirection direction = PinDirection::kNone;
  std::string net;
  std::string error;
  size_t leaf_begin = 0;
  size_t leaf_end = 0;
  int depth = 0;
};
}  // namespace ista
