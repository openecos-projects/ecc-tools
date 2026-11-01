// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <vector>

#include "tech/common/AntennaRuleTypes.h"

namespace eccdb {

namespace TechLayerAntennaFlag {
constexpr uint32_t kHasModels = 1u << 0;
}  // namespace TechLayerAntennaFlag

// Optional component on the shared Tech layer entity. It is used by routing,
// cut, and legacy masterslice layers without duplicating component types.
struct TechLayerAntenna
{
  uint32_t flags = 0;
  std::vector<AntennaModel> models;
};

}  // namespace eccdb
