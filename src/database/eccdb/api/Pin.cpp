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

InstancePinRef::operator bool() const noexcept
{
  return _state != nullptr
         && _state->design().netlistStorage().contains(
             detail::toStorageId<DesignInstancePinId>(_id));
}

detail::DatabaseState& InstancePinRef::state() const
{
  if (!*this) {
    throw std::out_of_range("invalid InstancePinRef handle");
  }
  return *_state;
}

InstancePinData InstancePinRef::data() const
{
  return detail::toApi(state().design().netlistStorage().instancePin(
      detail::toStorageId<DesignInstancePinId>(_id)));
}

InstanceRef InstancePinRef::instance() const
{
  auto& api = state();
  return InstanceRef{api, data().instance};
}

NetRef InstancePinRef::net() const noexcept
{
  if (!*this) return {};
  const auto id = _state->design().netlistStorage().instancePin(
      detail::toStorageId<DesignInstancePinId>(_id)).net;
  return id ? NetRef{*_state, detail::toApiId<NetId>(id)} : NetRef{};
}

NetRef InstancePinRef::specialNet() const noexcept
{
  if (!*this) return {};
  const auto id = _state->design().netlistStorage().instancePin(
      detail::toStorageId<DesignInstancePinId>(_id)).special_net;
  return id ? NetRef{*_state, detail::toApiId<NetId>(id)} : NetRef{};
}

void InstancePinRef::connect(NetRef net)
{
  auto& api = state();
  if (!net || net._state != &api) {
    throw std::invalid_argument("pin and net belong to different databases");
  }
  api.design().netlistStorage().connect(detail::toStorageId<DesignInstancePinId>(_id),
                                         detail::toStorageId<DesignNetId>(net._id));
}

void InstancePinRef::disconnect()
{
  state().design().netlistStorage().disconnect(detail::toStorageId<DesignInstancePinId>(_id));
}

void InstancePinRef::disconnect(NetRef net)
{
  auto& api = state();
  if (!net || net._state != &api) {
    throw std::invalid_argument("pin and net belong to different databases");
  }
  api.design().netlistStorage().disconnect(detail::toStorageId<DesignInstancePinId>(_id),
                                            detail::toStorageId<DesignNetId>(net._id));
}

IoPinRef::operator bool() const noexcept
{
  return _state != nullptr
         && _state->design().netlistStorage().contains(detail::toStorageId<DesignIoPinId>(_id));
}

detail::DatabaseState& IoPinRef::state() const
{
  if (!*this) {
    throw std::out_of_range("invalid IoPinRef handle");
  }
  return *_state;
}

IoPinData IoPinRef::data() const
{
  return detail::toApi(
      state().design().netlistStorage().ioPin(detail::toStorageId<DesignIoPinId>(_id)));
}

std::string_view IoPinRef::name() const
{
  return state().design().netlistStorage().ioPin(detail::toStorageId<DesignIoPinId>(_id)).name;
}

NetRef IoPinRef::net() const noexcept
{
  if (!*this) return {};
  const auto id = _state->design().netlistStorage().ioPin(
      detail::toStorageId<DesignIoPinId>(_id)).net;
  return id ? NetRef{*_state, detail::toApiId<NetId>(id)} : NetRef{};
}

NetRef IoPinRef::specialNet() const noexcept
{
  if (!*this) return {};
  const auto id = _state->design().netlistStorage().ioPin(
      detail::toStorageId<DesignIoPinId>(_id)).special_net;
  return id ? NetRef{*_state, detail::toApiId<NetId>(id)} : NetRef{};
}

void IoPinRef::rename(std::string name)
{
  auto value = data();
  value.name = std::move(name);
  update(std::move(value));
}

void IoPinRef::setDirection(IoDirection direction)
{
  auto value = data();
  value.direction = direction;
  update(std::move(value));
}

void IoPinRef::setUse(SignalUse use)
{
  auto value = data();
  value.use = use;
  update(std::move(value));
}

void IoPinRef::update(IoPinData value)
{
  state().design().netlistStorage().updateIoPin(detail::toStorageId<DesignIoPinId>(_id),
                                                detail::toStorage(value));
}

void IoPinRef::connect(NetRef net)
{
  auto& api = state();
  if (!net || net._state != &api) {
    throw std::invalid_argument("pin and net belong to different databases");
  }
  api.design().netlistStorage().connect(detail::toStorageId<DesignIoPinId>(_id),
                                         detail::toStorageId<DesignNetId>(net._id));
}

void IoPinRef::disconnect()
{
  state().design().netlistStorage().disconnect(detail::toStorageId<DesignIoPinId>(_id));
}

void IoPinRef::disconnect(NetRef net)
{
  auto& api = state();
  if (!net || net._state != &api) {
    throw std::invalid_argument("pin and net belong to different databases");
  }
  api.design().netlistStorage().disconnect(detail::toStorageId<DesignIoPinId>(_id),
                                            detail::toStorageId<DesignNetId>(net._id));
}

bool IoPinRef::destroy()
{
  if (!*this
      || !_state->design().netlistStorage().destroyIoPin(detail::toStorageId<DesignIoPinId>(_id))) {
    return false;
  }
  _state = nullptr;
  _id = {};
  return true;
}

}  // namespace eccdb
