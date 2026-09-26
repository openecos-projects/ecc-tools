#include "design/routing/storage/DesignRoutingStorage.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <malloc.h>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "design/common/DesignGeometryValidation.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {
namespace {

bool isValidWireStatus(DesignWireStatus status)
{
  switch (status) {
    case DesignWireStatus::kRouted:
    case DesignWireStatus::kFixed:
    case DesignWireStatus::kCover:
    case DesignWireStatus::kShield:
    case DesignWireStatus::kNoShield:
      return true;
  }
  return false;
}

bool isValidNetGeometryStatus(DesignWireStatus status)
{
  return status == DesignWireStatus::kRouted || status == DesignWireStatus::kFixed || status == DesignWireStatus::kCover
         || status == DesignWireStatus::kShield;
}

void validateNetGeometryOptions(DesignWireStatus status, uint32_t flags, uint32_t mask, const std::string& shield_net,
                                const std::string& shape)
{
  constexpr uint32_t kKnownFlags = DesignNetGeometryFlag::kHasMask | DesignNetGeometryFlag::kHasShape;
  const bool has_mask = (flags & DesignNetGeometryFlag::kHasMask) != 0u;
  const bool has_shape = (flags & DesignNetGeometryFlag::kHasShape) != 0u;
  if (!isValidNetGeometryStatus(status) || (flags & ~kKnownFlags) != 0u || has_mask != (mask != 0u) || has_shape == shape.empty()
      || (status == DesignWireStatus::kShield) == shield_net.empty()) {
    throw std::invalid_argument("standalone net geometry options are inconsistent");
  }
}

bool routingIsEmpty(const DesignWireRouting& routing) noexcept
{
  return routing.paths.empty() && routing.points.empty() && routing.vias.empty() && routing.rectangles.empty()
         && routing.extras.empty();
}

uint32_t checkedRoutingSize(std::size_t current, std::size_t added)
{
  if (current > std::numeric_limits<uint32_t>::max() || added > std::numeric_limits<uint32_t>::max() - current) {
    throw std::overflow_error("wire routing vector exceeds uint32_t");
  }
  return static_cast<uint32_t>(current + added);
}

template <typename T>
void reserveRoutingGrowth(std::vector<T>& values, std::size_t required)
{
  if (required > values.capacity()) {
    const auto limit = static_cast<std::size_t>(std::numeric_limits<uint32_t>::max());
    const auto grown = values.capacity() + std::max(values.capacity(), std::size_t{1});
    values.reserve(std::max(required, std::min(grown, limit)));
  }
}

void insertRoutingPath(DesignWireRouting& routing, std::size_t index, DesignWireRouting staged)
{
  static_assert(std::is_nothrow_move_constructible_v<DesignWirePathRecord>
                && std::is_nothrow_move_assignable_v<DesignWirePathRecord>
                && std::is_nothrow_move_constructible_v<DesignWirePoint>
                && std::is_nothrow_move_assignable_v<DesignWirePoint>
                && std::is_nothrow_move_constructible_v<DesignWireVia>
                && std::is_nothrow_move_assignable_v<DesignWireVia>
                && std::is_nothrow_move_constructible_v<DesignWireRectangle>
                && std::is_nothrow_move_assignable_v<DesignWireRectangle>
                && std::is_nothrow_move_constructible_v<DesignWirePathExtra>
                && std::is_nothrow_move_assignable_v<DesignWirePathExtra>);
  if (index > routing.paths.size() || staged.paths.size() != 1u) {
    throw std::out_of_range("wire path index is out of range");
  }

  const auto point_begin = index == routing.paths.size() ? routing.points.size() : routing.paths[index].points.begin;
  const auto via_begin = index == routing.paths.size() ? routing.vias.size() : routing.paths[index].vias.begin;
  const auto rectangle_begin = index == routing.paths.size() ? routing.rectangles.size() : routing.paths[index].rectangles.begin;
  const auto extra_begin = static_cast<std::size_t>(std::count_if(
      routing.paths.begin(), routing.paths.begin() + index,
      [](const DesignWirePathRecord& record) { return record.extra_index != kInvalidDesignWirePathExtraIndex; }));

  checkedRoutingSize(routing.paths.size(), 1u);
  checkedRoutingSize(routing.points.size(), staged.points.size());
  checkedRoutingSize(routing.vias.size(), staged.vias.size());
  checkedRoutingSize(routing.rectangles.size(), staged.rectangles.size());
  checkedRoutingSize(routing.extras.size(), staged.extras.size());
  reserveRoutingGrowth(routing.paths, routing.paths.size() + 1u);
  reserveRoutingGrowth(routing.points, routing.points.size() + staged.points.size());
  reserveRoutingGrowth(routing.vias, routing.vias.size() + staged.vias.size());
  reserveRoutingGrowth(routing.rectangles, routing.rectangles.size() + staged.rectangles.size());
  reserveRoutingGrowth(routing.extras, routing.extras.size() + staged.extras.size());

  // Once capacity is reserved, all element moves and inserts are non-throwing.
  routing.points.insert(routing.points.begin() + point_begin, std::make_move_iterator(staged.points.begin()),
                        std::make_move_iterator(staged.points.end()));
  routing.vias.insert(routing.vias.begin() + via_begin, std::make_move_iterator(staged.vias.begin()),
                      std::make_move_iterator(staged.vias.end()));
  routing.rectangles.insert(routing.rectangles.begin() + rectangle_begin, std::make_move_iterator(staged.rectangles.begin()),
                            std::make_move_iterator(staged.rectangles.end()));
  routing.extras.insert(routing.extras.begin() + extra_begin, std::make_move_iterator(staged.extras.begin()),
                        std::make_move_iterator(staged.extras.end()));

  for (std::size_t following = index; following < routing.paths.size(); ++following) {
    auto& record = routing.paths[following];
    record.points.begin += staged.paths.front().points.count;
    record.vias.begin += staged.paths.front().vias.count;
    record.rectangles.begin += staged.paths.front().rectangles.count;
    if (record.extra_index != kInvalidDesignWirePathExtraIndex) {
      record.extra_index += static_cast<uint32_t>(staged.extras.size());
    }
  }
  auto record = staged.paths.front();
  record.points.begin = static_cast<uint32_t>(point_begin);
  record.vias.begin = static_cast<uint32_t>(via_begin);
  record.rectangles.begin = static_cast<uint32_t>(rectangle_begin);
  if (record.extra_index != kInvalidDesignWirePathExtraIndex) {
    record.extra_index = static_cast<uint32_t>(extra_begin);
  }
  routing.paths.insert(routing.paths.begin() + index, record);
}

void eraseRoutingPath(DesignWireRouting& routing, std::size_t index)
{
  const auto removed = routing.paths[index];
  routing.points.erase(routing.points.begin() + removed.points.begin,
                       routing.points.begin() + removed.points.begin + removed.points.count);
  routing.vias.erase(routing.vias.begin() + removed.vias.begin,
                     routing.vias.begin() + removed.vias.begin + removed.vias.count);
  routing.rectangles.erase(routing.rectangles.begin() + removed.rectangles.begin,
                           routing.rectangles.begin() + removed.rectangles.begin + removed.rectangles.count);
  if (removed.extra_index != kInvalidDesignWirePathExtraIndex) {
    routing.extras.erase(routing.extras.begin() + removed.extra_index);
  }
  routing.paths.erase(routing.paths.begin() + index);
  for (std::size_t following = index; following < routing.paths.size(); ++following) {
    auto& record = routing.paths[following];
    record.points.begin -= removed.points.count;
    record.vias.begin -= removed.vias.count;
    record.rectangles.begin -= removed.rectangles.count;
    if (removed.extra_index != kInvalidDesignWirePathExtraIndex
        && record.extra_index != kInvalidDesignWirePathExtraIndex) {
      --record.extra_index;
    }
  }
}

std::size_t dynamicStringCapacityBytes(const std::string& value) noexcept
{
  const auto data = reinterpret_cast<uintptr_t>(value.data());
  const auto object = reinterpret_cast<uintptr_t>(&value);
  return data >= object && data < object + sizeof(value) ? 0u : value.capacity() + 1u;
}

template <typename T>
void addArrayStatistics(DesignRoutingArrayStatistics& result, const std::vector<T>& values) noexcept
{
  result.count += values.size();
  result.capacity += values.capacity();
  result.element_size = sizeof(T);
  result.capacity_bytes += values.capacity() * sizeof(T);
  if (values.capacity() != 0u) {
    ++result.buffer_count;
    result.allocation_usable_bytes += malloc_usable_size(const_cast<T*>(values.data()));
  }
}

}  // namespace

