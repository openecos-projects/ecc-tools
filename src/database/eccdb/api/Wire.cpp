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
#include "eccdb/Ref.h"

#include <stdexcept>

#include "api/internal/StorageConversions.h"
#include "api/internal/DatabaseState.h"
#include "design/DesignStore.h"

namespace eccdb {

WireRef::operator bool() const noexcept
{
  return _state != nullptr
         && _state->design().routingStorage().contains(detail::toStorageId<DesignWireId>(_id));
}

detail::DatabaseState& WireRef::state() const
{
  if (!*this) {
    throw std::out_of_range("invalid WireRef handle");
  }
  return *_state;
}

WireMetadata WireRef::metadata() const
{
  return detail::toApi(
      state().design().routingStorage().wire(detail::toStorageId<DesignWireId>(_id)));
}

bool WireRef::destroy()
{
  if (!*this
      || !_state->design().routingStorage().destroyWire(detail::toStorageId<DesignWireId>(_id))) {
    return false;
  }
  _state = nullptr;
  _id = {};
  return true;
}

}  // namespace eccdb
