// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <vector>

#include "design/routing/view/WireRoutingView.h"

namespace eccdb {

// Parser/export adapter value, never a database component. Canonical geometry
// consists of independent DesignRouteShape entities, not a path program.
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

  [[nodiscard]] DesignWirePathView view() const;
};

struct DesignWireRoutingInput
{
  std::vector<DesignWirePath> paths;
  void reservePaths(std::size_t count) { paths.reserve(count); }
  void appendPath(DesignWirePath path) { paths.push_back(std::move(path)); }
  void shrinkToFit() { paths.shrink_to_fit(); }
  [[nodiscard]] std::size_t pathCount() const noexcept { return paths.size(); }
  [[nodiscard]] DesignWirePathView path(std::size_t index) const { return paths.at(index).view(); }
};

// Compatibility spelling for temporary construction values only.
using DesignWireRouting = DesignWireRoutingInput;
[[nodiscard]] DesignWirePathView ownedWirePathView(DesignWirePath path);

}  // namespace eccdb
