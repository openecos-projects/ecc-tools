#include "design/netlist/storage/DesignNetlistStorage.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "design/common/DesignGeometryValidation.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/LibraryRegistry.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

bool isValidPlacementStatus(DesignPlacementStatus status)
{
  switch (status) {
    case DesignPlacementStatus::kUnplaced:
    case DesignPlacementStatus::kPlaced:
    case DesignPlacementStatus::kFixed:
    case DesignPlacementStatus::kCover:
      return true;
  }
  return false;
}

bool isValidInstanceSource(DesignInstanceSource source)
{
  switch (source) {
    case DesignInstanceSource::kNone:
    case DesignInstanceSource::kNetlist:
    case DesignInstanceSource::kDist:
    case DesignInstanceSource::kUser:
    case DesignInstanceSource::kTiming:
      return true;
  }
  return false;
}

bool isValidSignalUse(DesignSignalUse use)
{
  switch (use) {
    case DesignSignalUse::kNone:
    case DesignSignalUse::kSignal:
    case DesignSignalUse::kAnalog:
    case DesignSignalUse::kPower:
    case DesignSignalUse::kGround:
    case DesignSignalUse::kClock:
    case DesignSignalUse::kTieOff:
    case DesignSignalUse::kScan:
    case DesignSignalUse::kReset:
      return true;
  }
  return false;
}

bool isValidIoPinDirection(DesignIoPinDirection direction)
{
  switch (direction) {
    case DesignIoPinDirection::kNone:
    case DesignIoPinDirection::kInput:
    case DesignIoPinDirection::kOutput:
    case DesignIoPinDirection::kInOut:
    case DesignIoPinDirection::kFeedThru:
      return true;
  }
  return false;
}

bool isValidNetSource(DesignNetSource source)
{
  switch (source) {
    case DesignNetSource::kNone:
    case DesignNetSource::kNetlist:
    case DesignNetSource::kDist:
    case DesignNetSource::kUser:
    case DesignNetSource::kTiming:
    case DesignNetSource::kTest:
      return true;
  }
  return false;
}

void validatePinShapeFields(const TechRegistry& tech_registry, TechLayerId layer, uint32_t flags, uint32_t mask, int32_t spacing,
                            int32_t design_rule_width)
{
  constexpr uint32_t kKnownFlags = DesignPinShapeFlag::kHasMask | DesignPinShapeFlag::kHasSpacing | DesignPinShapeFlag::kHasDesignRuleWidth;
  const auto& tech = tech_registry.registry();
  const bool has_mask = (flags & DesignPinShapeFlag::kHasMask) != 0u;
  const bool has_spacing = (flags & DesignPinShapeFlag::kHasSpacing) != 0u;
  const bool has_design_rule_width = (flags & DesignPinShapeFlag::kHasDesignRuleWidth) != 0u;
  if (!layer || !tech.valid(layer.entity()) || !tech.all_of<TechLayerInfo>(layer.entity()) || (flags & ~kKnownFlags) != 0u
      || has_mask != (mask != 0u) || has_spacing == (spacing == 0) || has_design_rule_width == (design_rule_width == 0)
      || (has_spacing && has_design_rule_width) || spacing < 0 || design_rule_width < 0) {
    throw std::invalid_argument("design IO pin shape layer or optional fields are invalid");
  }
}

void validatePinPort(const DesignNetlistStorage::registry_type& registry, const TechRegistry& tech_registry, const DesignIoPinPort& port)
{
  constexpr uint32_t kKnownFlags = DesignIoPinPortFlag::kExplicit | DesignIoPinPortFlag::kHasPlacement;
  const bool has_placement = (port.flags & DesignIoPinPortFlag::kHasPlacement) != 0u;
  if ((port.flags & ~kKnownFlags) != 0u || !isValidPlacementStatus(port.placement_status) || !isValidDesignOrientation(port.orientation)
      || (!has_placement
          && (port.placement_status != DesignPlacementStatus::kUnplaced || port.origin != Point{}
              || port.orientation != DesignOrientation::kN))) {
    throw std::invalid_argument("design IO pin port placement or flags are invalid");
  }
  for (const auto& rectangle : port.rectangles) {
    validatePinShapeFields(tech_registry, rectangle.layer, rectangle.flags, rectangle.mask, rectangle.spacing, rectangle.design_rule_width);
    if (!rectangle.rectangle.hasArea()) {
      throw std::invalid_argument("design IO pin rectangle must have area");
    }
  }
  for (const auto& polygon : port.polygons) {
    validatePinShapeFields(tech_registry, polygon.layer, polygon.flags, polygon.mask, polygon.spacing, polygon.design_rule_width);
    if (polygon.points.size() < 3u) {
      throw std::invalid_argument("design IO pin polygon requires at least three points");
    }
  }
  const auto& tech = tech_registry.registry();
  for (const auto& via : port.vias) {
    constexpr uint32_t kKnownViaFlags = DesignPinViaFlag::kHasMask;
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    const bool has_mask = (via.flags & DesignPinViaFlag::kHasMask) != 0u;
    const bool any_mask = via.top_mask != 0u || via.cut_mask != 0u || via.bottom_mask != 0u;
    if ((via.flags & ~kKnownViaFlags) != 0u || has_tech_via == has_design_via || has_mask != any_mask
        || (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity())))
        || (has_design_via && (!registry.valid(via.design_via.entity()) || !registry.all_of<DesignVia>(via.design_via.entity())))) {
      throw std::invalid_argument("design IO pin via reference or mask is invalid");
    }
  }
}

