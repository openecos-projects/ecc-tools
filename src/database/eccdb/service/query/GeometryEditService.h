#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "eccdb/Database.h"

namespace eccdb {

// Native, GUI-independent mutation boundary for geometry edits.
class GeometryEditService
{
 public:
  explicit GeometryEditService(Database& database);
  ~GeometryEditService();
  GeometryEditService(const GeometryEditService&) = delete;
  GeometryEditService& operator=(const GeometryEditService&) = delete;

  void moveShape(uint64_t id, int32_t dx, int32_t dy);
  [[nodiscard]] bool destroyShape(uint64_t id);
  // Reversible session deletion. A removed Shape reserves its entity slot;
  // its detached payload lives only in this native edit journal. Saving the
  // design sees the owner relation without the removed Shape.
  [[nodiscard]] bool removeShape(uint64_t id);
  [[nodiscard]] bool restoreShape(uint64_t id);
  [[nodiscard]] bool removeShapes(std::span<const uint64_t> ids);
  [[nodiscard]] bool restoreShapes(std::span<const uint64_t> ids);

 private:
  Database* _database;
  struct Journal;
  std::unique_ptr<Journal> _journal;
};

}  // namespace eccdb