DesignViaId DesignRoutingStorage::createVia(DesignVia via)
{
  validateVia(via);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignVia>(entity, std::move(via));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignViaId{entity};
}

void DesignRoutingStorage::updateVia(DesignViaId id, DesignVia via)
{
  ensureVia(id);
  validateVia(via, id);
  _registry.replace<DesignVia>(id.entity(), std::move(via));
}

bool DesignRoutingStorage::destroyVia(DesignViaId id)
{
  if (!contains(id) || referencesVia(id)) {
    return false;
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignRoutingStorage::contains(DesignViaId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignVia>(id.entity());
}

DesignViaId DesignRoutingStorage::findVia(std::string_view name) const
{
  const auto view = _registry.view<const DesignVia>();
  for (const auto entity : view) {
    if (view.get<const DesignVia>(entity).name == name) {
      return DesignViaId{entity};
    }
  }
  return {};
}

const DesignVia& DesignRoutingStorage::via(DesignViaId id) const
{
  ensureVia(id);
  return _registry.get<const DesignVia>(id.entity());
}

std::vector<DesignViaId> DesignRoutingStorage::vias() const
{
  const auto view = _registry.view<const DesignVia>();
  std::vector<DesignViaId> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

DesignNonDefaultRuleId DesignRoutingStorage::createNonDefaultRule(DesignNonDefaultRule rule)
{
  validateNonDefaultRule(rule);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignNonDefaultRule>(entity, std::move(rule));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignNonDefaultRuleId{entity};
}

void DesignRoutingStorage::updateNonDefaultRule(DesignNonDefaultRuleId id, DesignNonDefaultRule rule)
{
  ensureNonDefaultRule(id);
  validateNonDefaultRule(rule, id);
  _registry.replace<DesignNonDefaultRule>(id.entity(), std::move(rule));
}

bool DesignRoutingStorage::destroyNonDefaultRule(DesignNonDefaultRuleId id)
{
  if (!contains(id)) {
    return false;
  }
  const auto nets = _registry.view<const DesignNet>();
  for (const auto entity : nets) {
    if (nets.get<const DesignNet>(entity).design_non_default_rule == id) {
      return false;
    }
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignRoutingStorage::contains(DesignNonDefaultRuleId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignNonDefaultRule>(id.entity());
}

DesignNonDefaultRuleId DesignRoutingStorage::findNonDefaultRule(std::string_view name) const
{
  const auto view = _registry.view<const DesignNonDefaultRule>();
  for (const auto entity : view) {
    if (view.get<const DesignNonDefaultRule>(entity).name == name) {
      return DesignNonDefaultRuleId{entity};
    }
  }
  return {};
}

const DesignNonDefaultRule& DesignRoutingStorage::nonDefaultRule(DesignNonDefaultRuleId id) const
{
  ensureNonDefaultRule(id);
  return _registry.get<const DesignNonDefaultRule>(id.entity());
}

std::vector<DesignNonDefaultRuleId> DesignRoutingStorage::nonDefaultRules() const
{
  const auto view = _registry.view<const DesignNonDefaultRule>();
  std::vector<DesignNonDefaultRuleId> result;
  result.reserve(view.size());
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

DesignWireId DesignRoutingStorage::createWire(DesignWire wire, DesignWireRoutingInput routing)
{
  validateWire(wire, routing);
  return createWireTrusted(std::move(wire), std::move(routing));
}

DesignWireId DesignRoutingStorage::createWireTrusted(DesignWire wire, DesignWireRoutingInput routing)
{
  if (!routingIsEmpty(wire.routing) || routing.pathCount() == 0u) {
    throw std::invalid_argument("new design wire requires separate non-empty routing vectors");
  }
  const auto owner = wire.net;
  wire.routing = std::move(routing);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignWire>(entity, std::move(wire));
    _wires_by_net[owner.packed()].emplace_back(entity);
  } catch (...) {
    const auto indexed = _wires_by_net.find(owner.packed());
    if (indexed != _wires_by_net.end() && indexed->second.empty()) {
      _wires_by_net.erase(indexed);
    }
    _registry.destroy(entity);
    throw;
  }
  return DesignWireId{entity};
}

void DesignRoutingStorage::updateWire(DesignWireId id, DesignWire wire, DesignWireRoutingInput routing)
{
  ensureWire(id);
  validateWire(wire, routing);
  if (!routingIsEmpty(wire.routing) || routing.pathCount() == 0u) {
    throw std::invalid_argument("updated design wire requires separate non-empty routing vectors");
  }
  const auto previous_owner = _registry.get<const DesignWire>(id.entity()).net;
  wire.routing = std::move(routing);
  auto new_owner = _wires_by_net.end();
  bool inserted_new_owner = false;
  if (previous_owner != wire.net) {
    try {
      auto inserted = _wires_by_net.try_emplace(wire.net.packed());
      new_owner = inserted.first;
      inserted_new_owner = inserted.second;
      new_owner->second.reserve(new_owner->second.size() + 1u);
    } catch (...) {
      const auto indexed = _wires_by_net.find(wire.net.packed());
      if (indexed != _wires_by_net.end() && indexed->second.empty()) {
        _wires_by_net.erase(indexed);
      }
      throw;
    }
  }
  try {
    _registry.replace<DesignWire>(id.entity(), std::move(wire));
  } catch (...) {
    if (inserted_new_owner) {
      _wires_by_net.erase(new_owner);
    }
    throw;
  }
  if (previous_owner != _registry.get<const DesignWire>(id.entity()).net) {
    new_owner->second.emplace_back(id);
    auto previous = _wires_by_net.find(previous_owner.packed());
    if (previous != _wires_by_net.end()) {
      std::erase(previous->second, id);
      if (previous->second.empty()) {
        _wires_by_net.erase(previous);
      }
    }
  }
}

bool DesignRoutingStorage::destroyWire(DesignWireId id)
{
  if (!contains(id)) {
    return false;
  }
  const auto owner = _registry.get<const DesignWire>(id.entity()).net;
  auto found = _wires_by_net.find(owner.packed());
  if (found != _wires_by_net.end()) {
    std::erase(found->second, id);
    if (found->second.empty()) {
      _wires_by_net.erase(found);
    }
  }
  _registry.destroy(id.entity());
  return true;
}

void DesignRoutingStorage::insertPath(DesignWireId id, std::size_t index, DesignWirePath path)
{
  ensureWire(id);
  const auto& wire = _registry.get<const DesignWire>(id.entity());
  if (index > wire.routing.pathCount()) {
    throw std::out_of_range("wire path index is out of range");
  }
  DesignWireRouting staged;
  staged.appendPath(std::move(path));
  validateWire(wire, staged);
  insertRoutingPath(_registry.get<DesignWire>(id.entity()).routing, index, std::move(staged));
}

void DesignRoutingStorage::replacePath(DesignWireId id, std::size_t index, DesignWirePath path)
{
  ensureWire(id);
  const auto& wire = _registry.get<const DesignWire>(id.entity());
  if (index >= wire.routing.pathCount()) {
    throw std::out_of_range("wire path index is out of range");
  }
  DesignWireRouting staged;
  staged.appendPath(std::move(path));
  validateWire(wire, staged);
  auto& routing = _registry.get<DesignWire>(id.entity()).routing;
  insertRoutingPath(routing, index + 1u, std::move(staged));
  eraseRoutingPath(routing, index);
}

void DesignRoutingStorage::erasePath(DesignWireId id, std::size_t index)
{
  ensureWire(id);
  auto& routing = _registry.get<DesignWire>(id.entity()).routing;
  if (index >= routing.pathCount()) {
    throw std::out_of_range("wire path index is out of range");
  }
  if (routing.pathCount() == 1u) {
    (void) destroyWire(id);
    return;
  }
  eraseRoutingPath(routing, index);
}

bool DesignRoutingStorage::contains(DesignWireId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignWire>(id.entity());
}

const DesignWire& DesignRoutingStorage::wire(DesignWireId id) const
{
  ensureWire(id);
  return _registry.get<const DesignWire>(id.entity());
}

std::size_t DesignRoutingStorage::pathCount(DesignWireId id) const
{
  return wire(id).routing.pathCount();
}

DesignWirePathView DesignRoutingStorage::path(DesignWireId id, std::size_t index) const
{
  return wire(id).routing.path(index);
}

std::vector<DesignWireId> DesignRoutingStorage::wires() const
{
  const auto view = _registry.view<const DesignWire>();
  std::vector<DesignWireId> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  return result;
}

std::vector<DesignWireId> DesignRoutingStorage::wires(DesignNetId owner) const
{
  const auto ids = wireIds(owner);
  return std::vector<DesignWireId>(ids.begin(), ids.end());
}

std::span<const DesignWireId> DesignRoutingStorage::wireIds(DesignNetId owner) const
{
  if (!owner || !_registry.valid(owner.entity()) || !_registry.all_of<DesignNet>(owner.entity())) {
    throw std::out_of_range("invalid design net id");
  }
  const auto found = _wires_by_net.find(owner.packed());
  return found == _wires_by_net.end() ? std::span<const DesignWireId>{} : std::span<const DesignWireId>{found->second};
}

void DesignRoutingStorage::rebuildWireIndex()
{
  decltype(_wires_by_net) rebuilt;
  const auto view = _registry.view<const DesignWire>();
  for (const auto entity : view) {
    rebuilt[view.get<const DesignWire>(entity).net.packed()].emplace_back(entity);
  }
  _wires_by_net.swap(rebuilt);
}

void DesignRoutingStorage::shrinkRoutingToFit()
{
  const auto view = _registry.view<DesignWire>();
  for (const auto entity : view) {
    view.get<DesignWire>(entity).routing.shrinkToFit();
  }
}

DesignRoutingStatistics DesignRoutingStorage::routingStatistics() const noexcept
{
  DesignRoutingStatistics result;
  const auto view = _registry.view<const DesignWire>();
  for (const auto entity : view) {
    const auto& wire = view.get<const DesignWire>(entity);
    const auto& routing = wire.routing;
    addArrayStatistics(result.paths, routing.paths);
    addArrayStatistics(result.path_extras, routing.extras);
    addArrayStatistics(result.points, routing.points);
    addArrayStatistics(result.vias, routing.vias);
    addArrayStatistics(result.rectangles, routing.rectangles);
    for (const auto& extra : routing.extras) {
      result.dynamic_string_capacity_bytes += dynamicStringCapacityBytes(extra.taper_rule);
      result.dynamic_string_capacity_bytes += dynamicStringCapacityBytes(extra.shape);
    }
  }
  return result;
}

void DesignRoutingStorage::validateRestoredRoutingState() const
{
  const auto view = _registry.view<const DesignWire>();
  for (const auto entity : view) {
    const auto& wire = view.get<const DesignWire>(entity);
    try {
      validateWire(wire, wire.routing);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string{"binary design has invalid Wire routing: "} + error.what());
    }
  }
}

void DesignRoutingStorage::setNetGeometry(DesignNetId net, DesignNetGeometry geometry)
{
  validateNetGeometry(net, geometry);
  _registry.emplace_or_replace<DesignNetGeometry>(net.entity(), std::move(geometry));
}


const DesignNetGeometry* DesignRoutingStorage::netGeometry(DesignNetId net) const
{
  if (!net || !_registry.valid(net.entity()) || !_registry.all_of<DesignNet>(net.entity())) {
    throw std::out_of_range("invalid design net id");
  }
  return _registry.try_get<const DesignNetGeometry>(net.entity());
}

uint32_t DesignRoutingStorage::viaCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignVia>().size());
}

uint32_t DesignRoutingStorage::nonDefaultRuleCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignNonDefaultRule>().size());
}

