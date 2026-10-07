#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <optional>
#include <unordered_map>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/index/rtree.hpp>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "geometry/GeometryEntity.h"

namespace eccdb {

class DesignStore;

// Optional filters for viewport references. UINT64_MAX means "not set" for
// numeric identifiers; kNone means no owner-domain restriction. The filter
// is evaluated against compact side metadata after the single global R-tree
// produces spatial candidates, so adding a layer filter does not duplicate
// the index for every technology layer.
struct GeometryQueryFilter
{
  static constexpr uint64_t kUnspecified = UINT64_MAX;

  uint64_t layer = kUnspecified;
  // Empty means unrestricted; a nonempty list selects visible Tech layers.
  std::vector<uint64_t> layers;
  // OR of (kind mask, owner-domain mask) clauses, then AND with other fields.
  // This expresses e.g. pin rectangles alongside routed vias in one query.
  // An empty list is unrestricted; a zero mask in a clause matches nothing.
  // Local DesignVia templates stay hidden unless a matching clause uses
  // exactly the DesignVia owner bit, avoiding accidental visibility from a
  // broad GUI owner mask.
  std::vector<std::pair<uint32_t,uint32_t>> kind_owner_masks;
  uint32_t kind_mask = UINT32_MAX;
  GeometryOwnerDomain owner_domain = GeometryOwnerDomain::kNone;
  uint64_t owner = kUnspecified;
  uint64_t net = kUnspecified;
  uint64_t wire = kUnspecified;
  bool include_local_vias = false;

  [[nodiscard]] bool acceptsLayer(uint64_t value) const noexcept {
    return (layer == kUnspecified || layer == value)
        && (layers.empty() || std::find(layers.begin(), layers.end(), value) != layers.end());
  }
};

// Lightweight records for derived overview construction.  The R-tree already
// owns the exact BBox used for a shape, so callers that only need spatial
// summaries should consume this record instead of resolving each typed payload
// and recomputing its bounds.
struct GeometrySpatialRecord
{
  GeometryShapeId id;
  Rect bounds{};
  GeometryShapeKind kind = GeometryShapeKind::kInvalid;
  uint64_t layer = GeometryQueryFilter::kUnspecified;
};

// Rebuildable spatial view over the canonical GeometryRegistry. The R-tree
// owns only Shape IDs, cached bounding boxes, and the Net/Wire relation needed
// for filtering. Geometry payloads remain in the registry.
//
// Edits increment the routing geometry epoch. The next query rebuilds this
// derived index, matching KLayout's derived-and-lazy spatial-index model.
class DesignGeometrySpatialIndex
{
 public:
  using ShapeId = GeometryShapeId;

  explicit DesignGeometrySpatialIndex(const DesignStore& design) noexcept : _design(design) {}

  DesignGeometrySpatialIndex(const DesignGeometrySpatialIndex&) = delete;
  DesignGeometrySpatialIndex& operator=(const DesignGeometrySpatialIndex&) = delete;

  void invalidate() noexcept;
  void rebuild() const;
  // A known single-Shape edit can maintain a warm index. Unknown external
  // edits still invalidate by epoch and use the normal full rebuild path.
  void applyChange(ShapeId id, Rect old_bounds, uint64_t previous_epoch) const;
  // A session removal keeps its vacant side-array slot for undo. Permanent
  // deletion callers release that bookkeeping after applying their edit.
  void forgetRemovedSlot(ShapeId id) const noexcept;

  [[nodiscard]] bool isBuilt() const noexcept { return _built; }
  [[nodiscard]] uint64_t indexedEpoch() const noexcept { return _indexed_epoch; }
  [[nodiscard]] std::size_t indexedShapeCount() const noexcept { return _tree.size(); }
  [[nodiscard]] std::size_t pendingRestoreSlotCount() const noexcept { return _removed_slots.size(); }

  [[nodiscard]] std::vector<ShapeId> query(Rect box) const;
  [[nodiscard]] std::vector<ShapeId> query(Rect box, const GeometryQueryFilter& filter) const;
  [[nodiscard]] std::vector<GeometrySpatialRecord> queryRecords(Rect box) const;
  [[nodiscard]] std::vector<ShapeId> query(DesignNetId net, Rect box) const;
  [[nodiscard]] std::vector<ShapeId> query(DesignWireId wire, Rect box) const;
  [[nodiscard]] std::vector<ShapeId> query(GeometryOwner owner, Rect box) const;

 private:
  using IndexPoint = boost::geometry::model::d2::point_xy<int32_t>;
  using IndexBox = boost::geometry::model::box<IndexPoint>;

  struct Entry
  {
    ShapeId id;
    GeometryShapeKind kind = GeometryShapeKind::kInvalid;
    uint64_t layer = GeometryQueryFilter::kUnspecified;
    // The spatial box is owned by the R-tree value. Keeping another Rect in
    // every side entry duplicated 32 bytes for every shape and made the cold
    // index several GiB larger on test10. Query predicates use the exact
    // same box that was inserted into the tree, so a second copy is not
    // needed here.
    DesignNetId net;
    DesignWireId wire;
    GeometryOwner owner;
  };

  // Keep only a compact entry index in the R-tree. The relation/filter
  // metadata stays in one contiguous side array and is not duplicated in
  // every tree value.
  using RTreeValue = std::pair<IndexBox, uint32_t>;
  using RTree = boost::geometry::index::rtree<RTreeValue, boost::geometry::index::quadratic<16>>;

  void ensureCurrent() const;
  void rebuildImpl() const;
  [[nodiscard]] Entry entryFor(ShapeId id) const;
  [[nodiscard]] static IndexBox indexBox(Rect box) noexcept;
  [[nodiscard]] static Rect rectFromIndexBox(const IndexBox& box) noexcept;
  [[nodiscard]] std::vector<ShapeId> queryImpl(Rect box, const GeometryQueryFilter& filter) const;

  const DesignStore& _design;
  mutable bool _built = false;
  mutable uint64_t _indexed_epoch = 0;
  mutable std::vector<Entry> _entries;
  // Session-deleted entries retain their side slot until undo or an explicit
  // permanent-deletion release. Reusing it on undo avoids a full-index rebuild.
  mutable std::unordered_map<uint64_t, uint32_t> _removed_slots;
  mutable RTree _tree;
};

}  // namespace eccdb
