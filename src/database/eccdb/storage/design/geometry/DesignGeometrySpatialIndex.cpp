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

#include "design/geometry/DesignGeometrySpatialIndex.h"

#include <array>
#include <iterator>
#include <boost/iterator/function_output_iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include "design/DesignStore.h"

namespace eccdb {

class DesignGeometrySpatialIndex::CompiledFilter
{
 public:
  explicit CompiledFilter(const GeometryQueryFilter& filter) : _filter(filter)
  {
    const auto design_via_bit = uint32_t{1} << static_cast<uint32_t>(GeometryOwnerDomain::kDesignVia);
    for (const auto& clause : _filter.kind_owner_masks) {
      if (clause.second == design_via_bit) _explicitly_requested_local_via_kinds |= clause.first;
      for (uint32_t owner_bit = 0; owner_bit < _accepted_kinds_by_owner.size(); ++owner_bit) {
        if ((clause.second & (uint32_t{1} << owner_bit)) != 0) {
          _accepted_kinds_by_owner[owner_bit] |= clause.first;
        }
      }
    }
  }

  [[nodiscard]] bool accepts(const Entry& entry) const noexcept
  {
    const auto kind_bit = uint32_t{1} << static_cast<uint32_t>(entry.kind);
    // Via-definition coordinates are local templates, not placed layout.
    // An exact DesignVia-only clause is an explicit request; broad aggregate
    // GUI masks must not accidentally expose template geometry.
    if (entry.owner.domain == GeometryOwnerDomain::kDesignVia && !_filter.include_local_vias
        && _filter.owner_domain != GeometryOwnerDomain::kDesignVia
        && (_explicitly_requested_local_via_kinds & kind_bit) == 0) {
      return false;
    }
    // A VIA owns geometry on multiple layers. It is a spatial candidate for
    // a layer-filtered payload query; expansion applies the exact layer test.
    const bool via = entry.kind == GeometryShapeKind::kVia || entry.kind == GeometryShapeKind::kNetVia
                     || entry.kind == GeometryShapeKind::kPinVia;
    if (!_filter.acceptsLayer(entry.layer) && !via) return false;
    if (!_filter.kind_owner_masks.empty()) {
      const auto owner_bit = static_cast<uint32_t>(entry.owner.domain);
      if (owner_bit >= _accepted_kinds_by_owner.size()
          || (_accepted_kinds_by_owner[owner_bit] & kind_bit) == 0) {
        return false;
      }
    }
    if (_filter.kind_mask != UINT32_MAX) {
      const auto bit = static_cast<uint32_t>(entry.kind);
      if (bit >= 32u || (_filter.kind_mask & (uint32_t{1} << bit)) == 0) return false;
    }
    if (_filter.owner_domain != GeometryOwnerDomain::kNone && entry.owner.domain != _filter.owner_domain) return false;
    if (_filter.owner != GeometryQueryFilter::kUnspecified && entry.owner.value != _filter.owner) return false;
    if (_filter.net != GeometryQueryFilter::kUnspecified && static_cast<uint64_t>(entry.net.packed()) != _filter.net) return false;
    if (_filter.wire != GeometryQueryFilter::kUnspecified && static_cast<uint64_t>(entry.wire.packed()) != _filter.wire) return false;
    return true;
  }

