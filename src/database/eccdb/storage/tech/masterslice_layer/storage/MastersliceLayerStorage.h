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
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"

namespace eccdb {

class TechMastersliceLayerStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  explicit TechMastersliceLayerStorage(TechRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] TechMastersliceLayerId createLayer(TechLayerInfo info, TechMastersliceLayer masterslice = {});
  [[nodiscard]] bool contains(TechMastersliceLayerId id) const;
  [[nodiscard]] TechMastersliceLayerId findLayerById(uint32_t id) const;

  [[nodiscard]] TechLayerInfo& layerInfo(TechMastersliceLayerId id);
  [[nodiscard]] const TechLayerInfo& layerInfo(TechMastersliceLayerId id) const;
  [[nodiscard]] TechMastersliceLayer& mastersliceLayer(TechMastersliceLayerId id);
  [[nodiscard]] const TechMastersliceLayer& mastersliceLayer(TechMastersliceLayerId id) const;

  void setTrimmedMetalRule(TechMastersliceLayerId owner, TechTrimmedMetalRule rule);
  void clearTrimmedMetalRule(TechMastersliceLayerId owner);
  [[nodiscard]] bool hasTrimmedMetalRule(TechMastersliceLayerId owner) const;
  [[nodiscard]] const TechTrimmedMetalRule& trimmedMetalRule(TechMastersliceLayerId owner) const;

 private:
  void ensureLayer(TechMastersliceLayerId id) const;
  void validateInfo(const TechLayerInfo& info) const;
  void validateTrimmedMetalRule(TechMastersliceLayerId owner, const TechTrimmedMetalRule& rule) const;

  registry_type& _registry;
};

}  // namespace eccdb
