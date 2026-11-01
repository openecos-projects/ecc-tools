// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include "common/EnttId.h"
#include "tech/common/TechEntity.h"

namespace eccdb {

struct TechRoot;
struct TechLayerInfo;
struct TechRoutingLayer;
struct TechCutLayer;
struct TechImplantLayer;
struct TechMastersliceLayer;
struct TechOverlapLayer;

using TechRootId = EnttId<TechEntity, TechRoot>;
using TechLayerId = EnttId<TechEntity, TechLayerInfo>;
using TechRoutingLayerId = EnttId<TechEntity, TechRoutingLayer>;
using TechCutLayerId = EnttId<TechEntity, TechCutLayer>;
using TechImplantLayerId = EnttId<TechEntity, TechImplantLayer>;
using TechMastersliceLayerId = EnttId<TechEntity, TechMastersliceLayer>;
using TechOverlapLayerId = EnttId<TechEntity, TechOverlapLayer>;

}  // namespace eccdb
