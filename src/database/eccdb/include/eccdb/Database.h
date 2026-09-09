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

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "eccdb/Config.h"
#include "eccdb/DesignData.h"
#include "eccdb/Ref.h"
#include "eccdb/RoutingData.h"

namespace eccdb {

namespace detail {
class DatabaseState;
class DatabaseTestAccess;
}  // namespace detail

struct ImportDiagnostic
{
  std::string source;
  std::string statement;
  std::size_t occurrence_count = 0;
};

// Owning public API entry point. Its implementation owns the technology,
// library and physical-design stores. IDs identify database-owned entities;
// Data values are detached snapshots; Ref values are non-owning conveniences.
class Database
{
 public:
  [[nodiscard]] static Database open(const Config& config);
  [[nodiscard]] static bool supportsLefDef() noexcept;

  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;

  [[nodiscard]] const std::vector<ImportDiagnostic>& diagnostics() const noexcept;
  void writeDef(const std::filesystem::path& file) const;
  void writeBinary(const BinaryFiles& files) const;

  [[nodiscard]] NetId createNet(NetData value);
  [[nodiscard]] NetId createSpecialNet(NetData value);
  [[nodiscard]] bool contains(NetId id) const noexcept;
  [[nodiscard]] bool isSpecialNet(NetId id) const;
  [[nodiscard]] std::optional<NetData> netData(NetId id) const;
  [[nodiscard]] std::optional<NetOptions> netOptions(NetId id) const;
  [[nodiscard]] NetId findNetId(std::string_view name) const noexcept;
  [[nodiscard]] NetId findRegularNetId(std::string_view name) const noexcept;
  [[nodiscard]] NetId findSpecialNetId(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<NetId> netIds() const;
  [[nodiscard]] std::vector<NetId> regularNetIds() const;
  [[nodiscard]] std::vector<NetId> specialNetIds() const;
  [[nodiscard]] std::vector<InstancePinId> instancePinIds(NetId net) const;
  [[nodiscard]] std::vector<IoPinId> ioPinIds(NetId net) const;
  [[nodiscard]] std::vector<WireId> wireIds(NetId net) const;
  void updateNet(NetId id, NetData value);
  void setNetOptions(NetId id, NetOptions options);
  [[nodiscard]] bool destroyNet(NetId id);
  [[nodiscard]] NetRef net(NetId id) const noexcept;

  [[nodiscard]] InstanceId createInstance(InstanceData value);
  [[nodiscard]] bool contains(InstanceId id) const noexcept;
  [[nodiscard]] std::optional<InstanceData> instanceData(InstanceId id) const;
  [[nodiscard]] InstanceId findInstanceId(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<InstanceId> instanceIds() const;
  [[nodiscard]] std::vector<InstancePinId> instancePinIds(InstanceId instance) const;
  [[nodiscard]] InstancePinId findInstancePinId(InstanceId instance,
                                                std::string_view term_name) const noexcept;
  void updateInstance(InstanceId id, InstanceData value);
  [[nodiscard]] bool destroyInstance(InstanceId id);
  [[nodiscard]] InstanceRef instance(InstanceId id) const noexcept;

  [[nodiscard]] bool contains(InstancePinId id) const noexcept;
  [[nodiscard]] std::optional<InstancePinData> instancePinData(InstancePinId id) const;
  void connect(InstancePinId pin, NetId net);
  void disconnect(InstancePinId pin);
  void disconnect(InstancePinId pin, NetId net);
  [[nodiscard]] InstancePinRef instancePin(InstancePinId id) const noexcept;

  [[nodiscard]] IoPinId createIoPin(IoPinData value);
  [[nodiscard]] bool contains(IoPinId id) const noexcept;
  [[nodiscard]] std::optional<IoPinData> ioPinData(IoPinId id) const;
  [[nodiscard]] IoPinId findIoPinId(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<IoPinId> ioPinIds() const;
  // Net fields in an existing IoPinData snapshot are relationship state. Use
  // connect/disconnect to change them; updateIoPin changes the pin properties.
  void updateIoPin(IoPinId id, IoPinData value);
  void connect(IoPinId pin, NetId net);
  void disconnect(IoPinId pin);
  void disconnect(IoPinId pin, NetId net);
  [[nodiscard]] bool destroyIoPin(IoPinId id);
  [[nodiscard]] IoPinRef ioPin(IoPinId id) const noexcept;

  [[nodiscard]] WireId createWire(WireMetadata metadata, WireRoutingData routing);
  [[nodiscard]] bool contains(WireId id) const noexcept;
  [[nodiscard]] std::optional<WireMetadata> wireMetadata(WireId id) const;
  [[nodiscard]] std::optional<WireRoutingData> wireRoutingData(WireId id) const;
  [[nodiscard]] std::vector<WireId> wireIds() const;
  void updateWire(WireId id, WireMetadata metadata, WireRoutingData routing);
  [[nodiscard]] bool destroyWire(WireId id);
  [[nodiscard]] WireRef wire(WireId id) const noexcept;

  [[nodiscard]] ViaId createDesignVia(DesignViaData value);
  [[nodiscard]] bool contains(ViaId id) const noexcept;
  [[nodiscard]] std::optional<DesignViaData> designViaData(ViaId id) const;
  [[nodiscard]] ViaId findDesignViaId(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<ViaId> designViaIds() const;
  void updateDesignVia(ViaId id, DesignViaData value);
  [[nodiscard]] bool destroyDesignVia(ViaId id);
  [[nodiscard]] DesignViaRef designVia(ViaId id) const noexcept;

 private:
  friend class detail::DatabaseTestAccess;

  explicit Database(std::unique_ptr<detail::DatabaseState> state) noexcept;
  [[nodiscard]] detail::DatabaseState& state() const;

  std::unique_ptr<detail::DatabaseState> _state;
};

}  // namespace eccdb
