#pragma once

#include "query/GeometryPayloadService.h"

namespace eccdb {

// BBox is only the broad phase. This predicate uses the same detached typed
// primitives as the renderer, including expanded VIA cuts/enclosures.
[[nodiscard]] std::optional<uint64_t> hitTestGeometry(const GeometryPayloadBatch& batch,
                                                     Point point, int32_t tolerance,
                                                     std::span<const uint64_t> layer_order = {});
[[nodiscard]] std::vector<uint64_t> intersectGeometry(const GeometryPayloadBatch& batch, Rect box);

}  // namespace eccdb
