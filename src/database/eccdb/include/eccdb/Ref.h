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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "eccdb/DesignData.h"
#include "eccdb/RoutingData.h"

namespace eccdb {

class Database;
class InstancePinRef;
class InstanceRef;
class IoPinRef;
class NetRef;
class WireRef;
namespace detail {
class DatabaseState;
}

// Ref types are small, non-owning C++ convenience handles. They may be copied,
// but must not outlive their Database. Returned string_views are short-lived
// borrows and can be invalidated by a mutation of the corresponding object.
class NetRef
{
 public:
  NetRef() noexcept = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] NetId id() const noexcept { return _id; }
  [[nodiscard]] NetData data() const;
  [[nodiscard]] std::string_view name() const;
  [[nodiscard]] bool isSpecial() const;
  [[nodiscard]] std::optional<NetOptions> options() const;
  [[nodiscard]] std::vector<InstancePinRef> instancePins() const;
  [[nodiscard]] std::vector<IoPinRef> ioPins() const;
  [[nodiscard]] std::vector<WireRef> wires() const;

  void rename(std::string name);
  void setUse(SignalUse use);
  void setSource(NetSource source);
  void setWeight(int32_t weight);
  void clearWeight();
  void setTechNonDefaultRule(TechRuleId rule);
  void setDesignNonDefaultRule(DesignRuleId rule);
  void clearNonDefaultRule();
  void setOptions(NetOptions options);
  void update(NetData value);
  [[nodiscard]] WireRef createWire(WireRoutingData routing,
                                   WireStatus status = WireStatus::kRouted,
                                   std::string shield_net = {});
  [[nodiscard]] bool destroy();

 private:
  friend class Database;
  friend class InstancePinRef;
  friend class IoPinRef;
  friend class WireRef;

  NetRef(detail::DatabaseState& state, NetId id) noexcept : _state(&state), _id(id) {}
  [[nodiscard]] detail::DatabaseState& state() const;

  detail::DatabaseState* _state = nullptr;
  NetId _id;
};

class InstanceRef
{
 public:
  InstanceRef() noexcept = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] InstanceId id() const noexcept { return _id; }
  [[nodiscard]] InstanceData data() const;
  [[nodiscard]] std::string_view name() const;
  [[nodiscard]] std::vector<InstancePinRef> pins() const;
  [[nodiscard]] InstancePinRef findPin(std::string_view term_name) const;

  void rename(std::string name);
  void setOrigin(Point origin);
  void setOrientation(Orientation orientation);
  void setPlacementStatus(PlacementStatus status);
  void setSource(InstanceSource source);
  void update(InstanceData value);
  [[nodiscard]] bool destroy();

 private:
  friend class Database;
  friend class InstancePinRef;

  InstanceRef(detail::DatabaseState& state, InstanceId id) noexcept : _state(&state), _id(id) {}
  [[nodiscard]] detail::DatabaseState& state() const;

  detail::DatabaseState* _state = nullptr;
  InstanceId _id;
};

class InstancePinRef
{
 public:
  InstancePinRef() noexcept = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] InstancePinId id() const noexcept { return _id; }
  [[nodiscard]] InstancePinData data() const;
  [[nodiscard]] InstanceRef instance() const;
  [[nodiscard]] NetRef net() const noexcept;
  [[nodiscard]] NetRef specialNet() const noexcept;

  void connect(NetRef net);
  void disconnect();
  void disconnect(NetRef net);

 private:
  friend class Database;
  friend class InstanceRef;
  friend class NetRef;

  InstancePinRef(detail::DatabaseState& state, InstancePinId id) noexcept : _state(&state), _id(id) {}
  [[nodiscard]] detail::DatabaseState& state() const;

  detail::DatabaseState* _state = nullptr;
  InstancePinId _id;
};

class IoPinRef
{
 public:
  IoPinRef() noexcept = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] IoPinId id() const noexcept { return _id; }
  [[nodiscard]] IoPinData data() const;
  [[nodiscard]] std::string_view name() const;
  [[nodiscard]] NetRef net() const noexcept;
  [[nodiscard]] NetRef specialNet() const noexcept;

  void rename(std::string name);
  void setDirection(IoDirection direction);
  void setUse(SignalUse use);
  void update(IoPinData value);
  void connect(NetRef net);
  void disconnect();
  void disconnect(NetRef net);
  [[nodiscard]] bool destroy();

 private:
  friend class Database;
  friend class NetRef;

  IoPinRef(detail::DatabaseState& state, IoPinId id) noexcept : _state(&state), _id(id) {}
  [[nodiscard]] detail::DatabaseState& state() const;

  detail::DatabaseState* _state = nullptr;
  IoPinId _id;
};

class WireRef
{
 public:
  WireRef() noexcept = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] WireId id() const noexcept { return _id; }
  [[nodiscard]] WireMetadata metadata() const;
  [[nodiscard]] WireRoutingData routingData() const;
  [[nodiscard]] std::size_t pathCount() const;
  [[nodiscard]] WirePathData pathData(std::size_t index) const;

  void update(WireMetadata metadata, WireRoutingData routing);
  [[nodiscard]] bool destroy();

 private:
  friend class Database;
  friend class NetRef;

  WireRef(detail::DatabaseState& state, WireId id) noexcept : _state(&state), _id(id) {}
  [[nodiscard]] detail::DatabaseState& state() const;

  detail::DatabaseState* _state = nullptr;
  WireId _id;
};

class DesignViaRef
{
 public:
  DesignViaRef() noexcept = default;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] ViaId id() const noexcept { return _id; }
  [[nodiscard]] DesignViaData data() const;
  [[nodiscard]] std::string_view name() const;

  void update(DesignViaData value);
  [[nodiscard]] bool destroy();

 private:
  friend class Database;

  DesignViaRef(detail::DatabaseState& state, ViaId id) noexcept : _state(&state), _id(id) {}
  [[nodiscard]] detail::DatabaseState& state() const;

  detail::DatabaseState* _state = nullptr;
  ViaId _id;
};

}  // namespace eccdb