template <typename Id, typename Component>
std::vector<Id> componentIds(const DesignNetlistStorage::registry_type& registry)
{
  const auto view = registry.view<const Component>();
  std::vector<Id> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

template <typename Component, typename Id>
void unlinkNetPin(DesignNetlistStorage::registry_type& registry, DesignNetId owner, Id pin)
{
  auto* connections = registry.try_get<Component>(owner.entity());
  if (connections == nullptr) {
    throw std::logic_error("design net reverse connection is missing");
  }
  const auto found = std::find(connections->values.begin(), connections->values.end(), pin);
  if (found == connections->values.end()) {
    throw std::logic_error("design net reverse connection is inconsistent");
  }
  connections->values.erase(found);
  if (connections->values.empty()) {
    registry.remove<Component>(owner.entity());
  }
}

}  // namespace

DesignInstanceId DesignNetlistStorage::createInstance(DesignInstance instance)
{
  validateInstance(instance);
  const auto& master = _library_registry.registry().get<const LibraryCellMaster>(instance.master.entity());
  auto name = instance.name;

  const auto entity = _registry.create();
  const DesignInstanceId id{entity};
  std::vector<DesignInstancePinId> created_pins;
  created_pins.reserve(master.terms.size());
  try {
    _registry.emplace<DesignInstance>(entity, std::move(instance));
    _registry.emplace<DesignInstancePins>(entity);
    for (const auto term : master.terms) {
      const auto& term_registry = _library_registry.registry();
      if (!term || !term_registry.valid(term.entity()) || !term_registry.all_of<LibraryMasterTerm>(term.entity())
          || term_registry.get<const LibraryMasterTerm>(term.entity()).master != _registry.get<const DesignInstance>(entity).master) {
        throw std::logic_error("library cell master contains an invalid master term relation");
      }

      const auto pin_entity = _registry.create();
      created_pins.emplace_back(pin_entity);
      _registry.emplace<DesignInstancePin>(pin_entity, DesignInstancePin{.instance = id, .master_term = term});
      _registry.get<DesignInstancePins>(entity).values.emplace_back(pin_entity);
    }
    const auto [unused, inserted] = _instance_names.emplace(std::move(name), id);
    if (!inserted) {
      throw std::logic_error("design instance name index is inconsistent");
    }
  } catch (...) {
    for (const auto pin : created_pins) {
      if (_registry.valid(pin.entity())) {
        _registry.destroy(pin.entity());
      }
    }
    if (_registry.valid(entity)) {
      _registry.destroy(entity);
    }
    throw;
  }
  return id;
}

void DesignNetlistStorage::updateInstance(DesignInstanceId id, DesignInstance instance)
{
  ensureInstance(id);
  const auto& stored = _registry.get<const DesignInstance>(id.entity());
  if (instance.master != stored.master) {
    throw std::invalid_argument("changing a design instance master requires recreating its pins");
  }
  validateInstance(instance, id);
  if (instance.name == stored.name) {
    _registry.replace<DesignInstance>(id.entity(), std::move(instance));
    return;
  }

  const auto old_name = stored.name;
  const auto [inserted_at, inserted] = _instance_names.emplace(instance.name, id);
  if (!inserted) {
    throw std::logic_error("design instance name index is inconsistent");
  }
  try {
    _registry.replace<DesignInstance>(id.entity(), std::move(instance));
  } catch (...) {
    _instance_names.erase(inserted_at);
    throw;
  }
  _instance_names.erase(old_name);
}