uint32_t DesignRoutingStorage::wireCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignWire>().size());
}

void DesignRoutingStorage::validateVia(const DesignVia& value, DesignViaId ignored) const
{
  constexpr uint32_t kKnownFlags = DesignViaFlag::kGenerated | DesignViaFlag::kHasPatternName;
  if (value.name.empty() || viaNameInUse(value.name, ignored) || (value.flags & ~kKnownFlags) != 0u
      || ((value.flags & DesignViaFlag::kHasPatternName) != 0u) == value.pattern_name.empty()) {
    throw std::invalid_argument("design via name is empty or duplicated");
  }
  const bool generated = (value.flags & DesignViaFlag::kGenerated) != 0u;
  if (generated) {
    if (!value.rectangles.empty() || !value.polygons.empty()) {
      throw std::invalid_argument("generated design via cannot also contain fixed geometry");
    }
    const auto& formula = value.generated;
    constexpr uint32_t kKnownGeneratedFlags = DesignGeneratedViaFlag::kHasRowCol | DesignGeneratedViaFlag::kHasOrigin
                                              | DesignGeneratedViaFlag::kHasOffset | DesignGeneratedViaFlag::kHasCutPattern;
    const auto& tech = _tech_registry.registry();
    if ((formula.flags & ~kKnownGeneratedFlags) != 0u || !formula.via_rule || !tech.valid(formula.via_rule.entity())
        || !tech.all_of<TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer, TechViaRuleGenerateTopLayer>(
            formula.via_rule.entity())
        || !formula.bottom_layer || !tech.valid(formula.bottom_layer.entity())
        || !tech.all_of<TechLayerInfo, TechRoutingLayer>(formula.bottom_layer.entity()) || !formula.cut_layer
        || !tech.valid(formula.cut_layer.entity()) || !tech.all_of<TechLayerInfo, TechCutLayer>(formula.cut_layer.entity())
        || !formula.top_layer || !tech.valid(formula.top_layer.entity())
        || !tech.all_of<TechLayerInfo, TechRoutingLayer>(formula.top_layer.entity()) || formula.cut_size_x <= 0 || formula.cut_size_y <= 0
        || formula.cut_spacing_x < 0 || formula.cut_spacing_y < 0 || formula.bottom_enclosure_x < 0 || formula.bottom_enclosure_y < 0
        || formula.top_enclosure_x < 0 || formula.top_enclosure_y < 0) {
      throw std::invalid_argument("generated design via formula or references are invalid");
    }
    if (tech.get<const TechViaRuleGenerateBottomLayer>(formula.via_rule.entity()).layer != formula.bottom_layer
        || tech.get<const TechViaRuleGenerateCutLayer>(formula.via_rule.entity()).layer != formula.cut_layer
        || tech.get<const TechViaRuleGenerateTopLayer>(formula.via_rule.entity()).layer != formula.top_layer) {
      throw std::invalid_argument("generated design via layers do not match its technology VIARULE");
    }
    const bool has_row_col = (formula.flags & DesignGeneratedViaFlag::kHasRowCol) != 0u;
    const bool has_origin = (formula.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u;
    const bool has_offset = (formula.flags & DesignGeneratedViaFlag::kHasOffset) != 0u;
    const bool has_cut_pattern = (formula.flags & DesignGeneratedViaFlag::kHasCutPattern) != 0u;
    if ((has_row_col && (formula.row_count == 0u || formula.column_count == 0u))
        || (!has_row_col && (formula.row_count != 1u || formula.column_count != 1u)) || (!has_origin && formula.origin != Point{})
        || (!has_offset && (formula.bottom_offset != Point{} || formula.top_offset != Point{}))
        || has_cut_pattern == formula.cut_pattern.empty()) {
      throw std::invalid_argument("generated design via optional fields and flags are inconsistent");
    }
    return;
  }

  const auto& formula = value.generated;
  if (formula.via_rule || formula.bottom_layer || formula.cut_layer || formula.top_layer || formula.flags != 0u || formula.cut_size_x != 0
      || formula.cut_size_y != 0 || formula.cut_spacing_x != 0 || formula.cut_spacing_y != 0 || formula.bottom_enclosure_x != 0
      || formula.bottom_enclosure_y != 0 || formula.top_enclosure_x != 0 || formula.top_enclosure_y != 0 || formula.row_count != 1u
      || formula.column_count != 1u || formula.origin != Point{} || formula.bottom_offset != Point{} || formula.top_offset != Point{}
      || !formula.cut_pattern.empty() || (value.rectangles.empty() && value.polygons.empty())) {
    throw std::invalid_argument("fixed design via contains generated fields or no geometry");
  }
  const auto& tech = _tech_registry.registry();
  for (const auto& rectangle : value.rectangles) {
    if (!rectangle.layer || !tech.valid(rectangle.layer.entity()) || !tech.all_of<TechLayerInfo>(rectangle.layer.entity())
        || !rectangle.rectangle.hasArea()) {
      throw std::invalid_argument("fixed design via rectangle is invalid");
    }
  }
  for (const auto& polygon : value.polygons) {
    if (!polygon.layer || !tech.valid(polygon.layer.entity()) || !tech.all_of<TechLayerInfo>(polygon.layer.entity())
        || polygon.points.size() < 3u) {
      throw std::invalid_argument("fixed design via polygon is invalid");
    }
  }
}

void DesignRoutingStorage::validateNonDefaultRule(const DesignNonDefaultRule& value, DesignNonDefaultRuleId ignored) const
{
  if (value.name.empty() || nonDefaultRuleNameInUse(value.name, ignored)
      || (value.flags & ~DesignNonDefaultRuleFlag::kHardSpacing) != 0u) {
    throw std::invalid_argument("design non-default rule name or flags are invalid");
  }

  const auto& tech = _tech_registry.registry();
  constexpr uint32_t kKnownLayerFlags
      = DesignNdrLayerRuleFlag::kHasDiagWidth | DesignNdrLayerRuleFlag::kHasSpacing | DesignNdrLayerRuleFlag::kHasWireExtension;
  for (std::size_t index = 0; index < value.layer_rules.size(); ++index) {
    const auto& rule = value.layer_rules[index];
    const bool has_diag_width = (rule.flags & DesignNdrLayerRuleFlag::kHasDiagWidth) != 0u;
    const bool has_spacing = (rule.flags & DesignNdrLayerRuleFlag::kHasSpacing) != 0u;
    const bool has_wire_extension = (rule.flags & DesignNdrLayerRuleFlag::kHasWireExtension) != 0u;
    if (!rule.layer || !tech.valid(rule.layer.entity()) || !tech.all_of<TechLayerInfo>(rule.layer.entity()) || rule.width <= 0
        || (rule.flags & ~kKnownLayerFlags) != 0u || (has_diag_width ? rule.diag_width <= 0 : rule.diag_width != 0)
        || (has_spacing ? rule.spacing < 0 : rule.spacing != 0)
        || (has_wire_extension ? rule.wire_extension < 0 : rule.wire_extension != 0)) {
      throw std::invalid_argument("design non-default layer rule is invalid");
    }
    for (std::size_t prior = 0; prior < index; ++prior) {
      if (value.layer_rules[prior].layer == rule.layer) {
        throw std::invalid_argument("design non-default rule repeats a layer");
      }
    }
  }

  for (const auto& via : value.vias) {
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (has_tech_via == has_design_via
        || (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity())))
        || (has_design_via && !contains(via.design_via))) {
      throw std::invalid_argument("design non-default rule VIA reference is invalid");
    }
  }
  for (const auto via_rule : value.via_rules) {
    if (!via_rule || !tech.valid(via_rule.entity()) || !tech.all_of<TechViaRuleGenerate>(via_rule.entity())) {
      throw std::invalid_argument("design non-default rule VIARULE reference is invalid");
    }
  }
  for (std::size_t index = 0; index < value.min_cuts.size(); ++index) {
    const auto& min_cuts = value.min_cuts[index];
    if (!min_cuts.layer || !tech.valid(min_cuts.layer.entity()) || !tech.all_of<TechLayerInfo, TechCutLayer>(min_cuts.layer.entity())
        || min_cuts.cut_count == 0u) {
      throw std::invalid_argument("design non-default rule MINCUTS is invalid");
    }
    for (std::size_t prior = 0; prior < index; ++prior) {
      if (value.min_cuts[prior].layer == min_cuts.layer) {
        throw std::invalid_argument("design non-default rule repeats MINCUTS for a layer");
      }
    }
  }
  if (std::any_of(value.properties.begin(), value.properties.end(), [](const auto& property) { return property.name.empty(); })) {
    throw std::invalid_argument("design non-default rule property name is required");
  }
}

