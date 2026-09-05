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
#include <utility>

#include "api/internal/StorageConversions.h"
#include "api/internal/DatabaseState.h"
#include "design/DesignStore.h"

namespace eccdb {

InstanceRef::operator bool() const noexcept
{
  return _state != nullptr
         && _state->design().netlistStorage().contains(detail::toStorageId<DesignInstanceId>(_id));
}

detail::DatabaseState& InstanceRef::state() const
{
  if (!*this) {
    throw std::out_of_range("invalid InstanceRef handle");
  }
  return *_state;
}

InstanceData InstanceRef::data() const
{
  return detail::toApi(
      state().design().netlistStorage().instance(detail::toStorageId<DesignInstanceId>(_id)));
}

std::string_view InstanceRef::name() const
{
  return state().design().netlistStorage().instance(detail::toStorageId<DesignInstanceId>(_id)).name;
}

std::vector<InstancePinRef> InstanceRef::pins() const
{
  auto& api = state();
  std::vector<InstancePinRef> result;
  for (const auto id : api.design().netlistStorage().instancePins(
           detail::toStorageId<DesignInstanceId>(_id))) {
    result.push_back(InstancePinRef{api, detail::toApiId<InstancePinId>(id)});
  }
  return result;
}

InstancePinRef InstanceRef::findPin(std::string_view term_name) const
{
  auto& api = state();
  const auto id = api.design().netlistStorage().findInstancePin(
      detail::toStorageId<DesignInstanceId>(_id), term_name);
  return id ? InstancePinRef{api, detail::toApiId<InstancePinId>(id)} : InstancePinRef{};
}

void InstanceRef::rename(std::string name)
{
  auto value = data();
  value.name = std::move(name);
  update(std::move(value));
}

void InstanceRef::setOrigin(Point origin)
{
  auto value = data();
  value.origin = origin;
  update(std::move(value));
}

void InstanceRef::setOrientation(Orientation orientation)
{
  auto value = data();
  value.orientation = orientation;
  update(std::move(value));
}

void InstanceRef::setPlacementStatus(PlacementStatus status)
{
  auto value = data();
  value.placement_status = status;
  update(std::move(value));
}

void InstanceRef::setSource(InstanceSource source)
{
  auto value = data();
  value.source = source;
  update(std::move(value));
}

void InstanceRef::update(InstanceData value)
{
  state().design().netlistStorage().updateInstance(detail::toStorageId<DesignInstanceId>(_id),
                                                    detail::toStorage(value));
}

bool InstanceRef::destroy()
{
  if (!*this
      || !_state->design().netlistStorage().destroyInstance(detail::toStorageId<DesignInstanceId>(_id))) {
    return false;
  }
  _state = nullptr;
  _id = {};
  return true;
}

}  // namespace eccdb