bool DesignNetlistStorage::destroyInstance(DesignInstanceId id)
{
  if (!contains(id)) {
    return false;
  }

  const auto group_view = _registry.view<const DesignGroup>();
  for (const auto entity : group_view) {
    for (const auto member : group_view.get<const DesignGroup>(entity).instances) {
      if (member == id) {
        return false;
      }
    }
  }
  const auto blockage_view = _registry.view<const DesignBlockage>();
  for (const auto entity : blockage_view) {
    if (blockage_view.get<const DesignBlockage>(entity).component == id) {
      return false;
    }
  }

  const auto pins = instancePins(id);
  for (const auto pin : pins) {
    if (!contains(pin)) {
      return false;
    }
    const auto& stored = _registry.get<const DesignInstancePin>(pin.entity());
    if (stored.net || stored.special_net) {
      return false;
    }
  }
  for (const auto pin : pins) {
    _registry.destroy(pin.entity());
  }
  _instance_names.erase(_registry.get<const DesignInstance>(id.entity()).name);
  _registry.destroy(id.entity());
  return true;
}

bool DesignNetlistStorage::contains(DesignInstanceId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignInstance, DesignInstancePins>(id.entity());
}

DesignInstanceId DesignNetlistStorage::findInstance(std::string_view name) const
{
  const auto found = _instance_names.find(name);
  return found == _instance_names.end() ? DesignInstanceId{} : found->second;
}

const DesignInstance& DesignNetlistStorage::instance(DesignInstanceId id) const
{
  ensureInstance(id);
  return _registry.get<const DesignInstance>(id.entity());
}

std::span<const DesignInstancePinId> DesignNetlistStorage::instancePins(DesignInstanceId id) const
{
  ensureInstance(id);
  return _registry.get<const DesignInstancePins>(id.entity()).values;
}

std::vector<DesignInstanceId> DesignNetlistStorage::instances() const
{
  return componentIds<DesignInstanceId, DesignInstance>(_registry);
}

bool DesignNetlistStorage::contains(DesignInstancePinId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignInstancePin>(id.entity());
}

const DesignInstancePin& DesignNetlistStorage::instancePin(DesignInstancePinId id) const
{
  ensureInstancePin(id);
  return _registry.get<const DesignInstancePin>(id.entity());
}

DesignInstancePinId DesignNetlistStorage::findInstancePin(DesignInstanceId owner, LibraryMasterTermId term) const
{
  for (const auto pin : instancePins(owner)) {
    if (_registry.get<const DesignInstancePin>(pin.entity()).master_term == term) {
      return pin;
    }
  }
  return {};
}

DesignInstancePinId DesignNetlistStorage::findInstancePin(DesignInstanceId owner, std::string_view term_name) const
{
  const auto& library = _library_registry.registry();
  for (const auto pin : instancePins(owner)) {
    const auto term = _registry.get<const DesignInstancePin>(pin.entity()).master_term;
    if (library.get<const LibraryMasterTerm>(term.entity()).name == term_name) {
      return pin;
    }
  }
  return {};
}

std::span<const DesignInstancePinId> DesignNetlistStorage::instancePins(DesignNetId owner) const
{
  ensureNet(owner);
  const auto* connections = _registry.try_get<const DesignNetInstancePins>(owner.entity());
  return connections == nullptr ? std::span<const DesignInstancePinId>{} : std::span<const DesignInstancePinId>{connections->values};
}

DesignIoPinId DesignNetlistStorage::createIoPin(DesignIoPin pin)
{
  if (pin.net || pin.special_net) {
    throw std::invalid_argument("create IO pin before connecting it to a net");
  }
  validateIoPin(pin);
  auto name = pin.name;
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignIoPin>(entity, std::move(pin));
    const auto [unused, inserted] = _io_pin_names.emplace(std::move(name), DesignIoPinId{entity});
    if (!inserted) {
      throw std::logic_error("design IO pin name index is inconsistent");
    }
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignIoPinId{entity};
}

void DesignNetlistStorage::updateIoPin(DesignIoPinId id, DesignIoPin pin)
{
  ensureIoPin(id);
  const auto& stored = _registry.get<const DesignIoPin>(id.entity());
  if (pin.net != stored.net || pin.special_net != stored.special_net) {
    throw std::invalid_argument("use connect or disconnect to change an IO pin net");
  }
  validateIoPin(pin, id);
  if (pin.name == stored.name) {
    _registry.replace<DesignIoPin>(id.entity(), std::move(pin));
    return;
  }

  const auto old_name = stored.name;
  const auto [inserted_at, inserted] = _io_pin_names.emplace(pin.name, id);
  if (!inserted) {
    throw std::logic_error("design IO pin name index is inconsistent");
  }
  try {
    _registry.replace<DesignIoPin>(id.entity(), std::move(pin));
  } catch (...) {
    _io_pin_names.erase(inserted_at);
    throw;
  }
  _io_pin_names.erase(old_name);
}

bool DesignNetlistStorage::destroyIoPin(DesignIoPinId id)
{
  if (!contains(id)) {
    return false;
  }
  const auto& pin = _registry.get<const DesignIoPin>(id.entity());
  if (pin.net || pin.special_net) {
    return false;
  }
  _io_pin_names.erase(pin.name);
  _registry.destroy(id.entity());
  return true;
}