void DesignRoutingStorage::validateWire(const DesignWire& value, const DesignWireRoutingInput& routing) const
{
  if (!value.net || !_registry.valid(value.net.entity()) || !_registry.all_of<DesignNet>(value.net.entity())) {
    throw std::invalid_argument("design wire references an invalid net");
  }
  if (!isValidWireStatus(value.status) || routing.pathCount() == 0u) {
    throw std::invalid_argument("design wire status or paths are invalid");
  }

  constexpr uint32_t kValueFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask
                                   | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape
                                   | DesignWirePathFlag::kHasStyle;
  std::size_t point_begin = 0;
  std::size_t via_begin = 0;
  std::size_t rectangle_begin = 0;
  std::size_t extra_index = 0;
  for (const auto& record : routing.paths) {
    const bool has_extra = (record.flags & kValueFlags) != 0u;
    if (record.points.begin != point_begin || record.vias.begin != via_begin
        || record.rectangles.begin != rectangle_begin || record.points.count == 0u
        || record.points.count > routing.points.size() - point_begin
        || record.vias.count > routing.vias.size() - via_begin
        || record.rectangles.count > routing.rectangles.size() - rectangle_begin
        || (has_extra && record.extra_index != extra_index)
        || (!has_extra && record.extra_index != kInvalidDesignWirePathExtraIndex)) {
      throw std::invalid_argument("design wire flat routing ranges are inconsistent");
    }
    point_begin += record.points.count;
    via_begin += record.vias.count;
    rectangle_begin += record.rectangles.count;
    extra_index += has_extra;
  }
  if (point_begin != routing.points.size() || via_begin != routing.vias.size()
      || rectangle_begin != routing.rectangles.size() || extra_index != routing.extras.size()) {
    throw std::invalid_argument("design wire routing contains unowned flat vector values");
  }
  const bool special_net = _registry.all_of<DesignSpecialNet>(value.net.entity());
  const bool needs_shield_net = value.status == DesignWireStatus::kShield;
  if (needs_shield_net == value.shield_net.empty()) {
    throw std::invalid_argument("design SHIELD wire requires exactly one shield net name");
  }
  for (std::size_t path_index = 0; path_index < routing.pathCount(); ++path_index) {
    const auto path = routing.path(path_index);
    validatePath(path);
    const bool has_width = (path.flags() & DesignWirePathFlag::kHasWidth) != 0u;
    const bool has_shape = (path.flags() & DesignWirePathFlag::kHasShape) != 0u;
    if (special_net != has_width || (!special_net && has_shape)) {
      throw std::invalid_argument("wire path fields do not match NET or SPECIALNET syntax");
    }
    if (!special_net) {
      for (const auto& via : path.vias()) {
        if ((via.flags & DesignWireViaFlag::kHasArray) != 0u) {
          throw std::invalid_argument("VIA arrays are only valid in SPECIALNET paths");
        }
      }
    }
  }
}

