// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#include "geometry/GeometryEditService.h"

#include <stdexcept>
#include <array>
#include <algorithm>
#include <unordered_map>
#include <variant>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/fill/model/FillComponents.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"
#include "geometry/GeometryMutation.h"

namespace eccdb {

namespace {

GeometryShapeId toShapeId(uint64_t value)
{
  return GeometryShapeId{static_cast<GeometryEntity>(value)};
}

using SavedPayload = std::variant<DesignRouteShape, DesignGeometryRectangle, DesignGeometryPolygon,
    DesignGeometryNetVia, GeometryRectangle, GeometryPolygon, GeometryBoundary, DesignPinRectangle,
    DesignPinPolygon, DesignPinVia, DesignViaRectangle, DesignViaPolygon, DesignRouteGuide,
    DesignTrackAssignmentShape>;

std::vector<GeometryShapeId>& ownerSequence(DesignStore& design, const GeometryShape& metadata, bool restoring = false)
{
  auto& objects = design.designRegistry().registry();
  const auto entity = static_cast<DesignEntity>(metadata.owner.value);
  switch (metadata.owner.domain) {
    case GeometryOwnerDomain::kDesignWire: return objects.get<DesignWire>(entity).shapes;
    case GeometryOwnerDomain::kDesignNet:
      return restoring ? objects.get_or_emplace<DesignNetGeometry>(entity).shapes : objects.get<DesignNetGeometry>(entity).shapes;
    case GeometryOwnerDomain::kDesignBlockPin: {
      auto& pin = objects.get<DesignBlockPin>(entity);
      if (metadata.kind == GeometryShapeKind::kRectangle) return pin.rectangles;
      if (metadata.kind == GeometryShapeKind::kPolygon) return pin.polygons;
      return pin.vias;
    }
    case GeometryOwnerDomain::kDesignInstance: return objects.get<DesignInstance>(entity).region_bounds;
    case GeometryOwnerDomain::kDesignRegion: return objects.get<DesignRegion>(entity).rectangles;
    case GeometryOwnerDomain::kDesignBlockage: {
      auto& blockage = objects.get<DesignBlockage>(entity);
      return metadata.kind == GeometryShapeKind::kPolygon ? blockage.polygons : blockage.rectangles;
    }
    case GeometryOwnerDomain::kDesignFill: return objects.get<DesignFill>(entity).rectangles;
    case GeometryOwnerDomain::kDesignGuide:
      return restoring ? objects.get_or_emplace<DesignNetRouteGuides>(entity).values : objects.get<DesignNetRouteGuides>(entity).values;
    case GeometryOwnerDomain::kDesignTrackAssignment:
      return restoring ? objects.get_or_emplace<DesignNetTrackAssignments>(entity).values : objects.get<DesignNetTrackAssignments>(entity).values;
    default: throw std::invalid_argument("this structural geometry cannot be deleted individually");
  }
}

void updateOwnerAfterRemoval(DesignStore& design, const GeometryShape& metadata, bool sequence_empty)
{
  auto& objects = design.designRegistry().registry();
  const auto entity = static_cast<DesignEntity>(metadata.owner.value);
  if (metadata.owner.domain == GeometryOwnerDomain::kDesignBlockPin) objects.remove<DesignBlockPinAccessGroups>(entity);
  if (!sequence_empty) return;
  switch (metadata.owner.domain) {
    case GeometryOwnerDomain::kDesignNet: objects.remove<DesignNetGeometry>(entity); break;
    case GeometryOwnerDomain::kDesignGuide: objects.remove<DesignNetRouteGuides>(entity); break;
    case GeometryOwnerDomain::kDesignTrackAssignment: objects.remove<DesignNetTrackAssignments>(entity); break;
    case GeometryOwnerDomain::kDesignInstance: objects.get<DesignInstance>(entity).flags &= ~DesignInstanceFlag::kHasRegionBounds; break;
    default: break;
  }
}

}  // namespace

struct GeometryEditService::Journal
{
  struct Entry {
    GeometryShape metadata;
    SavedPayload payload;
    std::optional<DesignRouteShapeAttributes> attributes;
    std::size_t ordinal;
    Rect bounds;
    bool removed = false;
    uint64_t removal_order = 0;
  };
  std::unordered_map<uint64_t, Entry> entries;
  struct PendingDeletion {
    GeometryShape metadata;
    std::size_t ordinal = 0;
  };
  // beforeChange is deliberately non-mutating: a Storage failure must not
  // perturb undo ordering. afterChange consumes this capture only after a
  // permanent deletion actually succeeded.
  std::unordered_map<uint64_t, PendingDeletion> pending_deletions;
  uint64_t next_removal_order = 0;