bool DesignNetlistStorage::contains(DesignIoPinId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignIoPin>(id.entity());
}

DesignIoPinId DesignNetlistStorage::findIoPin(std::string_view name) const
{
  const auto found = _io_pin_names.find(name);
  return found == _io_pin_names.end() ? DesignIoPinId{} : found->second;
}

const DesignIoPin& DesignNetlistStorage::ioPin(DesignIoPinId id) const
{
  ensureIoPin(id);
  return _registry.get<const DesignIoPin>(id.entity());
}

std::vector<DesignIoPinId> DesignNetlistStorage::ioPins() const
{
  return componentIds<DesignIoPinId, DesignIoPin>(_registry);
}

std::span<const DesignIoPinId> DesignNetlistStorage::ioPins(DesignNetId owner) const
{
  ensureNet(owner);
  const auto* connections = _registry.try_get<const DesignNetIoPins>(owner.entity());
  return connections == nullptr ? std::span<const DesignIoPinId>{} : std::span<const DesignIoPinId>{connections->values};
}

DesignNetId DesignNetlistStorage::createNet(DesignNet net)
{
  return createNet(std::move(net), false);
}

DesignNetId DesignNetlistStorage::createSpecialNet(DesignNet net)
{
  return createNet(std::move(net), true);
}

void DesignNetlistStorage::updateNet(DesignNetId id, DesignNet net)
{
  ensureNet(id);
  const bool special = isSpecialNet(id);
  validateNet(net, special, id);
  const auto& stored = _registry.get<const DesignNet>(id.entity());
  if (net.name == stored.name) {
    _registry.replace<DesignNet>(id.entity(), std::move(net));
    return;
  }

  auto& names = special ? _special_net_names : _regular_net_names;
  const auto old_name = stored.name;
  const auto [inserted_at, inserted] = names.emplace(net.name, id);
  if (!inserted) {
    throw std::logic_error("design net name index is inconsistent");
  }
  try {
    _registry.replace<DesignNet>(id.entity(), std::move(net));
  } catch (...) {
    names.erase(inserted_at);
    throw;
  }
  names.erase(old_name);
}

bool DesignNetlistStorage::destroyNet(DesignNetId id)
{
  if (!contains(id) || !instancePins(id).empty() || !ioPins(id).empty()) {
    return false;
  }
  const auto wire_view = _registry.view<const DesignWire>();
  for (const auto entity : wire_view) {
    if (wire_view.get<const DesignWire>(entity).net == id) {
      return false;
    }
  }
  auto& names = isSpecialNet(id) ? _special_net_names : _regular_net_names;
  names.erase(_registry.get<const DesignNet>(id.entity()).name);
  _registry.destroy(id.entity());
  return true;
}

bool DesignNetlistStorage::contains(DesignNetId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignNet>(id.entity());
}

bool DesignNetlistStorage::isSpecialNet(DesignNetId id) const
{
  ensureNet(id);
  return _registry.all_of<DesignSpecialNet>(id.entity());
}

DesignNetId DesignNetlistStorage::findNet(std::string_view name) const
{
  const auto regular = findRegularNet(name);
  return regular ? regular : findSpecialNet(name);
}

DesignNetId DesignNetlistStorage::findRegularNet(std::string_view name) const
{
  const auto found = _regular_net_names.find(name);
  return found == _regular_net_names.end() ? DesignNetId{} : found->second;
}

DesignNetId DesignNetlistStorage::findSpecialNet(std::string_view name) const
{
  const auto found = _special_net_names.find(name);
  return found == _special_net_names.end() ? DesignNetId{} : found->second;
}

const DesignNet& DesignNetlistStorage::net(DesignNetId id) const
{
  ensureNet(id);
  return _registry.get<const DesignNet>(id.entity());
}

std::vector<DesignNetId> DesignNetlistStorage::nets() const
{
  return componentIds<DesignNetId, DesignNet>(_registry);
}