void DesignRoutingStorage::validatePath(DesignWirePathView path) const
{
  constexpr uint32_t kKnownPathFlags = DesignWirePathFlag::kHasWidth | DesignWirePathFlag::kHasMask | DesignWirePathFlag::kTaper
                                       | DesignWirePathFlag::kHasTaperRule | DesignWirePathFlag::kHasShape | DesignWirePathFlag::kHasStyle;
  const auto& tech = _tech_registry.registry();
  if (!path.layer() || !tech.valid(path.layer().entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(path.layer().entity())) {
    throw std::invalid_argument("design wire path references an invalid routing layer");
  }
  if ((path.flags() & ~kKnownPathFlags) != 0u || path.points().empty()) {
    throw std::invalid_argument("design wire path flags or points are invalid");
  }
  const bool has_width = (path.flags() & DesignWirePathFlag::kHasWidth) != 0u;
  const bool zero_width_via_path
      = has_width && path.width() == 0 && path.points().size() == 1u && !path.vias().empty() && path.rectangles().empty();
  if ((has_width && path.width() < 0) || (has_width && path.width() == 0 && !zero_width_via_path)
      || (!has_width && path.width() != 0)) {
    throw std::invalid_argument("design wire path width and flags are inconsistent");
  }
  const bool has_mask = (path.flags() & DesignWirePathFlag::kHasMask) != 0u;
  const bool has_taper_rule = (path.flags() & DesignWirePathFlag::kHasTaperRule) != 0u;
  const bool has_shape = (path.flags() & DesignWirePathFlag::kHasShape) != 0u;
  const bool has_style = (path.flags() & DesignWirePathFlag::kHasStyle) != 0u;
  if (has_mask != (path.mask() != 0u) || has_taper_rule == path.taperRule().empty() || has_shape == path.shape().empty()
      || (!has_style && path.style() != 0)) {
    throw std::invalid_argument("design wire path optional fields and flags are inconsistent");
  }

  constexpr uint32_t kKnownPointFlags = DesignWirePointFlag::kHasExtension | DesignWirePointFlag::kVirtual;
  for (const auto& point : path.points()) {
    const bool has_extension = (point.flags & DesignWirePointFlag::kHasExtension) != 0u;
    const bool is_virtual = (point.flags & DesignWirePointFlag::kVirtual) != 0u;
    if ((point.flags & ~kKnownPointFlags) != 0u || (!has_extension && point.extension != 0) || (has_extension && is_virtual)) {
      throw std::invalid_argument("design wire point extension and flags are inconsistent");
    }
  }

  for (const auto& via : path.vias()) {
    if (via.point_index >= path.points().size() || !isValidDesignOrientation(via.orientation)) {
      throw std::invalid_argument("design wire via anchor or orientation is invalid");
    }
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (has_tech_via == has_design_via) {
      throw std::invalid_argument("design wire via must reference exactly one Tech or Design via");
    }
    if (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity()))) {
      throw std::invalid_argument("design wire references an invalid technology via");
    }
    if (has_design_via && !contains(via.design_via)) {
      throw std::invalid_argument("design wire references an invalid design via");
    }

    constexpr uint32_t kKnownViaFlags = DesignWireViaFlag::kHasMask | DesignWireViaFlag::kHasArray;
    const bool has_via_mask = (via.flags & DesignWireViaFlag::kHasMask) != 0u;
    const bool has_array = (via.flags & DesignWireViaFlag::kHasArray) != 0u;
    const bool any_via_mask = via.top_mask != 0u || via.cut_mask != 0u || via.bottom_mask != 0u;
    if ((via.flags & ~kKnownViaFlags) != 0u || has_via_mask != any_via_mask || (has_array && (via.rows == 0u || via.columns == 0u))
        || (!has_array && (via.rows != 1u || via.columns != 1u || via.step_x != 0 || via.step_y != 0))) {
      throw std::invalid_argument("design wire via optional fields and flags are inconsistent");
    }
  }

  for (const auto& rectangle : path.rectangles()) {
    if (rectangle.point_index >= path.points().size() || !rectangle.delta.isValid()) {
      throw std::invalid_argument("design wire rectangle anchor or delta is invalid");
    }
  }
}

