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
#include "eccdb/Database.h"

#include <stdexcept>
#include <utility>

#include "api/internal/StorageConversions.h"
#include "api/internal/DatabaseState.h"
#include "design/DesignStore.h"

namespace eccdb {

Database::Database(std::unique_ptr<detail::DatabaseState> state) noexcept : _state(std::move(state)) {}

Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

Database Database::open(const Config& config)
{
  return Database{detail::DatabaseState::open(config)};
}

bool Database::supportsLefDef() noexcept
{
#if ECCDB_HAS_LEF_DEF
  return true;
#else
  return false;
#endif
}

detail::DatabaseState& Database::state() const
{
  if (!_state) {
    throw std::logic_error("operation on a moved-from EccDB Database");
  }
  return *_state;
}

const std::vector<ImportDiagnostic>& Database::diagnostics() const noexcept
{
  static const std::vector<ImportDiagnostic> empty;
  return _state ? _state->diagnostics() : empty;
}

void Database::writeDef(const std::filesystem::path& file) const
{
  state().writeDef(file);
}

void Database::writeBinary(const BinaryFiles& files) const
{
  state().writeBinary(files);
}

NetId Database::createNet(NetData value)
{
  return detail::toApiId<NetId>(state().design().netlistStorage().createNet(detail::toStorage(value)));
}

NetId Database::createSpecialNet(NetData value)
{
  return detail::toApiId<NetId>(
      state().design().netlistStorage().createSpecialNet(detail::toStorage(value)));
}

bool Database::contains(NetId id) const noexcept
{
  return _state && _state->design().netlistStorage().contains(detail::toStorageId<DesignNetId>(id));
}

bool Database::isSpecialNet(NetId id) const
{
  return state().design().netlistStorage().isSpecialNet(detail::toStorageId<DesignNetId>(id));
}

std::optional<NetData> Database::netData(NetId id) const
{
  if (!contains(id)) return std::nullopt;
  return detail::toApi(state().design().netlistStorage().net(detail::toStorageId<DesignNetId>(id)));
}

std::optional<NetOptions> Database::netOptions(NetId id) const
{
  if (!contains(id)) return std::nullopt;
  const auto* options = state().design().netlistStorage().netOptions(detail::toStorageId<DesignNetId>(id));
  return options ? std::optional<NetOptions>{detail::toApi(*options)} : std::nullopt;
}

NetId Database::findNetId(std::string_view name) const noexcept
{
  return _state ? detail::toApiId<NetId>(_state->design().netlistStorage().findNet(name)) : NetId{};
}

NetId Database::findRegularNetId(std::string_view name) const noexcept
{
  return _state ? detail::toApiId<NetId>(_state->design().netlistStorage().findRegularNet(name))
                : NetId{};
}

NetId Database::findSpecialNetId(std::string_view name) const noexcept
{
  return _state ? detail::toApiId<NetId>(_state->design().netlistStorage().findSpecialNet(name))
                : NetId{};
}

std::vector<NetId> Database::netIds() const
{
  std::vector<NetId> result;
  for (const auto id : state().design().netlistStorage().nets()) {
    result.push_back(detail::toApiId<NetId>(id));
  }
  return result;
}

std::vector<NetId> Database::regularNetIds() const
{
  std::vector<NetId> result;
  for (const auto id : state().design().netlistStorage().regularNets()) {
    result.push_back(detail::toApiId<NetId>(id));
  }
  return result;
}

std::vector<NetId> Database::specialNetIds() const
{
  std::vector<NetId> result;
  for (const auto id : state().design().netlistStorage().specialNets()) {
    result.push_back(detail::toApiId<NetId>(id));
  }
  return result;
}

std::vector<InstancePinId> Database::instancePinIds(NetId net) const
{
  std::vector<InstancePinId> result;
  for (const auto id : state().design().netlistStorage().instancePins(detail::toStorageId<DesignNetId>(net))) {
    result.push_back(detail::toApiId<InstancePinId>(id));
  }
  return result;
}

std::vector<IoPinId> Database::ioPinIds(NetId net) const
{
  std::vector<IoPinId> result;
  for (const auto id : state().design().netlistStorage().ioPins(detail::toStorageId<DesignNetId>(net))) {
    result.push_back(detail::toApiId<IoPinId>(id));
  }
  return result;
}

std::vector<WireId> Database::wireIds(NetId net) const
{
  std::vector<WireId> result;
  for (const auto id : state().design().routingStorage().wireIds(detail::toStorageId<DesignNetId>(net))) {
    result.push_back(detail::toApiId<WireId>(id));
  }
  return result;
}

void Database::updateNet(NetId id, NetData value)
{
  state().design().netlistStorage().updateNet(detail::toStorageId<DesignNetId>(id),
                                               detail::toStorage(value));
}

void Database::setNetOptions(NetId id, NetOptions options)
{
  state().design().netlistStorage().setNetOptions(detail::toStorageId<DesignNetId>(id),
                                                   detail::toStorage(options));
}

bool Database::destroyNet(NetId id)
{
  return state().design().netlistStorage().destroyNet(detail::toStorageId<DesignNetId>(id));
}

NetRef Database::net(NetId id) const noexcept
{
  return contains(id) ? NetRef{*_state, id} : NetRef{};
}

InstanceId Database::createInstance(InstanceData value)
{
  return detail::toApiId<InstanceId>(
      state().design().netlistStorage().createInstance(detail::toStorage(value)));
}

bool Database::contains(InstanceId id) const noexcept
{
  return _state
         && _state->design().netlistStorage().contains(detail::toStorageId<DesignInstanceId>(id));
}

std::optional<InstanceData> Database::instanceData(InstanceId id) const
{
  if (!contains(id)) return std::nullopt;
  return detail::toApi(
      state().design().netlistStorage().instance(detail::toStorageId<DesignInstanceId>(id)));
}

InstanceId Database::findInstanceId(std::string_view name) const noexcept
{
  return _state
             ? detail::toApiId<InstanceId>(_state->design().netlistStorage().findInstance(name))
             : InstanceId{};
}

std::vector<InstanceId> Database::instanceIds() const
{
  std::vector<InstanceId> result;
  for (const auto id : state().design().netlistStorage().instances()) {
    result.push_back(detail::toApiId<InstanceId>(id));
  }
  return result;
}

std::vector<InstancePinId> Database::instancePinIds(InstanceId instance) const
{
  std::vector<InstancePinId> result;
  for (const auto id : state().design().netlistStorage().instancePins(
           detail::toStorageId<DesignInstanceId>(instance))) {
    result.push_back(detail::toApiId<InstancePinId>(id));
  }
  return result;
}

InstancePinId Database::findInstancePinId(InstanceId instance, std::string_view term_name) const noexcept
{
  if (!_state) return {};
  return detail::toApiId<InstancePinId>(_state->design().netlistStorage().findInstancePin(
      detail::toStorageId<DesignInstanceId>(instance), term_name));
}

void Database::updateInstance(InstanceId id, InstanceData value)
{
  state().design().netlistStorage().updateInstance(detail::toStorageId<DesignInstanceId>(id),
                                                    detail::toStorage(value));
}

bool Database::destroyInstance(InstanceId id)
{
  return state().design().netlistStorage().destroyInstance(detail::toStorageId<DesignInstanceId>(id));
}

InstanceRef Database::instance(InstanceId id) const noexcept
{
  return contains(id) ? InstanceRef{*_state, id} : InstanceRef{};
}

bool Database::contains(InstancePinId id) const noexcept
{
  return _state
         && _state->design().netlistStorage().contains(detail::toStorageId<DesignInstancePinId>(id));
}

std::optional<InstancePinData> Database::instancePinData(InstancePinId id) const
{
  if (!contains(id)) return std::nullopt;
  return detail::toApi(
      state().design().netlistStorage().instancePin(detail::toStorageId<DesignInstancePinId>(id)));
}

void Database::connect(InstancePinId pin, NetId net)
{
  state().design().netlistStorage().connect(detail::toStorageId<DesignInstancePinId>(pin),
                                             detail::toStorageId<DesignNetId>(net));
}

void Database::disconnect(InstancePinId pin)
{
  state().design().netlistStorage().disconnect(detail::toStorageId<DesignInstancePinId>(pin));
}

void Database::disconnect(InstancePinId pin, NetId net)
{
  state().design().netlistStorage().disconnect(detail::toStorageId<DesignInstancePinId>(pin),
                                                detail::toStorageId<DesignNetId>(net));
}

InstancePinRef Database::instancePin(InstancePinId id) const noexcept
{
  return contains(id) ? InstancePinRef{*_state, id} : InstancePinRef{};
}

IoPinId Database::createIoPin(IoPinData value)
{
  auto& storage = state().design().netlistStorage();
  const auto regular_net = value.net;
  const auto special_net = value.special_net;
  value.net = {};
  value.special_net = {};
  const auto id = storage.createIoPin(detail::toStorage(value));
  try {
    if (regular_net) storage.connect(id, detail::toStorageId<DesignNetId>(regular_net));
    if (special_net) storage.connect(id, detail::toStorageId<DesignNetId>(special_net));
  } catch (...) {
    storage.disconnect(id);
    static_cast<void>(storage.destroyIoPin(id));
    throw;
  }
  return detail::toApiId<IoPinId>(id);
}

bool Database::contains(IoPinId id) const noexcept
{
  return _state && _state->design().netlistStorage().contains(detail::toStorageId<DesignIoPinId>(id));
}

std::optional<IoPinData> Database::ioPinData(IoPinId id) const
{
  if (!contains(id)) return std::nullopt;
  return detail::toApi(state().design().netlistStorage().ioPin(detail::toStorageId<DesignIoPinId>(id)));
}

IoPinId Database::findIoPinId(std::string_view name) const noexcept
{
  return _state ? detail::toApiId<IoPinId>(_state->design().netlistStorage().findIoPin(name))
                : IoPinId{};
}

std::vector<IoPinId> Database::ioPinIds() const
{
  std::vector<IoPinId> result;
  for (const auto id : state().design().netlistStorage().ioPins()) {
    result.push_back(detail::toApiId<IoPinId>(id));
  }
  return result;
}

void Database::updateIoPin(IoPinId id, IoPinData value)
{
  state().design().netlistStorage().updateIoPin(detail::toStorageId<DesignIoPinId>(id),
                                                detail::toStorage(value));
}

void Database::connect(IoPinId pin, NetId net)
{
  state().design().netlistStorage().connect(detail::toStorageId<DesignIoPinId>(pin),
                                             detail::toStorageId<DesignNetId>(net));
}

void Database::disconnect(IoPinId pin)
{
  state().design().netlistStorage().disconnect(detail::toStorageId<DesignIoPinId>(pin));
}

void Database::disconnect(IoPinId pin, NetId net)
{
  state().design().netlistStorage().disconnect(detail::toStorageId<DesignIoPinId>(pin),
                                                detail::toStorageId<DesignNetId>(net));
}

bool Database::destroyIoPin(IoPinId id)
{
  return state().design().netlistStorage().destroyIoPin(detail::toStorageId<DesignIoPinId>(id));
}

IoPinRef Database::ioPin(IoPinId id) const noexcept
{
  return contains(id) ? IoPinRef{*_state, id} : IoPinRef{};
}

WireId Database::createWire(WireMetadata metadata, WireRoutingData routing)
{
  return detail::toApiId<WireId>(state().design().routingStorage().createWire(
      detail::toStorage(metadata), detail::toStorage(routing)));
}

bool Database::contains(WireId id) const noexcept
{
  return _state && _state->design().routingStorage().contains(detail::toStorageId<DesignWireId>(id));
}

std::optional<WireMetadata> Database::wireMetadata(WireId id) const
{
  if (!contains(id)) return std::nullopt;
  return detail::toApi(state().design().routingStorage().wire(detail::toStorageId<DesignWireId>(id)));
}

std::optional<WireRoutingData> Database::wireRoutingData(WireId id) const
{
  if (!contains(id)) return std::nullopt;
  auto& storage = state().design().routingStorage();
  const auto storage_id = detail::toStorageId<DesignWireId>(id);
  WireRoutingData result;
  result.paths.reserve(storage.pathCount(storage_id));
  storage.forEachPath(storage_id, [&result](const DesignWirePathView path) {
    result.paths.push_back(detail::toApi(path));
  });
  return result;
}

std::vector<WireId> Database::wireIds() const
{
  std::vector<WireId> result;
  for (const auto id : state().design().routingStorage().wires()) {
    result.push_back(detail::toApiId<WireId>(id));
  }
  return result;
}

void Database::updateWire(WireId id, WireMetadata metadata, WireRoutingData routing)
{
  state().design().routingStorage().updateWire(detail::toStorageId<DesignWireId>(id),
                                                detail::toStorage(metadata),
                                                detail::toStorage(routing));
}

bool Database::destroyWire(WireId id)
{
  return state().design().routingStorage().destroyWire(detail::toStorageId<DesignWireId>(id));
}

WireRef Database::wire(WireId id) const noexcept
{
  return contains(id) ? WireRef{*_state, id} : WireRef{};
}

ViaId Database::createDesignVia(DesignViaData value)
{
  return detail::toApiId<ViaId>(
      state().design().routingStorage().createVia(detail::toStorage(value)));
}

bool Database::contains(ViaId id) const noexcept
{
  return _state && _state->design().routingStorage().contains(detail::toStorageId<DesignViaId>(id));
}

std::optional<DesignViaData> Database::designViaData(ViaId id) const
{
  if (!contains(id)) return std::nullopt;
  return detail::toApi(state().design().routingStorage().via(detail::toStorageId<DesignViaId>(id)));
}

ViaId Database::findDesignViaId(std::string_view name) const noexcept
{
  return _state ? detail::toApiId<ViaId>(_state->design().routingStorage().findVia(name)) : ViaId{};
}

std::vector<ViaId> Database::designViaIds() const
{
  std::vector<ViaId> result;
  for (const auto id : state().design().routingStorage().vias()) {
    result.push_back(detail::toApiId<ViaId>(id));
  }
  return result;
}

void Database::updateDesignVia(ViaId id, DesignViaData value)
{
  state().design().routingStorage().updateVia(detail::toStorageId<DesignViaId>(id),
                                               detail::toStorage(value));
}

bool Database::destroyDesignVia(ViaId id)
{
  return state().design().routingStorage().destroyVia(detail::toStorageId<DesignViaId>(id));
}

DesignViaRef Database::designVia(ViaId id) const noexcept
{
  return contains(id) ? DesignViaRef{*_state, id} : DesignViaRef{};
}

}  // namespace eccdb
