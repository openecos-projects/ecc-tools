// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "view_geometry_transform.h"

#include <algorithm>

#include "IdbInstance.h"
#include "IdbPins.h"
#include "IdbVias.h"

namespace idb {

PlacedTransform ViewGeometryTransform::fromInstance(const IdbInstance* inst)
{
  PlacedTransform transform;
  auto* mutable_inst = const_cast<IdbInstance*>(inst);
  if (mutable_inst == nullptr || mutable_inst->get_coordinate() == nullptr || mutable_inst->get_cell_master() == nullptr) {
    return transform;
  }

  transform.origin = {mutable_inst->get_coordinate()->get_x(), mutable_inst->get_coordinate()->get_y()};
  transform.width = static_cast<int32_t>(mutable_inst->get_cell_master()->get_width());
  transform.height = static_cast<int32_t>(mutable_inst->get_cell_master()->get_height());
  transform.orient = mutable_inst->get_orient();
  return transform;
}

PlacedTransform ViewGeometryTransform::fromIoPin(const IdbPin* pin)
{
  PlacedTransform transform;
  auto* mutable_pin = const_cast<IdbPin*>(pin);
  if (mutable_pin == nullptr || mutable_pin->get_location() == nullptr) {
    return transform;
  }

  transform.origin = {mutable_pin->get_location()->get_x(), mutable_pin->get_location()->get_y()};
  transform.orient = mutable_pin->get_orient();
  return transform;
}

PlacedTransform ViewGeometryTransform::fromVia(const IdbVia* via)
{
  PlacedTransform transform;
  auto* mutable_via = const_cast<IdbVia*>(via);
  if (mutable_via == nullptr || mutable_via->get_coordinate() == nullptr) {
    return transform;
  }

  transform.origin = {mutable_via->get_coordinate()->get_x(), mutable_via->get_coordinate()->get_y()};
  return transform;
}

ViewPoint ViewGeometryTransform::transformPoint(const ViewPoint& local_point, const PlacedTransform& transform)
{
  const int32_t x = local_point.x;
  const int32_t y = local_point.y;
  const int32_t ox = transform.origin.x;
  const int32_t oy = transform.origin.y;
  const int32_t w = transform.width;
  const int32_t h = transform.height;

  switch (transform.orient) {
    case IdbOrient::kS_R180:
      return {ox + w - x, oy + h - y};
    case IdbOrient::kW_R90:
      return {ox + h - y, oy + x};
    case IdbOrient::kE_R270:
      return {ox + y, oy + w - x};
    case IdbOrient::kFN_MY:
      return {ox + w - x, oy + y};
    case IdbOrient::kFS_MX:
      return {ox + x, oy + h - y};
    case IdbOrient::kFW_MX90:
      return {ox + y, oy + x};
    case IdbOrient::kFE_MY90:
      return {ox + h - y, oy + w - x};
    case IdbOrient::kNone:
    case IdbOrient::kN_R0:
    case IdbOrient::kMax:
    default:
      return {ox + x, oy + y};
  }
}

ViewRect ViewGeometryTransform::transformRect(const ViewRect& local_rect, const PlacedTransform& transform)
{
  const ViewPoint low = transformPoint({local_rect.lx, local_rect.ly}, transform);
  const ViewPoint high = transformPoint({local_rect.ux, local_rect.uy}, transform);

  return {std::min(low.x, high.x), std::min(low.y, high.y), std::max(low.x, high.x), std::max(low.y, high.y)};
}

std::vector<ViewRect> ViewGeometryTransform::transformRects(const std::vector<ViewRect>& local_rects, const PlacedTransform& transform)
{
  std::vector<ViewRect> rects;
  rects.reserve(local_rects.size());
  for (const auto& rect : local_rects) {
    rects.push_back(transformRect(rect, transform));
  }
  return rects;
}

ViewLayerShape ViewGeometryTransform::transformLayerShape(const ViewLayerShape& local_shape, const PlacedTransform& transform)
{
  ViewLayerShape shape;
  shape.layer_id = local_shape.layer_id;
  shape.rects = transformRects(local_shape.rects, transform);
  return shape;
}

std::vector<ViewLayerShape> ViewGeometryTransform::transformLayerShapes(const std::vector<ViewLayerShape>& local_shapes,
                                                                        const PlacedTransform& transform)
{
  std::vector<ViewLayerShape> shapes;
  shapes.reserve(local_shapes.size());
  for (const auto& shape : local_shapes) {
    shapes.push_back(transformLayerShape(shape, transform));
  }
  return shapes;
}

}  // namespace idb
