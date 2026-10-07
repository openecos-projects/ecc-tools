// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "library/common/LibraryTypes.h"
#include "query/GeometryPayloadService.h"

namespace eccdb {

class Database;
class DesignStore;
class LibraryStore;
class TechStore;

// A master pin is a reusable LEF PORT. A placed instance pin is a concrete
// occurrence of that PORT beneath one embedded dbITerm. Neither is a
// design-domain GeometryShape, so occurrence payloads never expose a forged
// ShapeId.
enum class GeometryOccurrenceKind : uint8_t
{
  kMasterPin,
  kPlacedInstanceMasterPin,
};

struct GeometryOccurrenceHeader
{
  GeometryOccurrenceKind kind = GeometryOccurrenceKind::kMasterPin;
  uint64_t instance_term = UINT64_MAX;
  uint64_t instance = UINT64_MAX;
  uint64_t master_pin = UINT64_MAX;
  Rect bounds{};
  bool has_bounds = false;
};

// A detached exact-geometry snapshot for selected master-pin or instance-pin
// occurrences. `header_index` in every primitive indexes `headers`; the
// primitive arrays own their coordinate data and remain valid after the
// callback-based PhysicalDesignService traversal returns.
struct GeometryOccurrencePayloadBatch
{
  std::vector<GeometryOccurrenceHeader> headers;
  std::vector<GeometryPayloadRectangle> rectangles;
  std::vector<GeometryPayloadPolygon> polygons;
  std::vector<Point> polygon_points;
  std::vector<GeometryPayloadVia> vias;
};

// Exact geometry for a small number of selected pin occurrences. This is not
// a viewport API: callers must not invoke fetchInstancePin for every instance
// during painting. Whole-chip cell drawing should use a master-local GPU mesh
// cache plus instance transforms instead.
class GeometryOccurrenceService
{
 public:
  explicit GeometryOccurrenceService(Database& database);
  GeometryOccurrenceService(const TechStore& technology, const LibraryStore& library, const DesignStore& design) noexcept;

  [[nodiscard]] GeometryOccurrencePayloadBatch fetchMasterPin(LibraryMasterPinId pin) const;
  // The embedded dbITerm may resolve to several LEF PORT / dbMPin objects.
  // The result therefore has one header per master pin below this iterm.
  [[nodiscard]] GeometryOccurrencePayloadBatch fetchInstancePin(DesignInstanceTermId pin) const;
  // Aggregate the exact placed PORT geometry below a bounded set of dbInsts.
  // This remains an occurrence-only payload: every header is tied to the
  // embedded dbITerm and master dbMPin that produced it, never a fabricated
  // design ShapeId. Callers normally pass the component IDs visible in one
  // viewport rather than every instance in a full chip.
  [[nodiscard]] GeometryOccurrencePayloadBatch fetchInstancePins(std::span<const DesignInstanceId> instances) const;

 private:
  const TechStore* _technology = nullptr;
  const LibraryStore* _library = nullptr;
  const DesignStore* _design = nullptr;
};

}  // namespace eccdb
