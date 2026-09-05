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

NetRef::operator bool() const noexcept
{
  return _state != nullptr
         && _state->design().netlistStorage().contains(detail::toStorageId<DesignNetId>(_id));
}

detail::DatabaseState& NetRef::state() const
{
  if (!*this) {
    throw std::out_of_range("invalid NetRef handle");
  }
  return *_state;
}

NetData NetRef::data() const
{
  return detail::toApi(
      state().design().netlistStorage().net(detail::toStorageId<DesignNetId>(_id)));
}

std::string_view NetRef::name() const
{
  return state().design().netlistStorage().net(detail::toStorageId<DesignNetId>(_id)).name;
}

bool NetRef::isSpecial() const
{
  return state().design().netlistStorage().isSpecialNet(detail::toStorageId<DesignNetId>(_id));
}

std::optional<NetOptions> NetRef::options() const
{
  const auto* value = state().design().netlistStorage().netOptions(
      detail::toStorageId<DesignNetId>(_id));
  return value ? std::optional<NetOptions>{detail::toApi(*value)} : std::nullopt;
}

std::vector<InstancePinRef> NetRef::instancePins() const
{
  auto& api = state();
  std::vector<InstancePinRef> result;
  for (const auto id : api.design().netlistStorage().instancePins(
           detail::toStorageId<DesignNetId>(_id))) {
    result.push_back(InstancePinRef{api, detail::toApiId<InstancePinId>(id)});
  }
  return result;
}

std::vector<IoPinRef> NetRef::ioPins() const
{
  auto& api = state();
  std::vector<IoPinRef> result;
  for (const auto id : api.design().netlistStorage().ioPins(detail::toStorageId<DesignNetId>(_id))) {
    result.push_back(IoPinRef{api, detail::toApiId<IoPinId>(id)});
  }
  return result;
}

std::vector<WireRef> NetRef::wires() const
{
  auto& api = state();
  std::vector<WireRef> result;
  for (const auto id : api.design().routingStorage().wireIds(detail::toStorageId<DesignNetId>(_id))) {
    result.push_back(WireRef{api, detail::toApiId<WireId>(id)});
  }
  return result;
}

void NetRef::rename(std::string name)
{
  auto value = data();
  value.name = std::move(name);
  update(std::move(value));
}

void NetRef::setUse(SignalUse use)
{
  auto value = data();
  value.use = use;
  update(std::move(value));
}

void NetRef::setSource(NetSource source)
{
  auto value = data();
  value.source = source;
  update(std::move(value));
}

void NetRef::setWeight(int32_t weight)
{
  auto value = data();
  value.weight = weight;
  update(std::move(value));
}

void NetRef::clearWeight()
{
  auto value = data();
  value.weight.reset();
  update(std::move(value));
}

void NetRef::setTechNonDefaultRule(TechRuleId rule)
{
  auto value = data();
  value.tech_non_default_rule = rule;
  value.design_non_default_rule = {};
  update(std::move(value));
}

void NetRef::setDesignNonDefaultRule(DesignRuleId rule)
{
  auto value = data();
  value.tech_non_default_rule = {};
  value.design_non_default_rule = rule;
  update(std::move(value));
}

void NetRef::clearNonDefaultRule()
{
  auto value = data();
  value.tech_non_default_rule = {};
  value.design_non_default_rule = {};
  update(std::move(value));
}

void NetRef::setOptions(NetOptions options)
{
  state().design().netlistStorage().setNetOptions(detail::toStorageId<DesignNetId>(_id),
                                                   detail::toStorage(options));
}

void NetRef::update(NetData value)
{
  state().design().netlistStorage().updateNet(detail::toStorageId<DesignNetId>(_id),
                                               detail::toStorage(value));
}

WireRef NetRef::createWire(WireRoutingData routing, WireStatus status, std::string shield_net)
{
  auto& api = state();
  const auto id = api.design().routingStorage().createWire(
      detail::toStorage(WireMetadata{.net = _id, .status = status, .shield_net = std::move(shield_net)}),
      detail::toStorage(routing));
  return WireRef{api, detail::toApiId<WireId>(id)};
}

bool NetRef::destroy()
{
  if (!*this
      || !_state->design().netlistStorage().destroyNet(detail::toStorageId<DesignNetId>(_id))) {
    return false;
  }
  _state = nullptr;
  _id = {};
  return true;
}

}  // namespace eccdb