std::vector<DesignNetId> DesignNetlistStorage::regularNets() const
{
  const auto view = _registry.view<const DesignNet>(entt::exclude<DesignSpecialNet>);
  std::vector<DesignNetId> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<DesignNetId> DesignNetlistStorage::specialNets() const
{
  const auto view = _registry.view<const DesignNet, const DesignSpecialNet>();
  std::vector<DesignNetId> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

void DesignNetlistStorage::setNetOptions(DesignNetId net, DesignNetOptions options)
{
  validateNetOptions(net, options);
  _registry.emplace_or_replace<DesignNetOptions>(net.entity(), std::move(options));
}

const DesignNetOptions* DesignNetlistStorage::netOptions(DesignNetId net) const
{
  ensureNet(net);
  return _registry.try_get<const DesignNetOptions>(net.entity());
}

void DesignNetlistStorage::connect(DesignInstancePinId pin, DesignNetId owner)
{
  ensureInstancePin(pin);
  ensureNet(owner);
  if ((_registry.get<const DesignNet>(owner.entity()).flags & DesignNetFlag::kMustJoin) != 0u && !instancePins(owner).empty()) {
    throw std::invalid_argument("DEF MUSTJOIN stores exactly one component pin reference");
  }
  auto& stored = _registry.get<DesignInstancePin>(pin.entity());
  auto& relation = isSpecialNet(owner) ? stored.special_net : stored.net;
  if (relation && relation != owner) {
    throw std::logic_error("design instance pin is already connected");
  }
  if (!relation) {
    _registry.get_or_emplace<DesignNetInstancePins>(owner.entity()).values.push_back(pin);
  }
  relation = owner;
}

void DesignNetlistStorage::disconnect(DesignInstancePinId pin)
{
  ensureInstancePin(pin);
  auto& stored = _registry.get<DesignInstancePin>(pin.entity());
  if (stored.net) {
    unlinkNetPin<DesignNetInstancePins>(_registry, stored.net, pin);
  }
  if (stored.special_net) {
    unlinkNetPin<DesignNetInstancePins>(_registry, stored.special_net, pin);
  }
  stored.net = {};
  stored.special_net = {};
}

void DesignNetlistStorage::disconnect(DesignInstancePinId pin, DesignNetId owner)
{
  ensureInstancePin(pin);
  ensureNet(owner);
  auto& stored = _registry.get<DesignInstancePin>(pin.entity());
  auto& relation = isSpecialNet(owner) ? stored.special_net : stored.net;
  if (relation != owner) {
    throw std::logic_error("design instance pin is not connected to the requested net");
  }
  unlinkNetPin<DesignNetInstancePins>(_registry, owner, pin);
  relation = {};
}

void DesignNetlistStorage::connect(DesignIoPinId pin, DesignNetId owner)
{
  ensureIoPin(pin);
  ensureNet(owner);
  if ((_registry.get<const DesignNet>(owner.entity()).flags & DesignNetFlag::kMustJoin) != 0u) {
    throw std::invalid_argument("DEF MUSTJOIN cannot connect a top-level IO pin");
  }
  auto& stored = _registry.get<DesignIoPin>(pin.entity());
  auto& relation = isSpecialNet(owner) ? stored.special_net : stored.net;
  if (relation && relation != owner) {
    throw std::logic_error("design IO pin is already connected");
  }
  if (!relation) {
    _registry.get_or_emplace<DesignNetIoPins>(owner.entity()).values.push_back(pin);
  }
  relation = owner;
}

void DesignNetlistStorage::disconnect(DesignIoPinId pin)
{
  ensureIoPin(pin);
  auto& stored = _registry.get<DesignIoPin>(pin.entity());
  if (stored.net) {
    unlinkNetPin<DesignNetIoPins>(_registry, stored.net, pin);
  }
  if (stored.special_net) {
    unlinkNetPin<DesignNetIoPins>(_registry, stored.special_net, pin);
  }
  stored.net = {};
  stored.special_net = {};
}

void DesignNetlistStorage::disconnect(DesignIoPinId pin, DesignNetId owner)
{
  ensureIoPin(pin);
  ensureNet(owner);
  auto& stored = _registry.get<DesignIoPin>(pin.entity());
  auto& relation = isSpecialNet(owner) ? stored.special_net : stored.net;
  if (relation != owner) {
    throw std::logic_error("design IO pin is not connected to the requested net");
  }
  unlinkNetPin<DesignNetIoPins>(_registry, owner, pin);
  relation = {};
}

bool DesignNetlistStorage::referencesMaster(LibraryCellMasterId master) const
{
  const auto view = _registry.view<const DesignInstance>();
  for (const auto entity : view) {
    if (view.get<const DesignInstance>(entity).master == master) {
      return true;
    }
  }
  return false;
}

bool DesignNetlistStorage::referencesMasterTerm(LibraryMasterTermId term) const
{
  const auto view = _registry.view<const DesignInstancePin>();
  for (const auto entity : view) {
    if (view.get<const DesignInstancePin>(entity).master_term == term) {
      return true;
    }
  }
  return false;
}

uint32_t DesignNetlistStorage::instanceCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignInstance>().size());
}

uint32_t DesignNetlistStorage::instancePinCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignInstancePin>().size());
}

uint32_t DesignNetlistStorage::ioPinCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignIoPin>().size());
}

uint32_t DesignNetlistStorage::netCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignNet>().size());
}

