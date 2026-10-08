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

#include "tech/overlap_layer/storage/OverlapLayerStorage.h"

#include <stdexcept>
#include <utility>

namespace eccdb {

TechOverlapLayerId TechOverlapLayerStorage::createLayer(TechLayerInfo info)
{
  validateInfo(info);
  const auto entity = _registry.create();
  try {
    _registry.emplace<TechLayerInfo>(entity, std::move(info));
    _registry.emplace<TechOverlapLayer>(entity);
    return TechOverlapLayerId{entity};
  } catch (...) {
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
}

bool TechOverlapLayerStorage::contains(TechOverlapLayerId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechLayerInfo, TechOverlapLayer>(id.entity());
}

TechOverlapLayerId TechOverlapLayerStorage::findLayerById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechLayerInfo, TechOverlapLayer>(entity) ? TechOverlapLayerId{entity}
                                                                                              : TechOverlapLayerId{};
}

TechLayerInfo& TechOverlapLayerStorage::layerInfo(TechOverlapLayerId id)
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

const TechLayerInfo& TechOverlapLayerStorage::layerInfo(TechOverlapLayerId id) const
{
  ensureLayer(id);
  return _registry.get<TechLayerInfo>(id.entity());
}

void TechOverlapLayerStorage::ensureLayer(TechOverlapLayerId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech overlap layer id");
  }
}

void TechOverlapLayerStorage::validateInfo(const TechLayerInfo& info) const
{
  if (info.name.empty()) {
    throw std::invalid_argument("overlap layer name is required");
  }
  if (info.mask_count == 0) {
    throw std::invalid_argument("overlap layer mask count must be positive");
  }
}

}  // namespace eccdb
