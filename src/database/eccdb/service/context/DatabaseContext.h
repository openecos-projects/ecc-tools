// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "design/common/DesignTypes.h"
#include "geometry/GeometryEntity.h"

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;

enum class DatabaseChangeDomain : uint32_t
{
  kNone = 0,
  kInstances = 1u << 0,
  kConnectivity = 1u << 1,
  kGeometry = 1u << 2,
  kRouting = 1u << 3,
  kFloorplan = 1u << 4,
  kConstraints = 1u << 5,
  kPinAccess = 1u << 6,
  kAntenna = 1u << 7,
};

[[nodiscard]] constexpr DatabaseChangeDomain operator|(DatabaseChangeDomain lhs, DatabaseChangeDomain rhs) noexcept
{
  return static_cast<DatabaseChangeDomain>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

[[nodiscard]] constexpr bool includes(DatabaseChangeDomain value, DatabaseChangeDomain domain) noexcept
{
  return (static_cast<uint32_t>(value) & static_cast<uint32_t>(domain)) != 0;
}

// Synchronous notifications borrow their ID arrays for the callback duration.
// Deleted IDs describe the change; only beforeChange may inspect old objects.
struct DatabaseChange
{
  DatabaseChangeDomain domains = DatabaseChangeDomain::kNone;
  std::span<const DesignInstanceId> instances;
  std::span<const DesignNetId> nets;
  std::span<const GeometryShapeId> shapes;
  // Canonical children replaced under surviving owners. Detached journals
  // must not restore old children into the new collection.
  std::span<const GeometryOwner> geometry_replacements;
  // Permanent child deletions, distinct from reversible session removal.
  // beforeChange may capture the live owner/ordinal; afterChange commits it.
  std::span<const GeometryShapeId> geometry_deletions;
};

class DatabaseObserver
{
 public:
  virtual ~DatabaseObserver() = default;
  virtual void beforeChange(const DatabaseChange&) noexcept {}
  virtual void afterChange(const DatabaseChange&) noexcept {}
};

// Native, single-threaded service context. Stores and observers are borrowed
// and must outlive their use. Callbacks must not reenter business mutations;
// internal observers may discard detached derived/edit-journal state.
// Optional Tech/Library stores support design-only native tools and fixtures.
class DatabaseContext
{
 public:
  explicit DatabaseContext(DesignStore& design, TechStore* technology = nullptr, LibraryStore* library = nullptr) noexcept;
  DatabaseContext(TechStore& technology, LibraryStore& library, DesignStore& design) noexcept;
  DatabaseContext(const DatabaseContext&) = delete;
  DatabaseContext& operator=(const DatabaseContext&) = delete;

  [[nodiscard]] DesignStore& design() const noexcept { return *_design; }
  [[nodiscard]] TechStore* technology() const noexcept { return _technology; }
  [[nodiscard]] LibraryStore* library() const noexcept { return _library; }
  [[nodiscard]] uint64_t revision() const noexcept { return _revision; }

  void addObserver(DatabaseObserver& observer);
  void removeObserver(DatabaseObserver& observer) noexcept;
  void notifyBefore(const DatabaseChange& change) noexcept;
  void notifyAfter(const DatabaseChange& change) noexcept;

 private:
  DesignStore* _design;
  TechStore* _technology;
  LibraryStore* _library;
  uint64_t _revision = 1;
  std::vector<DatabaseObserver*> _observers;
};

}  // namespace eccdb
