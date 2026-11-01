// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>

namespace eccdb {

// Routing wire direction used by LEF layer clauses. This describes the routing
// wire orientation, not the direction of a via object.
enum class RoutingDirection : uint8_t
{
  kUnknown,
  kHorizontal,
  kVertical
};

}  // namespace eccdb
