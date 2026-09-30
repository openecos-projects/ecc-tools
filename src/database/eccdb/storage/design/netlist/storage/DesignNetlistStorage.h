#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/container/dense_map.hpp>

#include "design/DesignRegistry.h"
#include "design/netlist/model/NetlistComponents.h"
#include "index/NameHash.h"

namespace eccdb {

class LibraryRegistry;
class TechRegistry;

class DesignNetlistStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignNetlistStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry, const LibraryRegistry& library_registry)
      : _registry(design_registry.registry()), _tech_registry(tech_registry), _library_registry(library_registry)
  {
  }

  [[nodiscard]] DesignInstanceId createInstance(DesignInstance instance);
  void updateInstance(DesignInstanceId id, DesignInstance instance);
  [[nodiscard]] bool destroyInstance(DesignInstanceId id);
  [[nodiscard]] bool contains(DesignInstanceId id) const;
  [[nodiscard]] DesignInstanceId findInstance(std::string_view name) const;
  [[nodiscard]] const DesignInstance& instance(DesignInstanceId id) const;
  // The returned view is invalidated when this instance's Pin storage changes.
  [[nodiscard]] std::span<const DesignInstancePinId> instancePins(DesignInstanceId id) const;
  [[nodiscard]] std::vector<DesignInstanceId> instances() const;

  // Callbacks must not mutate instance or instance-pin storage while it is
  // being visited. The callback receives the dense component values directly
  // and a non-owning view of the instance's ordered pin IDs.
  template <typename Function>
    requires std::invocable<Function&, DesignInstanceId, const DesignInstance&, std::span<const DesignInstancePinId>>
  void forEachInstance(Function&& function) const
  {
    const auto view = _registry.view<const DesignInstance, const DesignInstancePins>();
    view.each([&function](const auto entity, const DesignInstance& instance, const DesignInstancePins& pins) {
      std::invoke(function, DesignInstanceId{entity}, instance, std::span<const DesignInstancePinId>{pins.values});
    });
  }

  [[nodiscard]] bool contains(DesignInstancePinId id) const;
  [[nodiscard]] const DesignInstancePin& instancePin(DesignInstancePinId id) const;
  [[nodiscard]] DesignInstancePinId findInstancePin(DesignInstanceId instance, LibraryMasterTermId term) const;
  // Requires a live instance and library cache. Master-term membership/order
  // must remain unchanged after instance creation; missing names return null.
  [[nodiscard]] DesignInstancePinId findInstancePin(DesignInstanceId instance, std::string_view term_name) const;
  // The returned view is invalidated when this net's connectivity changes.
  [[nodiscard]] std::span<const DesignInstancePinId> instancePins(DesignNetId net) const;

  // OpenDB-aligned dbITerm spellings. The legacy InstancePin API remains a
  // source-compatible view of the same independent entity storage.
  // Like dbITerm lookup by OID, requires a live Term ID from this storage.
  // The legacy instancePin accessor retains its checked-ID behavior.
  [[nodiscard]] const DesignInstanceTerm& instanceTerm(DesignInstanceTermId id) const
  {
    return _registry.get<const DesignInstanceTerm>(id.entity());
  }
  [[nodiscard]] DesignInstanceTermId findInstanceTerm(DesignInstanceId instance, LibraryMasterTermId term) const
  {
    return findInstancePin(instance, term);
  }
  [[nodiscard]] DesignInstanceTermId findInstanceTerm(DesignInstanceId instance, std::string_view term_name) const
  {
    return findInstancePin(instance, term_name);
  }
  [[nodiscard]] std::span<const DesignInstanceTermId> instanceTerms(DesignInstanceId instance) const
  {
    return instancePins(instance);
  }
  [[nodiscard]] std::span<const DesignInstanceTermId> instanceTerms(DesignNetId net) const { return instancePins(net); }

  // Callbacks must not mutate instance-pin storage while it is being visited.
  // Traversal follows the independent DesignInstancePin component's dense
  // order, rather than the owner relation's per-instance order.
  template <typename Function>
  void forEachInstancePin(Function&& function) const
  {
    const auto view = _registry.view<const DesignInstancePin>();
    view.each([&function](const auto entity, const DesignInstancePin& pin) {
      std::invoke(function, DesignInstancePinId{entity}, pin);
    });
  }

  template <typename Function>
    requires std::invocable<Function&, DesignInstanceTermId, const DesignInstanceTerm&>
  void forEachInstanceTerm(Function&& function) const
  {
    forEachInstancePin(std::forward<Function>(function));
  }

  [[nodiscard]] DesignIoPinId createIoPin(DesignIoPin pin, std::vector<DesignBlockPin> pins = {});
  void updateIoPin(DesignIoPinId id, DesignIoPin pin);
  [[nodiscard]] bool destroyIoPin(DesignIoPinId id);
  [[nodiscard]] bool contains(DesignIoPinId id) const;
  [[nodiscard]] DesignIoPinId findIoPin(std::string_view name) const;
  [[nodiscard]] const DesignIoPin& ioPin(DesignIoPinId id) const;
  [[nodiscard]] std::vector<DesignIoPinId> ioPins() const;
  // The returned view is invalidated when this net's connectivity changes.
  [[nodiscard]] std::span<const DesignIoPinId> ioPins(DesignNetId net) const;

  // OpenDB-aligned dbBTerm spellings. The legacy IoPin API remains a
  // source-compatible view of the same block-term storage.
  [[nodiscard]] DesignBlockTermId createBlockTerm(DesignBlockTerm term, std::vector<DesignBlockPin> pins = {})
  {
    return createIoPin(std::move(term), std::move(pins));
  }
  void updateBlockTerm(DesignBlockTermId id, DesignBlockTerm term) { updateIoPin(id, std::move(term)); }
  [[nodiscard]] bool destroyBlockTerm(DesignBlockTermId id) { return destroyIoPin(id); }
  [[nodiscard]] DesignBlockTermId findBlockTerm(std::string_view name) const { return findIoPin(name); }
  [[nodiscard]] const DesignBlockTerm& blockTerm(DesignBlockTermId id) const { return ioPin(id); }
  [[nodiscard]] std::vector<DesignBlockTermId> blockTerms() const { return ioPins(); }
  [[nodiscard]] std::span<const DesignBlockTermId> blockTerms(DesignNetId net) const { return ioPins(net); }

  // Callbacks must not mutate IO-pin storage while it is being visited.
  template <typename Function>
  void forEachIoPin(Function&& function) const
  {
    const auto view = _registry.view<const DesignIoPin>();
    view.each([&function](const auto entity, const DesignIoPin& pin) {
      std::invoke(function, DesignIoPinId{entity}, pin);
    });
  }

  template <typename Function>
    requires std::invocable<Function&, DesignBlockTermId, const DesignBlockTerm&>
  void forEachBlockTerm(Function&& function) const
  {
    forEachIoPin(std::forward<Function>(function));
  }

  [[nodiscard]] bool contains(DesignBlockPinId id) const;
  [[nodiscard]] const DesignBlockPin& blockPin(DesignBlockPinId id) const;
  // The returned view is invalidated when this block term's Pin storage changes.
  [[nodiscard]] std::span<const DesignBlockPinId> blockPins(DesignBlockTermId term) const;
  [[nodiscard]] DesignBlockPinId createBlockPin(DesignBlockTermId term, DesignBlockPin pin);
  void updateBlockPin(DesignBlockPinId id, DesignBlockPin pin);
  [[nodiscard]] bool destroyBlockPin(DesignBlockPinId id);

  // Callbacks must not mutate block-pin storage while it is being visited.
  template <typename Function>
    requires std::invocable<Function&, DesignBlockPinId, const DesignBlockPin&>
  void forEachBlockPin(DesignBlockTermId term, Function&& function) const
  {
    for (const auto pin : blockPins(term)) {
      std::invoke(function, pin, blockPin(pin));
    }
  }

  template <typename Function>
    requires std::invocable<Function&, DesignBlockPinId, const DesignBlockPin&>
  void forEachBlockPin(Function&& function) const
  {
    const auto view = _registry.view<const DesignBlockPin>();
    view.each([&function](const auto entity, const DesignBlockPin& pin) {
      std::invoke(function, DesignBlockPinId{entity}, pin);
    });
  }

  [[nodiscard]] DesignNetId createNet(DesignNet net);
  [[nodiscard]] DesignNetId createSpecialNet(DesignNet net);
  void updateNet(DesignNetId id, DesignNet net);
  [[nodiscard]] bool destroyNet(DesignNetId id);
  [[nodiscard]] bool contains(DesignNetId id) const;
  [[nodiscard]] bool isSpecialNet(DesignNetId id) const;
  [[nodiscard]] DesignNetId findNet(std::string_view name) const;
  [[nodiscard]] DesignNetId findRegularNet(std::string_view name) const;
  [[nodiscard]] DesignNetId findSpecialNet(std::string_view name) const;
  [[nodiscard]] const DesignNet& net(DesignNetId id) const;
  [[nodiscard]] std::vector<DesignNetId> nets() const;
  [[nodiscard]] std::vector<DesignNetId> regularNets() const;
  [[nodiscard]] std::vector<DesignNetId> specialNets() const;

  // These traversal APIs preserve the registry's dense order. They do not
  // sort by name and do not allocate an intermediate ID vector.
  template <typename Function>
    requires std::invocable<Function&, DesignNetId, const DesignNet&>
  void forEachRegularNet(Function&& function) const
  {
    const auto view = _registry.view<const DesignNet>(entt::exclude<DesignSpecialNet>);
    view.each([&function](const auto entity, const DesignNet& net) {
      std::invoke(function, DesignNetId{entity}, net);
    });
  }

  template <typename Function>
    requires std::invocable<Function&, DesignNetId, const DesignNet&>
  void forEachSpecialNet(Function&& function) const
  {
    const auto view = _registry.view<const DesignNet, const DesignSpecialNet>();
    view.each([&function](const auto entity, const DesignNet& net) {
      std::invoke(function, DesignNetId{entity}, net);
    });
  }
  void setNetOptions(DesignNetId net, DesignNetOptions options);
  [[nodiscard]] const DesignNetOptions* netOptions(DesignNetId net) const;

  // Unchecked: IDs must be live in this storage, and the pin's relation for
  // this net kind must be empty. Caller guarantees valid MUSTJOIN connectivity.
  void connect(DesignInstancePinId pin, DesignNetId net);
  void disconnect(DesignInstancePinId pin);
  void disconnect(DesignInstancePinId pin, DesignNetId net);
  // Same connection contract as the instance-pin overload.
  void connect(DesignIoPinId pin, DesignNetId net);
  void disconnect(DesignIoPinId pin);
  void disconnect(DesignIoPinId pin, DesignNetId net);

  [[nodiscard]] bool referencesMaster(LibraryCellMasterId master) const;
  [[nodiscard]] bool referencesMasterTerm(LibraryMasterTermId term) const;
  [[nodiscard]] uint32_t instanceCount() const;
  [[nodiscard]] uint32_t instancePinCount() const;
  [[nodiscard]] uint32_t ioPinCount() const;
  [[nodiscard]] uint32_t blockPinCount() const;
  [[nodiscard]] uint32_t netCount() const;

  // Name indexes are derived acceleration data. Rebuild them after a direct
  // registry restore or rollback; they are intentionally not serialized.
  void rebuildNameIndexes();
  // Rebind runtime-only Library IDs and pointers after a binary restore.
  void rebuildLibraryCaches();

 private:
  [[nodiscard]] DesignNetId createNet(DesignNet net, bool special);
  void validateInstance(const DesignInstance& instance, DesignInstanceId ignored = {}) const;
  void validateIoPin(const DesignIoPin& pin, DesignIoPinId ignored = {}) const;
  void validateBlockPin(const DesignBlockPin& pin, DesignBlockPinId ignored = {}) const;
  void validateBlockPinRelation(DesignBlockTermId term, const DesignBlockPin& pin, DesignBlockPinId ignored = {}) const;
  void validateNet(const DesignNet& net, bool special, DesignNetId ignored = {}) const;
  void validateNetOptions(DesignNetId net, const DesignNetOptions& options) const;
  [[nodiscard]] bool instanceNameInUse(std::string_view name, DesignInstanceId ignored = {}) const;
  [[nodiscard]] bool ioPinNameInUse(std::string_view name, DesignIoPinId ignored = {}) const;
  [[nodiscard]] bool netNameInUse(std::string_view name, bool special, DesignNetId ignored = {}) const;
  void ensureInstance(DesignInstanceId id) const;
  void ensureInstancePin(DesignInstancePinId id) const;
  void ensureIoPin(DesignIoPinId id) const;
  void ensureBlockPin(DesignBlockPinId id) const;
  void ensureNet(DesignNetId id) const;
  [[nodiscard]] DesignInstancePin& uncheckedInstancePin(DesignInstancePinId id) noexcept;
  [[nodiscard]] const DesignInstancePin& uncheckedInstancePin(DesignInstancePinId id) const noexcept;

  registry_type& _registry;
  const TechRegistry& _tech_registry;
  const LibraryRegistry& _library_registry;
  entt::dense_map<std::string, DesignInstanceId, index::TransparentStringHash, std::equal_to<>> _instance_names;
  entt::dense_map<std::string, DesignIoPinId, index::TransparentStringHash, std::equal_to<>> _io_pin_names;
  entt::dense_map<std::string, DesignNetId, index::TransparentStringHash, std::equal_to<>> _regular_net_names;
  entt::dense_map<std::string, DesignNetId, index::TransparentStringHash, std::equal_to<>> _special_net_names;
};

}  // namespace eccdb
