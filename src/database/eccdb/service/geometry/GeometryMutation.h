// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <array>
#include <cstdint>

#include "common/GeometryTypes.h"
#include "context/DatabaseContext.h"
#include "design/common/DesignTypes.h"
#include "geometry/GeometryEntity.h"

namespace eccdb {

class DesignStore;

namespace detail {

// Captures the small amount of pre-edit state required to keep the derived
// design geometry views current after one canonical Shape mutation.  It owns
// no geometry and never notifies DatabaseContext; the public Service remains
// responsible for one logical notification per edit.
struct GeometryMutation
{
  GeometryShapeId shape;
  GeometryOwner owner;
  Rect old_bounds;
  uint64_t previous_epoch = 0;
  DatabaseChangeDomain domains = DatabaseChangeDomain::kGeometry;
  std::array<DesignNetId, 2> nets{};
  uint8_t net_count = 0;
  DesignInstanceId instance;

  [[nodiscard]] DatabaseChange change(bool permanently_deleted = false) const noexcept;
};

[[nodiscard]] GeometryMutation captureGeometryMutation(DesignStore& design, GeometryShapeId shape);

// Applies the shared derived-data post-step after Storage has completed a
// successful Shape mutation.  Permanent deletion additionally releases the
// R-tree's undo-reserved slot.
void finishGeometryMutation(DesignStore& design,
                            const GeometryMutation& mutation,
                            bool permanently_deleted = false);

// A Fill replacement atomically destroys and materializes an owner collection
// of Shapes.  The current R-tree supports cheap exact maintenance for one
// Shape, not a changing owner collection, so retain correctness by dropping
// only the derived views.  Canonical Fill geometry remains in Storage.
void finishGeometryOwnerReplacement(DesignStore& design, uint64_t previous_epoch) noexcept;

}  // namespace detail
}  // namespace eccdb
