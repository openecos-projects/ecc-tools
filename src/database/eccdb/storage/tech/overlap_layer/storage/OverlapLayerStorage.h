// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include <cstdint>

#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/overlap_layer/model/OverlapLayerComponents.h"

namespace eccdb {

class TechOverlapLayerStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit TechOverlapLayerStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] TechOverlapLayerId createLayer(TechLayerInfo info);
  [[nodiscard]] bool contains(TechOverlapLayerId id) const;
  [[nodiscard]] TechOverlapLayerId findLayerById(uint32_t id) const;
  [[nodiscard]] TechLayerInfo& layerInfo(TechOverlapLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechOverlapLayerId id) const;

 private:
  void ensureLayer(TechOverlapLayerId id) const;
  void validateInfo(const TechLayerInfo& info) const;

  registry_type& _registry;
};

}  // namespace eccdb
