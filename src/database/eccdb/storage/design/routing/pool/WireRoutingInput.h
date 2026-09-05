#pragma once

#include <limits>
#include <vector>

#include "design/routing/pool/RoutingPoolRecords.h"
#include "design/routing/view/WireRoutingView.h"

namespace eccdb {

inline constexpr uint32_t kInvalidDesignWirePathExtraIndex = std::numeric_limits<uint32_t>::max();

// Owning write value used only while building one Pool insertion batch.
struct DesignWirePath
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  int32_t width = 0;
  uint32_t mask = 0;
  std::string taper_rule;
  std::string shape;
  int32_t style = 0;
  std::vector<DesignWirePoint> points;
  std::vector<DesignWireVia> vias;
  std::vector<DesignWireRectangle> rectangles;
};

// One Wire's resolved semantic input. Importers and mutation APIs own this
// temporary batch; it is validated by DesignRoutingStorage and encoded by
// DesignRoutingPool, but is never stored as an EnTT component.
struct DesignWireInputPathRecord
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  DesignWirePoolEnd<DesignWirePoint> point_end;
  DesignWirePoolEnd<DesignWireVia> via_end;
  DesignWirePoolEnd<DesignWireRectangle> rectangle_end;
  uint32_t extra_index = kInvalidDesignWirePathExtraIndex;
};

static_assert(sizeof(DesignWireInputPathRecord) == 24u);

struct DesignWireRoutingInput
{
  std::vector<DesignWireInputPathRecord> path_records;
  std::vector<DesignWirePoint> points;
  std::vector<DesignWireVia> vias;
  std::vector<DesignWireRectangle> rectangles;
  std::vector<DesignWirePathExtra> extras;

  void reservePaths(std::size_t count) { path_records.reserve(count); }
  void appendPath(DesignWirePath path);
  [[nodiscard]] std::size_t pathCount() const noexcept { return path_records.size(); }
  [[nodiscard]] DesignWirePathView path(std::size_t index) const;
};

}  // namespace eccdb