 private:
  const GeometryQueryFilter& _filter;
  uint32_t _explicitly_requested_local_via_kinds = 0;
  std::array<uint32_t, 32> _accepted_kinds_by_owner{};
};

void DesignGeometrySpatialIndex::invalidate() noexcept
{
  _built = false;
  _indexed_epoch = 0;
  _entries.clear();
  _removed_slots.clear();
  _tree.clear();
}

DesignGeometrySpatialIndex::IndexBox DesignGeometrySpatialIndex::indexBox(Rect box) noexcept
{
  return IndexBox{IndexPoint{box.ll_x, box.ll_y}, IndexPoint{box.ur_x, box.ur_y}};
}

Rect DesignGeometrySpatialIndex::rectFromIndexBox(const IndexBox& box) noexcept
{
  return Rect{box.min_corner().x(), box.min_corner().y(), box.max_corner().x(), box.max_corner().y()};
}

void DesignGeometrySpatialIndex::ensureCurrent() const
{
  if (!_built || _indexed_epoch != _design.geometryStorage().geometryEpoch()) {
    rebuildImpl();
  }
}

void DesignGeometrySpatialIndex::rebuild() const
{
  rebuildImpl();
}

DesignGeometrySpatialIndex::Entry DesignGeometrySpatialIndex::entryFor(ShapeId id) const
{
  const auto& geometry = _design.geometryStorage();
  const auto& metadata = geometry.registry().shape(id);
  Entry entry{.id = id, .kind = metadata.kind, .layer = geometry.effectiveLayer(id).packed(),
              .net = {}, .wire = {}, .owner = metadata.owner};
  const auto entity = static_cast<DesignEntity>(metadata.owner.value);
  switch (metadata.owner.domain) {
    case GeometryOwnerDomain::kDesignWire:
      entry.wire = DesignWireId{entity};
      entry.net = _design.routingStorage().wire(entry.wire).net;
      break;
    case GeometryOwnerDomain::kDesignNet:
    case GeometryOwnerDomain::kDesignGuide:
    case GeometryOwnerDomain::kDesignTrackAssignment: entry.net = DesignNetId{entity}; break;
    case GeometryOwnerDomain::kDesignBlockPin: {
      const auto& pin = _design.netlistStorage().blockPin(DesignBlockPinId{entity});
      const auto& term = _design.netlistStorage().blockTerm(pin.term);
      entry.net = term.net ? term.net : term.special_net;
      break;
    }
    default: break;
  }
  return entry;
}

void DesignGeometrySpatialIndex::applyChange(ShapeId id, Rect old_bounds, uint64_t previous_epoch) const
{
  if (!_built || _indexed_epoch != previous_epoch) return;
  std::vector<RTreeValue> candidates;
  _tree.query(boost::geometry::index::intersects(indexBox(old_bounds)), std::back_inserter(candidates));
  std::optional<uint32_t> slot;
  for (const auto& candidate : candidates) {
    if (_entries[candidate.second].id != id) continue;
    slot = candidate.second;
    _tree.remove(candidate);
    break;
  }
  if (_design.geometryStorage().contains(id)) {
    auto entry = entryFor(id);
    if (!slot) {
      if (const auto removed = _removed_slots.find(id.packed()); removed != _removed_slots.end()) {
        slot = removed->second;
        _removed_slots.erase(removed);
      }
    }
    if (!slot) {
      slot = static_cast<uint32_t>(_entries.size());
      _entries.push_back(std::move(entry));
    } else {
      _entries[*slot] = std::move(entry);
    }
    _tree.insert({indexBox(_design.geometryStorage().boundsUncached(id)), *slot});
  } else if (slot) {
    _removed_slots.insert_or_assign(id.packed(), *slot);
    _entries[*slot].id = {};
  }
  _indexed_epoch = _design.geometryStorage().geometryEpoch();
}

void DesignGeometrySpatialIndex::forgetRemovedSlot(ShapeId id) const noexcept
{
  _removed_slots.erase(id.packed());
}

void DesignGeometrySpatialIndex::rebuildImpl() const
{
  _built = false;
  _entries.clear();
  _removed_slots.clear();
  _tree.clear();

  const auto& geometry = _design.geometryStorage();
  const auto view = geometry.registry().registry().view<const GeometryShape>();
  _entries.reserve(view.size());

  // Design geometry entities are unique in the registry. Their owner relation
  // provides the filter metadata directly; a routed Shape obtains its Net
  // from the owning Wire.
  for (const auto entity : view) {
    const auto id = ShapeId{entity};
    Entry entry = entryFor(id);
    _entries.push_back(std::move(entry));
  }

  // Bulk packing avoids N incremental insert/rebalance operations on cold
  // import. Transform the side entries lazily so no second full value array
  // is allocated just to feed the R-tree constructor.
  struct ValueFor {
    const Entry* base;
    const DesignGeometrySpatialIndex* index;
    RTreeValue operator()(const Entry& entry) const {
      return {index->indexBox(index->_design.geometryStorage().boundsUncached(entry.id)),
              static_cast<uint32_t>(&entry - base)};
    }
  };
  const ValueFor value_for{_entries.data(), this};
  _tree = RTree(boost::make_transform_iterator(_entries.begin(), value_for),
                boost::make_transform_iterator(_entries.end(), value_for));

  _indexed_epoch = geometry.geometryEpoch();
  _built = true;
}

std::vector<DesignGeometrySpatialIndex::ShapeId> DesignGeometrySpatialIndex::queryImpl(
    Rect box, const GeometryQueryFilter& filter) const
{
  ensureCurrent();
  if (!box.isValid()) {
    return {};
  }
  std::vector<ShapeId> result;
  // Filter while traversing the R-tree. The old two-step path materialized a
  // full RTreeValue vector before filtering; a whole-chip query could then
  // hold tens of millions of temporary candidates in addition to the tree.
  result.reserve(std::min<std::size_t>(_tree.size(), 4096u));
  const CompiledFilter compiled_filter{filter};
  const auto accept = [&](const RTreeValue& candidate) {
    const auto& entry = _entries[candidate.second];
    if (compiled_filter.accepts(entry)) result.push_back(entry.id);
  };
  _tree.query(boost::geometry::index::intersects(indexBox(box)), boost::make_function_output_iterator(accept));
  return result;
}

std::vector<DesignGeometrySpatialIndex::ShapeId> DesignGeometrySpatialIndex::query(Rect box) const
{
  return queryImpl(box, GeometryQueryFilter{});
}

std::optional<DesignGeometrySpatialIndex::ShapeId> DesignGeometrySpatialIndex::queryFirst(
    Rect box, const GeometryQueryFilter& filter, const ShapePredicate& predicate) const
{
  ensureCurrent();
  if (!box.isValid()) return std::nullopt;
  const CompiledFilter compiled_filter{filter};
  for (auto iterator = _tree.qbegin(boost::geometry::index::intersects(indexBox(box))); iterator != _tree.qend(); ++iterator) {
    const auto& entry = _entries[iterator->second];
    if (!compiled_filter.accepts(entry)) continue;
    if (!predicate || predicate(entry.id)) return entry.id;
  }
  return std::nullopt;
}

bool DesignGeometrySpatialIndex::any(Rect box, const GeometryQueryFilter& filter, const ShapePredicate& predicate) const
{
  return queryFirst(box, filter, predicate).has_value();
}

std::vector<GeometrySpatialRecord> DesignGeometrySpatialIndex::queryRecords(Rect box) const
{
  ensureCurrent();
  if (!box.isValid()) return {};
  std::vector<GeometrySpatialRecord> result;
  result.reserve(std::min<std::size_t>(_tree.size(), 4096u));
  _tree.query(boost::geometry::index::intersects(indexBox(box.normalized())),
              boost::make_function_output_iterator([&](const RTreeValue& candidate) {
                const auto& entry = _entries[candidate.second];
                result.push_back(GeometrySpatialRecord{
                    .id = entry.id,
                    .bounds = rectFromIndexBox(candidate.first),
                    .kind = entry.kind,
                    .layer = entry.layer});
              }));
  return result;
}

std::vector<DesignGeometrySpatialIndex::ShapeId> DesignGeometrySpatialIndex::query(
    Rect box, const GeometryQueryFilter& filter) const
{
  return queryImpl(box, filter);
}

std::vector<DesignGeometrySpatialIndex::ShapeId> DesignGeometrySpatialIndex::query(DesignNetId net, Rect box) const
{
  GeometryQueryFilter filter;
  filter.net = static_cast<uint64_t>(net.packed());
  return queryImpl(box, filter);
}

std::vector<DesignGeometrySpatialIndex::ShapeId> DesignGeometrySpatialIndex::query(DesignWireId wire, Rect box) const
{
  GeometryQueryFilter filter;
  filter.wire = static_cast<uint64_t>(wire.packed());
  return queryImpl(box, filter);
}

std::vector<DesignGeometrySpatialIndex::ShapeId> DesignGeometrySpatialIndex::query(GeometryOwner owner, Rect box) const
{
  GeometryQueryFilter filter;
  filter.owner_domain = owner.domain;
  filter.owner = owner.value;
  return queryImpl(box, filter);
}

}  // namespace eccdb