void DesignNetlistStorage::rebuildNameIndexes()
{
  NameIndex<DesignInstanceId> instance_names;
  NameIndex<DesignIoPinId> io_pin_names;
  NameIndex<DesignNetId> regular_net_names;
  NameIndex<DesignNetId> special_net_names;
  instance_names.reserve(instanceCount());
  io_pin_names.reserve(ioPinCount());
  regular_net_names.reserve(netCount());
  special_net_names.reserve(_registry.storage<DesignSpecialNet>().size());

  const auto instance_view = _registry.view<const DesignInstance>();
  for (const auto entity : instance_view) {
    if (!instance_names.emplace(instance_view.get<const DesignInstance>(entity).name, DesignInstanceId{entity}).second) {
      throw std::logic_error("duplicate design instance while rebuilding name indexes");
    }
  }

  const auto io_pin_view = _registry.view<const DesignIoPin>();
  for (const auto entity : io_pin_view) {
    if (!io_pin_names.emplace(io_pin_view.get<const DesignIoPin>(entity).name, DesignIoPinId{entity}).second) {
      throw std::logic_error("duplicate design IO pin while rebuilding name indexes");
    }
  }

  const auto net_view = _registry.view<const DesignNet>();
  for (const auto entity : net_view) {
    auto& names = _registry.all_of<DesignSpecialNet>(entity) ? special_net_names : regular_net_names;
    if (!names.emplace(net_view.get<const DesignNet>(entity).name, DesignNetId{entity}).second) {
      throw std::logic_error("duplicate design net while rebuilding name indexes");
    }
  }

  _instance_names.swap(instance_names);
  _io_pin_names.swap(io_pin_names);
  _regular_net_names.swap(regular_net_names);
  _special_net_names.swap(special_net_names);
}

