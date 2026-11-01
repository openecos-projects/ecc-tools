// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "context/DatabaseContext.h"
#include "design/fill/model/FillComponents.h"

namespace eccdb {

// Native, GUI-independent mutation boundary for geometry edits.
class GeometryEditService : private DatabaseObserver
{
 public:
  explicit GeometryEditService(DatabaseContext& context);
  ~GeometryEditService();
  GeometryEditService(const GeometryEditService&) = delete;
  GeometryEditService& operator=(const GeometryEditService&) = delete;

  void moveShape(uint64_t id, int32_t dx, int32_t dy);
  [[nodiscard]] bool destroyShape(uint64_t id);

  // Fill is a design-owned aggregate of rectangular Shapes.  These commands
  // preserve the Fill identity on update and leave all typed geometry in the
  // canonical Fill Storage; GeometryEdit only maintains derived views and the
  // owner-level edit/journal boundary.
  [[nodiscard]] DesignFillId createFill(DesignFillInput fill);
  void updateFill(DesignFillId id, DesignFillInput fill);
  [[nodiscard]] bool destroyFill(DesignFillId id);

  // Reversible session deletion. A removed Shape reserves its entity slot;
  // its detached payload lives only in this native edit journal. Saving the
  // design sees the owner relation without the removed Shape.
  [[nodiscard]] bool removeShape(uint64_t id);
  [[nodiscard]] bool restoreShape(uint64_t id);
  [[nodiscard]] bool removeShapes(std::span<const uint64_t> ids);
  [[nodiscard]] bool restoreShapes(std::span<const uint64_t> ids);

 private:
  void beforeChange(const DatabaseChange& change) noexcept override;
  void afterChange(const DatabaseChange& change) noexcept override;
  void notifyChange(const DatabaseChange& change) noexcept;
  DatabaseContext* _context;
  uint32_t _batch_depth = 0;
  struct Journal;
  std::unique_ptr<Journal> _journal;
};

}  // namespace eccdb
