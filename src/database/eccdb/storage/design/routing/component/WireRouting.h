#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "design/routing/view/WireRoutingView.h"

namespace eccdb {

inline constexpr uint32_t kInvalidDesignWirePathExtraIndex = std::numeric_limits<uint32_t>::max();

// Convenient owning value for constructing one path before flattening it into
// a Wire's routing vectors.
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

struct DesignWireRoutingRange
{
  uint32_t begin = 0;
  uint32_t count = 0;
};

// One path indexes Wire-local flat vectors. Every range is self-contained, so
// paths can be inspected without consulting an adjacent record.
struct DesignWirePathRecord
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  DesignWireRoutingRange points;
  DesignWireRoutingRange vias;
  DesignWireRoutingRange rectangles;
  uint32_t extra_index = kInvalidDesignWirePathExtraIndex;
};

// Persistent routing payload owned by one DesignWire component. Primitives are
// semantic values in flat vectors and share the Wire entity's lifetime.
struct DesignWireRouting
{
  std::vector<DesignWirePathRecord> paths;
  std::vector<DesignWirePoint> points;
  std::vector<DesignWireVia> vias;
  std::vector<DesignWireRectangle> rectangles;
  std::vector<DesignWirePathExtra> extras;

  void reservePaths(std::size_t count) { paths.reserve(count); }
  void appendPath(DesignWirePath path);
  void shrinkToFit();
  [[nodiscard]] std::size_t pathCount() const noexcept { return paths.size(); }
  [[nodiscard]] DesignWirePathView path(std::size_t index) const;
};

using DesignWireRoutingInput = DesignWireRouting;

}  // namespace eccdb