  static bool sameSequence(const GeometryShape& a, const GeometryShape& b)
  {
    if (a.owner.domain != b.owner.domain || a.owner.value != b.owner.value) return false;
    // Block pins keep rectangles, polygons and vias in separate sequences.
    return a.owner.domain != GeometryOwnerDomain::kDesignBlockPin || a.kind == b.kind;
  }

  std::size_t ordinalIncludingRemoved(const GeometryShape& metadata, std::size_t active_ordinal) const
  {
    std::vector<std::size_t> removed_ordinals;
    for (const auto& [id, saved] : entries) {
      static_cast<void>(id);
      if (saved.removed && sameSequence(saved.metadata, metadata)) removed_ordinals.push_back(saved.ordinal);
    }
    std::sort(removed_ordinals.begin(), removed_ordinals.end());
    for (const auto ordinal : removed_ordinals)
      if (ordinal <= active_ordinal) ++active_ordinal;
    return active_ordinal;
  }

  std::size_t activeOrdinal(const Entry& entry) const
  {
    std::size_t ordinal = entry.ordinal;
    for (const auto& [id, saved] : entries) {
      static_cast<void>(id);
      if (saved.removed && sameSequence(saved.metadata, entry.metadata) && saved.ordinal < entry.ordinal) --ordinal;
    }
    return ordinal;
  }
};

GeometryEditService::GeometryEditService(DatabaseContext& context) : _context(&context), _journal(std::make_unique<Journal>())
{
  _context->addObserver(*this);
}

GeometryEditService::~GeometryEditService()
{
  _context->removeObserver(*this);
  auto& registry = _context->design().geometryStorage().registry().registry();
  for (const auto& [id, entry] : _journal->entries) {
    const auto entity = toShapeId(id).entity();
    if (entry.removed && registry.valid(entity)) {
      registry.destroy(entity);
      _context->design().geometryIndex().forgetRemovedSlot(toShapeId(id));
    }
  }
}

void GeometryEditService::beforeChange(const DatabaseChange& change) noexcept
{
  if (!includes(change.domains, DatabaseChangeDomain::kGeometry) || change.geometry_deletions.empty()) return;
  try {
    auto& design = _context->design();
    auto& geometry = design.geometryStorage();
    for (const auto shape : change.geometry_deletions) {
      if (!geometry.contains(shape)) continue;
      const auto& metadata = geometry.registry().shape(shape);
      const bool has_pending = std::any_of(_journal->entries.begin(), _journal->entries.end(), [&](const auto& saved) {
        return saved.second.removed && Journal::sameSequence(saved.second.metadata, metadata);
      });
      if (!has_pending) continue;
      const auto& sequence = ownerSequence(design, metadata);
      const auto position = std::find(sequence.begin(), sequence.end(), shape);
      if (position == sequence.end()) continue;
      _journal->pending_deletions.insert_or_assign(
          shape.packed(), Journal::PendingDeletion{metadata,
                                                    _journal->ordinalIncludingRemoved(
                                                        metadata, static_cast<std::size_t>(position - sequence.begin()))});
    }
  } catch (...) {
    // Context notifications are noexcept. A best-effort journal capture must
    // never turn an otherwise valid Storage edit into process termination.
  }
}

void GeometryEditService::afterChange(const DatabaseChange& change) noexcept
{
  if (!includes(change.domains, DatabaseChangeDomain::kGeometry)) return;
  auto& design = _context->design();
  auto& registry = design.geometryRegistry().registry();
  const auto& objects = design.designRegistry().registry();
  // Commit previously captured virtual-sequence gaps only after the caller
  // reports a successful permanent deletion. This covers typed Routing edits
  // without scanning the whole geometry registry or corrupting a session when
  // Storage rejects an edit.
  for (const auto shape : change.geometry_deletions) {
    const auto pending = _journal->pending_deletions.find(shape.packed());
    if (pending == _journal->pending_deletions.end()) continue;
    if (!design.geometryStorage().contains(shape)) {
      for (auto& [saved_id, saved] : _journal->entries) {
        static_cast<void>(saved_id);
        if (saved.removed && Journal::sameSequence(saved.metadata, pending->second.metadata)
            && saved.ordinal > pending->second.ordinal) {
          --saved.ordinal;
        }
      }
    }
    _journal->pending_deletions.erase(pending);
  }
  // A parent deleted by another business service can have detached shapes in
  // this undo journal. Release those reserved slots rather than restoring a
  // child beneath a stale owner. Canonical live geometry is not changed here.
  for (auto it = _journal->entries.begin(); it != _journal->entries.end();) {
    const auto& entry = it->second;
    if (!entry.removed) {
      // Restored canonical geometry no longer needs a detached payload copy.
      it = _journal->entries.erase(it);
      continue;
    }
    const bool replaced = std::find(change.geometry_replacements.begin(), change.geometry_replacements.end(),
                                    entry.metadata.owner) != change.geometry_replacements.end();
    if (replaced || !objects.valid(static_cast<DesignEntity>(entry.metadata.owner.value))) {
      const auto shape = toShapeId(it->first);
      if (entry.removed && registry.valid(shape.entity())) {
        registry.destroy(shape.entity());
        design.geometryIndex().forgetRemovedSlot(shape);
      }
      it = _journal->entries.erase(it);
    } else {
      ++it;
    }
  }
}

void GeometryEditService::notifyChange(const DatabaseChange& change) noexcept
{
  if (_batch_depth != 0) return;
  _context->notifyAfter(change);
}

void GeometryEditService::moveShape(uint64_t id, int32_t dx, int32_t dy)
{
  if (!_context) throw std::logic_error("GeometryEditService has no database");
  auto& design = _context->design();
  const auto shape = toShapeId(id);
  const auto mutation = detail::captureGeometryMutation(design, shape);
  design.geometryStorage().moveShape(shape, dx, dy);
  detail::finishGeometryMutation(design, mutation);
  notifyChange(mutation.change());
}

bool GeometryEditService::destroyShape(uint64_t id)
{
  if (!_context) return false;
  auto& design = _context->design();
  const auto shape = toShapeId(id);
  if (!design.geometryStorage().contains(shape)) return false;
  const auto mutation = detail::captureGeometryMutation(design, shape);
  const auto change = mutation.change(true);
  _context->notifyBefore(change);
  const auto destroyed = design.geometryStorage().destroy(shape);
  if (destroyed) {
    detail::finishGeometryMutation(design, mutation, true);
    notifyChange(change);
  }
  return destroyed;
}

DesignFillId GeometryEditService::createFill(DesignFillInput fill)
{
  if (!_context) throw std::logic_error("GeometryEditService has no database");
  auto& design = _context->design();
  const auto epoch = design.geometryStorage().geometryEpoch();
  const auto id = design.fillStorage().createFill(std::move(fill));
  detail::finishGeometryOwnerReplacement(design, epoch);
  const std::array replacements{designFillOwner(id)};
  notifyChange(DatabaseChange{.domains = DatabaseChangeDomain::kGeometry, .geometry_replacements = replacements});
  return id;
}

void GeometryEditService::updateFill(DesignFillId id, DesignFillInput fill)
{
  if (!_context) throw std::logic_error("GeometryEditService has no database");
  auto& design = _context->design();
  const auto epoch = design.geometryStorage().geometryEpoch();
  design.fillStorage().updateFill(id, std::move(fill));
  detail::finishGeometryOwnerReplacement(design, epoch);
  const std::array replacements{designFillOwner(id)};
  notifyChange(DatabaseChange{.domains = DatabaseChangeDomain::kGeometry, .geometry_replacements = replacements});
}

bool GeometryEditService::destroyFill(DesignFillId id)
{
  if (!_context || !_context->design().fillStorage().contains(id)) return false;
  auto& design = _context->design();
  const auto epoch = design.geometryStorage().geometryEpoch();
  if (!design.fillStorage().destroyFill(id)) return false;
  detail::finishGeometryOwnerReplacement(design, epoch);
  const std::array replacements{designFillOwner(id)};
  notifyChange(DatabaseChange{.domains = DatabaseChangeDomain::kGeometry, .geometry_replacements = replacements});
  return true;
}

bool GeometryEditService::removeShape(uint64_t id)
{
  auto& design = _context->design();
  auto& geometry = design.geometryStorage();
  const auto shape = toShapeId(id);
  if (!geometry.contains(shape)) return false;
  const auto mutation = detail::captureGeometryMutation(design, shape);
  const auto view = geometry.shape(shape);
  auto& sequence = ownerSequence(design, *view.metadata);
  const auto position = std::find(sequence.begin(), sequence.end(), shape);
  if (position == sequence.end()) throw std::logic_error("geometry is missing its owner relation");
  if (sequence.size() == 1 && (view.metadata->owner.domain == GeometryOwnerDomain::kDesignRegion
                              || view.metadata->owner.domain == GeometryOwnerDomain::kDesignFill))
    throw std::invalid_argument("cannot delete the final structural owner geometry");
  if (view.metadata->owner.domain == GeometryOwnerDomain::kDesignBlockage) {
    const auto& owner = design.designRegistry().registry().get<DesignBlockage>(
        static_cast<DesignEntity>(view.metadata->owner.value));
    if (owner.rectangles.size() + owner.polygons.size() == 1)
      throw std::invalid_argument("cannot delete the final blockage geometry");
  }
  auto payload = std::visit([](auto pointer) -> SavedPayload {
    if constexpr (std::is_same_v<decltype(pointer), std::monostate>) throw std::logic_error("missing payload");
    else return *pointer;
  }, view.payload);
  auto& registry = geometry.registry().registry();
  const auto* attributes = registry.try_get<DesignRouteShapeAttributes>(shape.entity());
  Journal::Entry entry{*view.metadata, std::move(payload),
      attributes ? std::optional<DesignRouteShapeAttributes>{*attributes} : std::nullopt,
      _journal->ordinalIncludingRemoved(*view.metadata, static_cast<std::size_t>(position - sequence.begin())),
      mutation.old_bounds, true, _journal->next_removal_order++};
  // Complete all potentially allocating journal work before changing state.
  auto [saved, inserted] = _journal->entries.insert_or_assign(id, std::move(entry));
  static_cast<void>(inserted);
  sequence.erase(position);
  updateOwnerAfterRemoval(design, saved->second.metadata, sequence.empty());
  std::visit([&](const auto& value) { registry.remove<std::remove_cvref_t<decltype(value)>>(shape.entity()); }, saved->second.payload);
  registry.remove<GeometryShape, GeometryBounds, DesignRouteShapeAttributes>(shape.entity());
  design.routingStorage().markGeometryChanged();
  detail::finishGeometryMutation(design, mutation);
  notifyChange(mutation.change());
  return true;
}

bool GeometryEditService::restoreShape(uint64_t id)
{
  const auto iterator = _journal->entries.find(id);
  if (iterator == _journal->entries.end() || !iterator->second.removed) return false;
  auto& design = _context->design();
  auto& saved = iterator->second;
  auto& sequence = ownerSequence(design, saved.metadata, true);
  auto& registry = design.geometryStorage().registry().registry();
  const auto shape = toShapeId(id);
  if (!registry.valid(shape.entity())) throw std::logic_error("removed geometry slot was released");
  sequence.reserve(sequence.size() + 1);
  const auto epoch = design.geometryStorage().geometryEpoch();
  try {
    std::visit([&](const auto& value) { registry.emplace<std::remove_cvref_t<decltype(value)>>(shape.entity(), value); }, saved.payload);
    registry.emplace<GeometryShape>(shape.entity(), saved.metadata);
    // Bounds are a derived cache. Restoration keeps the canonical payload and
    // handle; the cache is materialized only if an edit/query explicitly asks
    // for it.
    if (saved.attributes) registry.emplace<DesignRouteShapeAttributes>(shape.entity(), *saved.attributes);
  } catch (...) {
    std::visit([&](const auto& value) { registry.remove<std::remove_cvref_t<decltype(value)>>(shape.entity()); }, saved.payload);
    registry.remove<GeometryShape, GeometryBounds, DesignRouteShapeAttributes>(shape.entity());
    throw;
  }
  sequence.insert(sequence.begin() + std::min(_journal->activeOrdinal(saved), sequence.size()), shape);
  if (saved.metadata.owner.domain == GeometryOwnerDomain::kDesignInstance)
    design.designRegistry().registry().get<DesignInstance>(static_cast<DesignEntity>(saved.metadata.owner.value)).flags |= DesignInstanceFlag::kHasRegionBounds;
  if (saved.metadata.owner.domain == GeometryOwnerDomain::kDesignBlockPin)
    design.designRegistry().registry().remove<DesignBlockPinAccessGroups>(static_cast<DesignEntity>(saved.metadata.owner.value));
  saved.removed = false;
  design.routingStorage().markGeometryChanged();
  auto mutation = detail::captureGeometryMutation(design, shape);
  mutation.old_bounds = saved.bounds;
  mutation.previous_epoch = epoch;
  detail::finishGeometryMutation(design, mutation);
  notifyChange(mutation.change());
  return true;
}

bool GeometryEditService::removeShapes(std::span<const uint64_t> ids)
{
  const auto batch = [&]() {
  struct BatchGuard { uint32_t& depth; explicit BatchGuard(uint32_t& value) : depth(value) { ++depth; } ~BatchGuard() { --depth; } } guard{_batch_depth};
  std::vector<uint64_t> removed;
  removed.reserve(ids.size());
  try {
    for (const auto id : ids) {
      if (!removeShape(id)) {
        for (auto iterator = removed.rbegin(); iterator != removed.rend(); ++iterator)
          static_cast<void>(restoreShape(*iterator));
        return false;
      }
      removed.push_back(id);
    }
  } catch (...) {
    for (auto iterator = removed.rbegin(); iterator != removed.rend(); ++iterator)
      static_cast<void>(restoreShape(*iterator));
    throw;
  }
  return !removed.empty();
  };
  const auto result = batch();
  if (result) {
    _context->notifyAfter(DatabaseChange{.domains = DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kRouting | DatabaseChangeDomain::kPinAccess});
  }
  return result;
}

bool GeometryEditService::restoreShapes(std::span<const uint64_t> ids)
{
  const auto batch = [&]() {
  struct BatchGuard { uint32_t& depth; explicit BatchGuard(uint32_t& value) : depth(value) { ++depth; } ~BatchGuard() { --depth; } } guard{_batch_depth};
  // The caller may supply a set in a different order. The journal records
  // actual deletion order so captured ordinals always reconstruct the owner.
  std::vector<uint64_t> ordered(ids.begin(), ids.end());
  for (const auto id : ordered) {
    const auto saved = _journal->entries.find(id);
    if (saved == _journal->entries.end() || !saved->second.removed) return false;
  }
  std::sort(ordered.begin(), ordered.end(), [&](uint64_t a, uint64_t b) {
    return _journal->entries.at(a).removal_order > _journal->entries.at(b).removal_order;
  });
  if (std::adjacent_find(ordered.begin(), ordered.end()) != ordered.end()) return false;
  std::vector<uint64_t> restored;
  restored.reserve(ordered.size());
  try {
    for (const auto id : ordered) {
      static_cast<void>(restoreShape(id));
      restored.push_back(id);
    }
  } catch (...) {
    for (auto iterator = restored.rbegin(); iterator != restored.rend(); ++iterator)
      static_cast<void>(removeShape(*iterator));
    throw;
  }
  return !restored.empty();
  };
  const auto result = batch();
  if (result) {
    _context->notifyAfter(DatabaseChange{.domains = DatabaseChangeDomain::kGeometry | DatabaseChangeDomain::kRouting | DatabaseChangeDomain::kPinAccess});
  }
  return result;
}

}  // namespace eccdb
