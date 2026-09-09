#pragma once

#include <cstdint>

#include "design/routing/view/RoutingViewTypes.h"

namespace eccdb {

template <typename T>
struct DesignWirePoolEnd
{
  uint32_t value = 0;
};

struct DesignRoutingPointRecord
{
  Point position;
};

static_assert(sizeof(DesignRoutingPointRecord) == 8u);

struct DesignRoutingPointExtraEntry
{
  uint32_t point_index = 0;
  uint32_t flags = 0;
  int32_t extension = 0;
};

namespace DesignRoutingViaReferenceKind {
constexpr uint8_t kTech = 0;
constexpr uint8_t kDesign = 1;
}  // namespace DesignRoutingViaReferenceKind

struct DesignRoutingViaMeta
{
  uint8_t orientation = 0;
  uint8_t flags = 0;
  uint8_t reference_kind = DesignRoutingViaReferenceKind::kTech;
  uint8_t reserved = 0;
};

struct DesignRoutingViaRecord
{
  uint32_t point_index = 0;
  DesignRoutingViaMeta meta;
  uint64_t reference = 0;
};

static_assert(sizeof(DesignRoutingViaRecord) == 16u);

struct DesignRoutingViaExtraEntry
{
  uint32_t via_index = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
  uint32_t rows = 1;
  uint32_t columns = 1;
  int32_t step_x = 0;
  int32_t step_y = 0;
};

struct DesignRoutingPathMeta
{
  uint16_t layer_ordinal = 0;
  uint8_t flags = 0;
  uint8_t reserved = 0;
};

// Design-wide path records and primitive pools are appended in the same
// order. The previous physical record therefore supplies every begin offset.
struct DesignRoutingPathRecord
{
  DesignRoutingPathMeta meta;
  DesignWirePoolEnd<DesignWirePoint> point_end;
  DesignWirePoolEnd<DesignWireVia> via_end;
  DesignWirePoolEnd<DesignWireRectangle> rectangle_end;
};

static_assert(sizeof(DesignRoutingPathRecord) == 16u);

struct DesignRoutingPathExtraEntry
{
  uint32_t path_index = 0;
  DesignWirePathExtra value;
};

}  // namespace eccdb