DesignNetId DesignNetlistStorage::createNet(DesignNet net, bool special)
{
  validateNet(net, special);
  auto name = net.name;
  const auto entity = _registry.create();
  const DesignNetId id{entity};
  try {
    _registry.emplace<DesignNet>(entity, std::move(net));
    if (special) {
      _registry.emplace<DesignSpecialNet>(entity);
    }
    auto& names = special ? _special_net_names : _regular_net_names;
    const auto [unused, inserted] = names.emplace(std::move(name), id);
    if (!inserted) {
      throw std::logic_error("design net name index is inconsistent");
    }
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return id;
}

void DesignNetlistStorage::validateInstance(const DesignInstance& value, DesignInstanceId ignored) const
{
  constexpr uint32_t kKnownFlags = DesignInstanceFlag::kHasWeight | DesignInstanceFlag::kHasRegion | DesignInstanceFlag::kHasRegionBounds
                                   | DesignInstanceFlag::kHasHalo | DesignInstanceFlag::kHaloSoft | DesignInstanceFlag::kHasRouteHalo;
  if (value.name.empty() || instanceNameInUse(value.name, ignored)) {
    throw std::invalid_argument("design instance name is empty or duplicated");
  }
  if (!isValidDesignOrientation(value.orientation) || !isValidPlacementStatus(value.placement_status)
      || !isValidInstanceSource(value.source) || (value.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("design instance enum or flags are invalid");
  }
  const bool has_weight = (value.flags & DesignInstanceFlag::kHasWeight) != 0u;
  if (value.weight < 0 || (!has_weight && value.weight != 0)) {
    throw std::invalid_argument("design instance weight and flags are inconsistent");
  }

  const bool has_region = (value.flags & DesignInstanceFlag::kHasRegion) != 0u;
  const bool has_region_bounds = (value.flags & DesignInstanceFlag::kHasRegionBounds) != 0u;
  if (has_region && has_region_bounds) {
    throw std::invalid_argument("design instance cannot use named and inline regions together");
  }
  if (has_region != static_cast<bool>(value.region)
      || (has_region && (!_registry.valid(value.region.entity()) || !_registry.all_of<DesignRegion>(value.region.entity())))) {
    throw std::invalid_argument("design instance region and flags are inconsistent");
  }
  if (has_region_bounds != !value.region_bounds.empty()) {
    throw std::invalid_argument("design instance inline region and flags are inconsistent");
  }
  for (const auto rectangle : value.region_bounds) {
    if (!rectangle.isValid() || !rectangle.hasArea()) {
      throw std::invalid_argument("design instance inline region rectangle must have area");
    }
  }

  const bool has_halo = (value.flags & DesignInstanceFlag::kHasHalo) != 0u;
  const bool soft_halo = (value.flags & DesignInstanceFlag::kHaloSoft) != 0u;
  const bool halo_is_zero = value.halo.left == 0 && value.halo.bottom == 0 && value.halo.right == 0 && value.halo.top == 0;
  if (soft_halo && !has_halo) {
    throw std::invalid_argument("design instance SOFT halo requires HALO");
  }
  if ((!has_halo && !halo_is_zero) || value.halo.left < 0 || value.halo.bottom < 0 || value.halo.right < 0 || value.halo.top < 0) {
    throw std::invalid_argument("design instance halo and flags are inconsistent");
  }

  const bool has_route_halo = (value.flags & DesignInstanceFlag::kHasRouteHalo) != 0u;
  const bool has_route_layers = static_cast<bool>(value.route_halo.min_layer) && static_cast<bool>(value.route_halo.max_layer);
  if (has_route_halo != has_route_layers || (has_route_halo ? value.route_halo.distance <= 0 : value.route_halo.distance != 0)) {
    throw std::invalid_argument("design instance route halo and flags are inconsistent");
  }
  if (has_route_halo) {
    const auto& tech = _tech_registry.registry();
    for (const auto layer : {value.route_halo.min_layer, value.route_halo.max_layer}) {
      if (!tech.valid(layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity())) {
        throw std::invalid_argument("design instance route halo references an invalid routing layer");
      }
    }
  }

  const auto& registry = _library_registry.registry();
  if (!value.master || !registry.valid(value.master.entity()) || !registry.all_of<LibraryCellMaster>(value.master.entity())) {
    throw std::invalid_argument("design instance references an invalid cell master");
  }
}

void DesignNetlistStorage::validateIoPin(const DesignIoPin& value, DesignIoPinId ignored) const
{
  constexpr uint32_t kKnownFlags = DesignIoPinFlag::kSpecial;
  if (value.name.empty() || ioPinNameInUse(value.name, ignored) || !isValidIoPinDirection(value.direction) || !isValidSignalUse(value.use)
      || (value.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("design IO pin name or enum is invalid");
  }
  if ((value.net && (!contains(value.net) || isSpecialNet(value.net)))
      || (value.special_net && (!contains(value.special_net) || !isSpecialNet(value.special_net)))) {
    throw std::invalid_argument("design IO pin references a net of the wrong kind");
  }
  bool has_explicit_port = false;
  bool has_implicit_port = false;
  for (const auto& port : value.ports) {
    validatePinPort(_registry, _tech_registry, port);
    has_explicit_port |= (port.flags & DesignIoPinPortFlag::kExplicit) != 0u;
    has_implicit_port |= (port.flags & DesignIoPinPortFlag::kExplicit) == 0u;
  }
  if ((has_explicit_port && has_implicit_port) || (has_implicit_port && value.ports.size() != 1u)) {
    throw std::invalid_argument("design IO pin cannot mix explicit and implicit ports");
  }
}

void DesignNetlistStorage::validateNet(const DesignNet& value, bool special, DesignNetId ignored) const
{
  constexpr uint32_t kKnownFlags
      = DesignNetFlag::kHasWeight | DesignNetFlag::kFixedBump | DesignNetFlag::kHasNonDefaultRule | DesignNetFlag::kMustJoin;
  if (value.name.empty() || netNameInUse(value.name, special, ignored) || !isValidSignalUse(value.use) || !isValidNetSource(value.source)
      || (value.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("design net name, enum or flags are invalid");
  }
  const bool has_weight = (value.flags & DesignNetFlag::kHasWeight) != 0u;
  if (value.weight < 0 || (!has_weight && value.weight != 0)) {
    throw std::invalid_argument("design net weight and flags are inconsistent");
  }
  const bool has_ndr = (value.flags & DesignNetFlag::kHasNonDefaultRule) != 0u;
  const bool must_join = (value.flags & DesignNetFlag::kMustJoin) != 0u;
  if ((must_join && special) || (must_join && ignored && !ioPins(ignored).empty())) {
    throw std::invalid_argument("design MUSTJOIN must be a regular net without top-level IO pins");
  }
  const auto& tech = _tech_registry.registry();
  const bool has_tech_ndr = static_cast<bool>(value.non_default_rule);
  const bool has_design_ndr = static_cast<bool>(value.design_non_default_rule);
  const bool valid_tech_ndr
      = has_tech_ndr && tech.valid(value.non_default_rule.entity()) && tech.all_of<TechNonDefaultRule>(value.non_default_rule.entity());
  const bool valid_design_ndr = has_design_ndr && _registry.valid(value.design_non_default_rule.entity())
                                && _registry.all_of<DesignNonDefaultRule>(value.design_non_default_rule.entity());
  if (has_ndr != (has_tech_ndr || has_design_ndr) || (has_tech_ndr && has_design_ndr)
      || (has_tech_ndr && !valid_tech_ndr) || (has_design_ndr && !valid_design_ndr)) {
    throw std::invalid_argument("design net non-default rule and flags are inconsistent");
  }
}

void DesignNetlistStorage::validateNetOptions(DesignNetId net, const DesignNetOptions& value) const
{
  ensureNet(net);
  constexpr uint32_t kKnownFlags = DesignNetOptionsFlag::kHasOriginal | DesignNetOptionsFlag::kHasPattern
                                   | DesignNetOptionsFlag::kHasEstimatedCapacitance | DesignNetOptionsFlag::kHasFrequency
                                   | DesignNetOptionsFlag::kHasXTalk | DesignNetOptionsFlag::kHasStyle | DesignNetOptionsFlag::kHasVoltage;
  if ((value.flags & ~kKnownFlags) != 0u || (value.flags == 0u && value.spacing_rules.empty())) {
    throw std::invalid_argument("design net options are empty or contain unknown flags");
  }

  const bool has_original = (value.flags & DesignNetOptionsFlag::kHasOriginal) != 0u;
  const bool has_pattern = (value.flags & DesignNetOptionsFlag::kHasPattern) != 0u;
  const bool valid_pattern = value.pattern == DesignNetPattern::kNone || value.pattern == DesignNetPattern::kBalanced
                             || value.pattern == DesignNetPattern::kSteiner || value.pattern == DesignNetPattern::kTrunk
                             || value.pattern == DesignNetPattern::kWiredLogic;
  const bool has_capacitance = (value.flags & DesignNetOptionsFlag::kHasEstimatedCapacitance) != 0u;
  const bool has_frequency = (value.flags & DesignNetOptionsFlag::kHasFrequency) != 0u;
  const bool has_xtalk = (value.flags & DesignNetOptionsFlag::kHasXTalk) != 0u;
  const bool has_style = (value.flags & DesignNetOptionsFlag::kHasStyle) != 0u;
  const bool has_voltage = (value.flags & DesignNetOptionsFlag::kHasVoltage) != 0u;
  if (has_original == value.original.empty() || !valid_pattern || has_pattern != (value.pattern != DesignNetPattern::kNone)
      || !std::isfinite(value.estimated_capacitance) || value.estimated_capacitance < 0.0
      || (!has_capacitance && value.estimated_capacitance != 0.0) || !std::isfinite(value.frequency) || value.frequency < 0.0
      || (!has_frequency && value.frequency != 0.0) || value.xtalk < 0 || (!has_xtalk && value.xtalk != 0) || value.style < 0
      || (!has_style && value.style != 0) || value.voltage < 0 || (!has_voltage && value.voltage != 0)) {
    throw std::invalid_argument("design net option values and flags are inconsistent");
  }

  const bool special = isSpecialNet(net);
  if ((!special && (has_voltage || !value.spacing_rules.empty())) || (special && has_xtalk)) {
    throw std::invalid_argument("design net options do not match NET or SPECIALNET syntax");
  }
  const auto& tech = _tech_registry.registry();
  for (const auto& rule : value.spacing_rules) {
    constexpr uint32_t kKnownSpacingFlags = DesignNetSpacingRuleFlag::kHasRange;
    const bool has_range = (rule.flags & DesignNetSpacingRuleFlag::kHasRange) != 0u;
    if (!rule.layer || !tech.valid(rule.layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(rule.layer.entity())
        || rule.spacing < 0 || (rule.flags & ~kKnownSpacingFlags) != 0u
        || (has_range && (rule.range_left < 0 || rule.range_right < rule.range_left))
        || (!has_range && (rule.range_left != 0 || rule.range_right != 0))) {
      throw std::invalid_argument("design SPECIALNET spacing rule is invalid");
    }
  }
}

bool DesignNetlistStorage::instanceNameInUse(std::string_view name, DesignInstanceId ignored) const
{
  const auto found = _instance_names.find(name);
  return found != _instance_names.end() && found->second != ignored;
}

bool DesignNetlistStorage::ioPinNameInUse(std::string_view name, DesignIoPinId ignored) const
{
  const auto found = _io_pin_names.find(name);
  return found != _io_pin_names.end() && found->second != ignored;
}

bool DesignNetlistStorage::netNameInUse(std::string_view name, bool special, DesignNetId ignored) const
{
  const auto& names = special ? _special_net_names : _regular_net_names;
  const auto found = names.find(name);
  return found != names.end() && found->second != ignored;
}

void DesignNetlistStorage::ensureInstance(DesignInstanceId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design instance id");
  }
}

void DesignNetlistStorage::ensureInstancePin(DesignInstancePinId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design instance pin id");
  }
}

void DesignNetlistStorage::ensureIoPin(DesignIoPinId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design IO pin id");
  }
}

void DesignNetlistStorage::ensureNet(DesignNetId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design net id");
  }
}

}  // namespace eccdb
