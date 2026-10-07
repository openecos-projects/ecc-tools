#include "query/GeometryEditService.h"

#include <stdexcept>
#include <algorithm>
#include <unordered_map>
#include <variant>

#include "api/internal/DatabaseState.h"
#include "design/DesignStore.h"
#include "design/fill/model/FillComponents.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"

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

void maintainViews(DesignStore& design, GeometryShapeId id, Rect old_bounds, uint64_t previous_epoch)
{
  design.geometryIndex().applyChange(id, old_bounds, previous_epoch);
  const auto new_bounds = design.geometryStorage().contains(id)
      ? std::optional<Rect>{design.geometryStorage().bounds(id)} : std::nullopt;
  design.geometryOverview().applyChange(old_bounds, new_bounds, previous_epoch);
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

GeometryEditService::GeometryEditService(Database& database) : _database(&database), _journal(std::make_unique<Journal>()) {}

GeometryEditService::~GeometryEditService()
{
  auto& registry = _database->state().design().geometryStorage().registry().registry();
  for (const auto& [id, entry] : _journal->entries) {
    const auto entity = toShapeId(id).entity();
    if (entry.removed && registry.valid(entity)) {
      registry.destroy(entity);
      _database->state().design().geometryIndex().forgetRemovedSlot(toShapeId(id));
    }
  }
}

void GeometryEditService::moveShape(uint64_t id, int32_t dx, int32_t dy)
{
  if (!_database) throw std::logic_error("GeometryEditService has no database");
  auto& design = _database->state().design();
  const auto shape = toShapeId(id);
  const auto old_bounds = design.geometryStorage().bounds(shape);
  const auto epoch = design.geometryStorage().geometryEpoch();
  design.geometryStorage().moveShape(shape, dx, dy);
  maintainViews(design, shape, old_bounds, epoch);
}

bool GeometryEditService::destroyShape(uint64_t id)
{
  if (!_database) return false;
  auto& design = _database->state().design();
  const auto shape = toShapeId(id);
  if (!design.geometryStorage().contains(shape)) return false;
  const auto old_bounds = design.geometryStorage().bounds(shape);
  const auto epoch = design.geometryStorage().geometryEpoch();
  const auto metadata = design.geometryRegistry().shape(shape);
  const bool has_pending = std::any_of(_journal->entries.begin(), _journal->entries.end(), [&](const auto& saved) {
    return saved.second.removed && Journal::sameSequence(saved.second.metadata, metadata);
  });
  std::size_t ordinal = 0;
  if (has_pending) {
    const auto& sequence = ownerSequence(design, metadata);
    const auto position = std::find(sequence.begin(), sequence.end(), shape);
    ordinal = _journal->ordinalIncludingRemoved(metadata, static_cast<std::size_t>(position - sequence.begin()));
  }
  const auto destroyed = design.geometryStorage().destroy(shape);
  if (destroyed) {
    // A permanent deletion closes a position in the virtual owner sequence;
    // pending journal entries retain their relative order around that gap.
    if (has_pending)
      for (auto& [saved_id, saved] : _journal->entries) {
        static_cast<void>(saved_id);
        if (saved.removed && Journal::sameSequence(saved.metadata, metadata) && saved.ordinal > ordinal) --saved.ordinal;
      }
    maintainViews(design, shape, old_bounds, epoch);
    design.geometryIndex().forgetRemovedSlot(shape);
  }
  return destroyed;
}

bool GeometryEditService::removeShape(uint64_t id)
{
  auto& design = _database->state().design();
  auto& geometry = design.geometryStorage();
  const auto shape = toShapeId(id);
  if (!geometry.contains(shape)) return false;
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
      geometry.bounds(shape), true, _journal->next_removal_order++};
  // Complete all potentially allocating journal work before changing state.
  auto [saved, inserted] = _journal->entries.insert_or_assign(id, std::move(entry));
  static_cast<void>(inserted);
  const auto epoch = geometry.geometryEpoch();
  sequence.erase(position);
  updateOwnerAfterRemoval(design, saved->second.metadata, sequence.empty());
  std::visit([&](const auto& value) { registry.remove<std::remove_cvref_t<decltype(value)>>(shape.entity()); }, saved->second.payload);
  registry.remove<GeometryShape, GeometryBounds, DesignRouteShapeAttributes>(shape.entity());
  design.routingStorage().markGeometryChanged();
  maintainViews(design, shape, saved->second.bounds, epoch);
  return true;
}

bool GeometryEditService::restoreShape(uint64_t id)
{
  const auto iterator = _journal->entries.find(id);
  if (iterator == _journal->entries.end() || !iterator->second.removed) return false;
  auto& design = _database->state().design();
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
  maintainViews(design, shape, saved.bounds, epoch);
  return true;
}

bool GeometryEditService::removeShapes(std::span<const uint64_t> ids)
{
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
}

bool GeometryEditService::restoreShapes(std::span<const uint64_t> ids)
{
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
}

}  // namespace eccdb
