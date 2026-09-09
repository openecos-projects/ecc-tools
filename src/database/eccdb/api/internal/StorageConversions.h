// ***************************************************************************************
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

#include <type_traits>

#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/pool/WireRoutingInput.h"
#include "design/via/component/ViaComponents.h"
#include "eccdb/DesignData.h"
#include "eccdb/RoutingData.h"

namespace eccdb::detail {

template <typename ApiId, typename StorageId>
[[nodiscard]] ApiId toApiId(StorageId id) noexcept
{
  return id ? ApiId{static_cast<uint64_t>(id.packed())} : ApiId{};
}

template <typename StorageId, typename ApiId>
[[nodiscard]] StorageId toStorageId(ApiId id) noexcept
{
  if (!id) {
    return {};
  }
  using Entity = std::remove_cvref_t<decltype(StorageId{}.entity())>;
  return StorageId{static_cast<Entity>(id.packed())};
}

[[nodiscard]] DesignNet toStorage(const NetData& value);
void applyToStorage(const NetData& value, DesignNet& target);
[[nodiscard]] NetData toApi(const DesignNet& value);
[[nodiscard]] DesignNetOptions toStorage(const NetOptions& value);
[[nodiscard]] NetOptions toApi(const DesignNetOptions& value);

[[nodiscard]] DesignInstance toStorage(const InstanceData& value);
void applyToStorage(const InstanceData& value, DesignInstance& target);
[[nodiscard]] InstanceData toApi(const DesignInstance& value);
[[nodiscard]] InstancePinData toApi(const DesignInstancePin& value);
[[nodiscard]] DesignIoPin toStorage(const IoPinData& value);
void applyToStorage(const IoPinData& value, DesignIoPin& target);
[[nodiscard]] IoPinData toApi(const DesignIoPin& value);

[[nodiscard]] DesignVia toStorage(const DesignViaData& value);
[[nodiscard]] DesignViaData toApi(const DesignVia& value);

[[nodiscard]] DesignWire toStorage(const WireMetadata& value);
[[nodiscard]] WireMetadata toApi(const DesignWire& value);
[[nodiscard]] DesignWireRoutingInput toStorage(const WireRoutingData& value);
[[nodiscard]] WirePathData toApi(const DesignWirePathView& value);

}  // namespace eccdb::detail
