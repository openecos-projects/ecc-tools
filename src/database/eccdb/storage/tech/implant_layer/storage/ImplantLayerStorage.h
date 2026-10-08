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
#include <vector>

#include "tech/TechRegistry.h"
#include "tech/implant_layer/model/ImplantLayerComponents.h"

namespace eccdb {

// Storage facade for LEF IMPLANT layers. Each layer is one entity; repeated
// SPACING clauses are value records in TechImplantSpacingRules on that entity.
class TechImplantLayerStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit TechImplantLayerStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] TechImplantLayerId createLayer(TechLayerInfo info, TechImplantLayer implant = {});
  [[nodiscard]] bool contains(TechImplantLayerId id) const;
  [[nodiscard]] TechImplantLayerId findLayerById(uint32_t id) const;

  [[nodiscard]] TechLayerInfo& layerInfo(TechImplantLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechImplantLayerId id) const;
  [[nodiscard]] TechImplantLayer& implantLayer(TechImplantLayerId id);
  [[nodiscard]] const TechImplantLayer& implantLayer(TechImplantLayerId id) const;

  void addSpacingRule(TechImplantLayerId owner, TechImplantSpacingRule rule);
  [[nodiscard]] const std::vector<TechImplantSpacingRule>& spacingRules(TechImplantLayerId owner) const;

 private:
  void ensureLayer(TechImplantLayerId id) const;
  void validateInfo(const TechLayerInfo& info) const;
  void validateLayer(const TechImplantLayer& implant) const;
  void validateSpacingRule(TechImplantLayerId owner, const TechImplantSpacingRule& rule) const;

  registry_type& _registry;
};

}  // namespace eccdb
