#pragma once

#include <cstdint>

#include "eccdb/Database.h"

namespace eccdb {

// Native, GUI-independent mutation boundary for geometry edits.
class GeometryEditService
{
 public:
  explicit GeometryEditService(Database& database) noexcept : _database(&database) {}

  void moveShape(uint64_t id, int32_t dx, int32_t dy);
  [[nodiscard]] bool destroyShape(uint64_t id);

 private:
  Database* _database;
};

}  // namespace eccdb