void DesignRoutingStorage::validateNetGeometry(DesignNetId net, const DesignNetGeometry& geometry) const
{
  if (!net || !_registry.valid(net.entity()) || !_registry.all_of<DesignNet, DesignSpecialNet>(net.entity())) {
    throw std::invalid_argument("standalone net geometry requires a valid SPECIALNET owner");
  }
  if (geometry.rectangles.empty() && geometry.polygons.empty() && geometry.vias.empty()) {
    throw std::invalid_argument("standalone net geometry cannot be empty");
  }

  const auto& tech = _tech_registry.registry();
  for (const auto& rectangle : geometry.rectangles) {
    validateNetGeometryOptions(rectangle.route_status, rectangle.flags, rectangle.mask, rectangle.shield_net, rectangle.shape);
    if (!rectangle.layer || !tech.valid(rectangle.layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(rectangle.layer.entity())
        || !rectangle.rectangle.hasArea()) {
      throw std::invalid_argument("standalone net rectangle is invalid");
    }
  }
  for (const auto& polygon : geometry.polygons) {
    validateNetGeometryOptions(polygon.route_status, polygon.flags, polygon.mask, polygon.shield_net, polygon.shape);
    if (!polygon.layer || !tech.valid(polygon.layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(polygon.layer.entity())
        || polygon.points.size() < 3u) {
      throw std::invalid_argument("standalone net polygon is invalid");
    }
  }
  for (const auto& via : geometry.vias) {
    validateNetGeometryOptions(via.route_status, via.flags, via.top_mask || via.cut_mask || via.bottom_mask, via.shield_net, via.shape);
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (has_tech_via == has_design_via || via.origins.empty() || !isValidDesignOrientation(via.orientation)
        || (has_tech_via && (!tech.valid(via.tech_via.entity()) || !tech.all_of<TechViaMaster>(via.tech_via.entity())))
        || (has_design_via && !contains(via.design_via))) {
      throw std::invalid_argument("standalone net VIA is invalid");
    }
  }
}

bool DesignRoutingStorage::viaNameInUse(std::string_view name, DesignViaId ignored) const
{
  const auto view = _registry.view<const DesignVia>();
  for (const auto entity : view) {
    if (entity != ignored.entity() && view.get<const DesignVia>(entity).name == name) {
      return true;
    }
  }
  return false;
}

bool DesignRoutingStorage::nonDefaultRuleNameInUse(std::string_view name, DesignNonDefaultRuleId ignored) const
{
  const auto view = _registry.view<const DesignNonDefaultRule>();
  for (const auto entity : view) {
    if (entity != ignored.entity() && view.get<const DesignNonDefaultRule>(entity).name == name) {
      return true;
    }
  }
  return false;
}

bool DesignRoutingStorage::referencesVia(DesignViaId id) const
{
  const auto pin_access_references = [id](const auto& groups) {
    for (const auto& group : groups) {
      for (const auto& point : group.points) {
        if (std::any_of(point.vias.begin(), point.vias.end(), [id](const auto& via) { return via.design_via == id; })) {
          return true;
        }
      }
    }
    return false;
  };
  const auto ndr_view = _registry.view<const DesignNonDefaultRule>();
  for (const auto entity : ndr_view) {
    for (const auto& via : ndr_view.get<const DesignNonDefaultRule>(entity).vias) {
      if (via.design_via == id) {
        return true;
      }
    }
  }
  const auto view = _registry.view<const DesignWire>();
  for (const auto entity : view) {
    const DesignWireId wire_id{entity};
    for (std::size_t path_index = 0; path_index < pathCount(wire_id); ++path_index) {
      for (const auto& via : path(wire_id, path_index).vias()) {
        if (via.design_via == id) {
          return true;
        }
      }
    }
  }
  const auto pin_view = _registry.view<const DesignIoPin>();
  for (const auto entity : pin_view) {
    for (const auto& port : pin_view.get<const DesignIoPin>(entity).ports) {
      for (const auto& via : port.vias) {
        if (via.design_via == id) {
          return true;
        }
      }
    }
  }
  const auto geometry_view = _registry.view<const DesignNetGeometry>();
  for (const auto entity : geometry_view) {
    for (const auto& via : geometry_view.get<const DesignNetGeometry>(entity).vias) {
      if (via.design_via == id) {
        return true;
      }
    }
  }
  const auto catalog_view = _registry.view<const DesignPinAccessCatalog>();
  for (const auto entity : catalog_view) {
    for (const auto& port_groups : catalog_view.get<const DesignPinAccessCatalog>(entity).master_port_groups) {
      if (pin_access_references(port_groups.groups)) {
        return true;
      }
    }
  }
  const auto io_access_view = _registry.view<const DesignIoPinAccessGroups>();
  for (const auto entity : io_access_view) {
    for (const auto& port_groups : io_access_view.get<const DesignIoPinAccessGroups>(entity).port_groups) {
      if (pin_access_references(port_groups.groups)) {
        return true;
      }
    }
  }
  return false;
}

void DesignRoutingStorage::ensureVia(DesignViaId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design via id");
  }
}

void DesignRoutingStorage::ensureNonDefaultRule(DesignNonDefaultRuleId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design non-default rule id");
  }
}

void DesignRoutingStorage::ensureWire(DesignWireId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design wire id");
  }
}

}  // namespace eccdb
