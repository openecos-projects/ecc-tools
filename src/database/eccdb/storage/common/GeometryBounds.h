// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
// ***************************************************************************************
#pragma once

#include <cstdint>

#include "eccdb/Types.h"

namespace eccdb {

// A bounds value can describe geometry in more than one coordinate space.
// Keep the space on the cache so a local master box is never accidentally
// consumed as a placed design box.
enum class BoundsSpace : uint8_t
{
  kLocal,
  kPlaced,
  kGlobal,
};

// Derived spatial data shared by geometry-aware entities.  The component is
// deliberately not part of the binary schema: it can always be rebuilt from
// the canonical geometry and is therefore safe to invalidate on edits.
struct GeometryBounds
{
  Rect rect{};
  BoundsSpace space = BoundsSpace::kLocal;
  uint64_t source_epoch = 0;
  bool valid = false;
  bool dirty = true;

  [[nodiscard]] bool usable() const noexcept { return valid && !dirty && rect.isValid(); }

  void invalidate(uint64_t epoch = 0) noexcept
  {
    source_epoch = epoch;
    valid = false;
    dirty = true;
  }

  void set(Rect value, BoundsSpace value_space, uint64_t epoch = 0) noexcept
  {
    rect = value.normalized();
    space = value_space;
    source_epoch = epoch;
    valid = rect.isValid();
    dirty = false;
  }
};

// Short name for callers that model all spatial metadata as a Bounds
// component while retaining GeometryBounds as the descriptive type name.
using Bounds = GeometryBounds;

}  // namespace eccdb
