// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "IdbDesign.h"
#include "IdbGeometry.h"
#include "IdbNet.h"
#include "RoutingContext.hpp"
#include "ZHHeader.hpp"

namespace izh {

class WireEnvIndex
{
 public:
  void rebuild(idb::IdbDesign* design, const RoutingContext& ctx);
  bool hasOverlap(int layer_order, int32_t lx, int32_t ly, int32_t hx, int32_t hy, idb::IdbNet* skip_net = nullptr) const;
  bool hasViaOverlap(int32_t x, int32_t y, int cut_order, int32_t spacing) const;
  bool inDie(const RoutingContext& ctx, int32_t x, int32_t y) const;
  bool hasInstanceOverlap(int32_t lx, int32_t ly, int32_t hx, int32_t hy) const;
  bool findFreeDiodeSite(const RoutingContext& ctx, int32_t pin_x, int32_t pin_y, int32_t width, int32_t height, int32_t& out_x,
                         int32_t& out_y, idb::IdbOrient& out_orient) const;

 private:
  using Item = std::pair<BGRectInt, idb::IdbNet*>;
  using RTree = bgi::rtree<Item, bgi::quadratic<16>>;
  using InstItem = std::pair<BGRectInt, int>;
  using InstRTree = bgi::rtree<InstItem, bgi::quadratic<16>>;

  std::map<int, RTree> _wire_rtree;
  std::map<int, RTree> _via_rtree;
  InstRTree _inst_rtree;
};

}  // namespace izh
