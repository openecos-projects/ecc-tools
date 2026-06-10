// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#pragma once

#include <cstdint>
#include <vector>

#include "IdbEnum.h"

namespace idb {

struct ViewPoint
{
  int32_t x = 0;
  int32_t y = 0;
};

struct ViewRect
{
  int32_t lx = 0;
  int32_t ly = 0;
  int32_t ux = 0;
  int32_t uy = 0;
};

struct ViewLayerShape
{
  int layer_id = -1;
  std::vector<ViewRect> rects;
};

struct PlacedTransform
{
  ViewPoint origin;
  int32_t width = 0;
  int32_t height = 0;
  IdbOrient orient = IdbOrient::kN_R0;
};

class IdbInstance;
class IdbPin;
class IdbVia;

class ViewGeometryTransform
{
 public:
  static PlacedTransform fromInstance(const IdbInstance* inst);
  static PlacedTransform fromIoPin(const IdbPin* pin);
  static PlacedTransform fromVia(const IdbVia* via);

  static ViewPoint transformPoint(const ViewPoint& local_point, const PlacedTransform& transform);
  static ViewRect transformRect(const ViewRect& local_rect, const PlacedTransform& transform);
  static std::vector<ViewRect> transformRects(const std::vector<ViewRect>& local_rects, const PlacedTransform& transform);
  static ViewLayerShape transformLayerShape(const ViewLayerShape& local_shape, const PlacedTransform& transform);
  static std::vector<ViewLayerShape> transformLayerShapes(const std::vector<ViewLayerShape>& local_shapes,
                                                          const PlacedTransform& transform);
};

}  // namespace idb
