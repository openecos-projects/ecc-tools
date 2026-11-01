// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "DRCInterfaceEccdb.h"
#include "DRCInterface.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <entt/container/dense_map.hpp>

#include "CutLayer.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Direction.hpp"
#include "EndOfLineSpacingRule.hpp"
#include "RoutingLayer.hpp"
#include "ViolationType.hpp"
#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "geometry/PolygonRectDecomposer.h"
#include "library/LibraryStore.h"
#include "service/geometry/PhysicalDesignService.h"
#include "tech/TechStore.h"

namespace idrc {
namespace {

struct EccdbLayerIndex
{
  int32_t wrap_idx = -1;
  int32_t order = -1;
  bool routing = false;
  bool cut = false;
};

class EccdbLayerTable
{
 public:
  explicit EccdbLayerTable(const eccdb::TechStore& tech)
  {
    int32_t routing_idx = 0;
    int32_t cut_idx = 0;
    const auto& sequence = tech.layerSequence();
    for (int32_t order = 0; order < static_cast<int32_t>(sequence.size()); ++order) {
      const auto layer = sequence[static_cast<size_t>(order)];
      EccdbLayerIndex index;
      index.order = order;
      const auto routing_id = eccdb::TechRoutingLayerId{layer.entity()};
      const auto cut_id = eccdb::TechCutLayerId{layer.entity()};
      if (tech.routingLayerStorage().contains(routing_id)) {
        index.routing = true;
        index.wrap_idx = routing_idx++;
      } else if (tech.cutLayerStorage().contains(cut_id)) {
        index.cut = true;
        index.wrap_idx = cut_idx++;
      }
      _by_entity.emplace(static_cast<uint32_t>(layer.packed()), index);
    }
  }

  [[nodiscard]] const EccdbLayerIndex* find(eccdb::TechLayerId layer) const
  {
    const auto it = _by_entity.find(static_cast<uint32_t>(layer.packed()));
    return it == _by_entity.end() ? nullptr : &it->second;
  }

  [[nodiscard]] int32_t routingWrapIdx(eccdb::TechRoutingLayerId layer) const
  {
    const auto* index = find(eccdb::TechLayerId{layer.entity()});
    return (index != nullptr && index->routing) ? index->wrap_idx : -1;
  }

 private:
  entt::dense_map<uint32_t, EccdbLayerIndex> _by_entity;
};

int32_t effectiveMinWidth(const eccdb::TechRoutingLayer& tech_layer)
{
  // iDB lef_read defaults omitted MINWIDTH to WIDTH.
  return (tech_layer.flags & eccdb::TechRoutingLayerFlag::kHasMinWidth) != 0 ? tech_layer.min_width : tech_layer.width;
}

Direction directionFromTech(eccdb::TechRoutingDirection direction)
{
  switch (direction) {
    case eccdb::TechRoutingDirection::kHorizontal:
      return Direction::kHorizontal;
    case eccdb::TechRoutingDirection::kVertical:
      return Direction::kVertical;
    default:
      return Direction::kOblique;
  }
}

bool isPadMaster(eccdb::LibraryCellMasterType type)
{
  return type >= eccdb::LibraryCellMasterType::kPad && type <= eccdb::LibraryCellMasterType::kPadAreaIo;
}

bool isBlockMaster(eccdb::LibraryCellMasterType type)
{
  return type == eccdb::LibraryCellMasterType::kBlock || type == eccdb::LibraryCellMasterType::kBlockBlackbox
         || type == eccdb::LibraryCellMasterType::kBlockSoft;
}

void transformCoordinate(int32_t& x, int32_t& y, eccdb::DesignOrientation orient, eccdb::Point origin, int32_t width,
                         int32_t height)
{
  switch (orient) {
    case eccdb::DesignOrientation::kN:
      return;
    case eccdb::DesignOrientation::kW: {
      const int32_t dx = x - origin.x;
      const int32_t dy = y - origin.y;
      x = -dy + height + origin.x;
      y = dx + origin.y;
      return;
    }
    case eccdb::DesignOrientation::kS: {
      const int32_t dx = x - origin.x;
      const int32_t dy = y - origin.y;
      x = -dx + width + origin.x;
      y = -dy + height + origin.y;
      return;
    }
    case eccdb::DesignOrientation::kE: {
      const int32_t dx = x - origin.x;
      const int32_t dy = y - origin.y;
      x = dy + origin.x;
      y = -dx + width + origin.y;
      return;
    }
    case eccdb::DesignOrientation::kFN: {
      const int32_t dx = x - origin.x;
      const int32_t dy = y - origin.y;
      x = -dx + width + origin.x;
      y = dy + origin.y;
      return;
    }
    case eccdb::DesignOrientation::kFS: {
      const int32_t dx = x - origin.x;
      const int32_t dy = y - origin.y;
      x = dx + origin.x;
      y = -dy + height + origin.y;
      return;
    }
    case eccdb::DesignOrientation::kFW: {
      const int32_t dx = x - origin.x;
      const int32_t dy = y - origin.y;
      x = dy + origin.x;
      y = dx + origin.y;
      return;
    }
    case eccdb::DesignOrientation::kFE: {
      const int32_t dx = x - origin.x;
      const int32_t dy = y - origin.y;
      x = -dy + height + origin.x;
      y = -dx + width + origin.y;
      return;
    }
  }
}

eccdb::Rect transformRect(eccdb::Rect rect, eccdb::DesignOrientation orient, eccdb::Point origin,
                                  int32_t width, int32_t height)
{
  int32_t ll_x = rect.ll_x;
  int32_t ll_y = rect.ll_y;
  int32_t ur_x = rect.ur_x;
  int32_t ur_y = rect.ur_y;
  transformCoordinate(ll_x, ll_y, orient, origin, width, height);
  transformCoordinate(ur_x, ur_y, orient, origin, width, height);
  return eccdb::Rect{.ll_x = std::min(ll_x, ur_x),
                             .ll_y = std::min(ll_y, ur_y),
                             .ur_x = std::max(ll_x, ur_x),
                             .ur_y = std::max(ll_y, ur_y)};
}

eccdb::Rect placeMasterRect(eccdb::Rect local, eccdb::Point origin, eccdb::DesignOrientation orient,
                                    int32_t width, int32_t height)
{
  return transformRect(local.offset(origin.x, origin.y), orient, origin, width, height);
}

eccdb::Point placeMasterPoint(eccdb::Point local, eccdb::Point origin, eccdb::DesignOrientation orient,
                                      int32_t width, int32_t height)
{
  eccdb::Point placed{local.x + origin.x, local.y + origin.y};
  transformCoordinate(placed.x, placed.y, orient, origin, width, height);
  return placed;
}

template <typename GeometryStorage>
std::vector<eccdb::Rect> geometryRects(const GeometryStorage& storage,
                                       std::span<const eccdb::GeometryShapeId> shapes)
{
  std::vector<eccdb::Rect> rects;
  for (const auto shape : shapes) {
    const auto& metadata = storage.geometryRegistry().shape(shape);
    if (metadata.kind == eccdb::GeometryShapeKind::kRectangle) {
      rects.push_back(storage.rectangle(shape).rectangle);
    } else if (metadata.kind == eccdb::GeometryShapeKind::kPolygon) {
      const auto decomposed = eccdb::decomposePolygonToRectangles(storage.polygonPoints(shape));
      rects.insert(rects.end(), decomposed.begin(), decomposed.end());
    }
  }
  return rects;
}

eccdb::Rect boundingBox(const std::vector<eccdb::Rect>& rects)
{
  eccdb::Rect box;
  bool first = true;
  for (const auto& rect : rects) {
    box = first ? rect : box.united(rect);
    first = false;
  }
  return box;
}

struct LayeredRect
{
  eccdb::TechLayerId layer;
  eccdb::Rect rect;
};

struct ViaShapes
{
  std::vector<LayeredRect> routing;
  std::vector<LayeredRect> cut;
};

ViaShapes techViaShapes(const eccdb::TechStore& tech, eccdb::TechViaMasterId via_id, eccdb::Point origin)
{
  ViaShapes shapes;
  const auto& geometry = tech.viaMasterStorage().geometry(via_id);
  auto append = [&](eccdb::TechLayerId layer, const std::vector<eccdb::Rect>& rects, bool cut) {
    for (const auto& rect : rects) {
      LayeredRect placed{.layer = layer, .rect = rect.offset(origin.x, origin.y)};
      if (cut) {
        shapes.cut.push_back(placed);
      } else {
        shapes.routing.push_back(placed);
      }
    }
  };
  append(geometry.bottom_layer.layer(), geometryRects(tech.geometryStorage(), geometry.bottom_geometry), false);
  append(eccdb::TechLayerId{geometry.cut_layer.entity()}, geometryRects(tech.geometryStorage(), geometry.cut_geometry), true);
  append(geometry.top_layer.layer(), geometryRects(tech.geometryStorage(), geometry.top_geometry), false);
  return shapes;
}

ViaShapes generatedViaShapes(const eccdb::DesignVia& via, eccdb::Point origin)
{
  ViaShapes shapes;
  const auto& generated = via.generated;
  const int32_t rows = static_cast<int32_t>(std::max(generated.row_count, 1u));
  const int32_t columns = static_cast<int32_t>(std::max(generated.column_count, 1u));
  const int32_t origin_x = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOrigin) != 0 ? generated.origin.x : 0;
  const int32_t origin_y = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOrigin) != 0 ? generated.origin.y : 0;
  const int32_t bottom_offset_x
      = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOffset) != 0 ? generated.bottom_offset.x : 0;
  const int32_t bottom_offset_y
      = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOffset) != 0 ? generated.bottom_offset.y : 0;
  const int32_t top_offset_x
      = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOffset) != 0 ? generated.top_offset.x : 0;
  const int32_t top_offset_y
      = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOffset) != 0 ? generated.top_offset.y : 0;
  const int32_t cut_width_total = columns * generated.cut_size_x + (columns - 1) * generated.cut_spacing_x;
  const int32_t cut_height_total = rows * generated.cut_size_y + (rows - 1) * generated.cut_spacing_y;
  const int32_t ll_x_min = (-cut_width_total / 2) + origin_x;
  const int32_t ll_y_min = (-cut_height_total / 2) + origin_y;

  auto append = [&](eccdb::TechLayerId layer, eccdb::Rect rect, bool cut) {
    LayeredRect placed{.layer = layer, .rect = rect.offset(origin.x, origin.y)};
    if (cut) {
      shapes.cut.push_back(placed);
    } else {
      shapes.routing.push_back(placed);
    }
  };

  for (int32_t row = 0; row < rows; ++row) {
    for (int32_t column = 0; column < columns; ++column) {
      const int32_t ll_x = ll_x_min + column * (generated.cut_size_x + generated.cut_spacing_x);
      const int32_t ll_y = ll_y_min + row * (generated.cut_size_y + generated.cut_spacing_y);
      append(eccdb::TechLayerId{generated.cut_layer.entity()},
             eccdb::Rect{.ll_x = ll_x, .ll_y = ll_y, .ur_x = ll_x + generated.cut_size_x, .ur_y = ll_y + generated.cut_size_y},
             true);
    }
  }

  const eccdb::Rect cut_box{.ll_x = ll_x_min,
                                    .ll_y = ll_y_min,
                                    .ur_x = ll_x_min + cut_width_total,
                                    .ur_y = ll_y_min + cut_height_total};
  append(eccdb::TechLayerId{generated.bottom_layer.entity()},
         eccdb::Rect{.ll_x = cut_box.ll_x - generated.bottom_enclosure_x + bottom_offset_x,
                     .ll_y = cut_box.ll_y - generated.bottom_enclosure_y + bottom_offset_y,
                     .ur_x = cut_box.ur_x + generated.bottom_enclosure_x + bottom_offset_x,
                     .ur_y = cut_box.ur_y + generated.bottom_enclosure_y + bottom_offset_y},
         false);
  append(eccdb::TechLayerId{generated.top_layer.entity()},
         eccdb::Rect{.ll_x = cut_box.ll_x - generated.top_enclosure_x + top_offset_x,
                     .ll_y = cut_box.ll_y - generated.top_enclosure_y + top_offset_y,
                     .ur_x = cut_box.ur_x + generated.top_enclosure_x + top_offset_x,
                     .ur_y = cut_box.ur_y + generated.top_enclosure_y + top_offset_y},
         false);
  return shapes;
}

ViaShapes designViaShapes(const eccdb::DesignStore& design, const eccdb::TechStore& tech,
                          eccdb::DesignViaId via_id, eccdb::Point origin)
{
  const auto& via = design.routingStorage().via(via_id);
  if ((via.flags & eccdb::DesignViaFlag::kGenerated) != 0) {
    return generatedViaShapes(via, origin);
  }

  ViaShapes shapes;
  const auto& geometry = design.geometryRegistry();
  auto append_rect = [&](eccdb::TechLayerId layer, eccdb::Rect rect) {
    LayeredRect placed{.layer = layer, .rect = rect.offset(origin.x, origin.y)};
    if (tech.cutLayerStorage().contains(eccdb::TechCutLayerId{layer.entity()})) {
      shapes.cut.push_back(placed);
    } else {
      shapes.routing.push_back(placed);
    }
  };
  for (const auto id : via.rectangles) {
    const auto& rect = geometry.get<eccdb::DesignViaRectangle>(id);
    append_rect(rect.layer, rect.rectangle);
  }
  for (const auto id : via.polygons) {
    const auto& polygon = geometry.get<eccdb::DesignViaPolygon>(id);
    for (const auto& rect : eccdb::decomposePolygonToRectangles(polygon.points)) {
      append_rect(polygon.layer, rect);
    }
  }
  return shapes;
}

ViaShapes resolveViaShapes(const eccdb::DesignStore& design, const eccdb::TechStore& tech,
                           eccdb::TechViaMasterId tech_via, eccdb::DesignViaId design_via, eccdb::Point origin)
{
  if (tech_via) {
    return techViaShapes(tech, tech_via, origin);
  }
  if (design_via) {
    return designViaShapes(design, tech, design_via, origin);
  }
  return {};
}

eccdb::Rect wireSegmentBox(eccdb::Point a, eccdb::Point b, int32_t width)
{
  if (a.y == b.y) {
    return eccdb::Rect{.ll_x = std::min(a.x, b.x),
                               .ll_y = a.y - width / 2,
                               .ur_x = std::max(a.x, b.x),
                               .ur_y = a.y - width / 2 + width};
  }
  return eccdb::Rect{.ll_x = a.x - width / 2,
                             .ll_y = std::min(a.y, b.y),
                             .ur_x = a.x - width / 2 + width,
                             .ur_y = std::max(a.y, b.y)};
}

eccdb::Rect enlargedWireSegmentBox(eccdb::Point a, eccdb::Point b, int32_t half_width)
{
  return eccdb::Rect{.ll_x = std::min(a.x, b.x),
                             .ll_y = std::min(a.y, b.y),
                             .ur_x = std::max(a.x, b.x),
                             .ur_y = std::max(a.y, b.y)}
      .enlarged(half_width);
}

eccdb::Rect regularWireSegmentBox(const eccdb::DesignRouteSegment& segment, int32_t half_width)
{
  const auto& begin = segment.begin;
  const auto& end = segment.end;
  const auto endpoint_extension = [half_width](const eccdb::DesignWirePoint& point) {
    return (point.flags & eccdb::DesignWirePointFlag::kHasExtension) != 0u ? point.extension : half_width;
  };

  if (begin.position.y == end.position.y && begin.position.x != end.position.x) {
    const bool forward = begin.position.x < end.position.x;
    const int32_t lower_extension = endpoint_extension(forward ? begin : end);
    const int32_t upper_extension = endpoint_extension(forward ? end : begin);
    return eccdb::Rect{.ll_x = std::min(begin.position.x, end.position.x) - lower_extension,
                       .ll_y = begin.position.y - half_width,
                       .ur_x = std::max(begin.position.x, end.position.x) + upper_extension,
                       .ur_y = begin.position.y + half_width};
  }
  if (begin.position.x == end.position.x && begin.position.y != end.position.y) {
    const bool forward = begin.position.y < end.position.y;
    const int32_t lower_extension = endpoint_extension(forward ? begin : end);
    const int32_t upper_extension = endpoint_extension(forward ? end : begin);
    return eccdb::Rect{.ll_x = begin.position.x - half_width,
                       .ll_y = std::min(begin.position.y, end.position.y) - lower_extension,
                       .ur_x = begin.position.x + half_width,
                       .ur_y = std::max(begin.position.y, end.position.y) + upper_extension};
  }

  // Preserve the prior broad-phase behavior for zero-length and unsupported
  // oblique segments. DEF flush extensions describe a path direction, which
  // those cases do not provide.
  return enlargedWireSegmentBox(begin.position, end.position, half_width);
}

struct NetSkipInfo
{
  bool skip = false;
  int32_t port_pin_count = 0;
};

using NetSkipCache = entt::dense_map<uint32_t, NetSkipInfo>;
using NetIndexMap = entt::dense_map<uint32_t, int32_t>;

NetSkipInfo analyzeNetSkip(const eccdb::DesignStore& design, const eccdb::LibraryStore& library,
                           eccdb::DesignNetId net)
{
  NetSkipInfo info;
  if (!net) {
    info.skip = true;
    return info;
  }
  const auto& netlist = design.netlistStorage();
  const auto io_pins = netlist.ioPins(net);
  const auto instance_pins = netlist.instancePins(net);
  const bool has_io_pin = io_pins.size() == 1;
  bool has_io_cell = false;
  if (instance_pins.size() == 1) {
    const auto& instance = netlist.instance(netlist.instancePin(instance_pins.front()).instance);
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    has_io_cell = isPadMaster(master.type);
  }
  if (has_io_pin && has_io_cell) {
    info.skip = true;
    return info;
  }

  for (const auto pin_id : instance_pins) {
    const auto& pin = netlist.instancePin(pin_id);
    if (library.masterTermStorage().masterTerm(pin.master_term).ports.empty()) {
      continue;
    }
    ++info.port_pin_count;
  }
  for (const auto pin_id : io_pins) {
    if (netlist.blockPins(pin_id).empty()) {
      continue;
    }
    ++info.port_pin_count;
  }
  info.skip = info.port_pin_count <= 1;
  return info;
}

bool netIsSkipping(const eccdb::DesignStore& design, const eccdb::LibraryStore& library, eccdb::DesignNetId net,
                   NetSkipCache& cache)
{
  if (!net) {
    return true;
  }
  const auto key = static_cast<uint32_t>(net.packed());
  auto it = cache.find(key);
  if (it == cache.end()) {
    it = cache.emplace(key, analyzeNetSkip(design, library, net)).first;
  }
  return it->second.skip;
}

NetIndexMap netIndexMap(const eccdb::DesignStore& design)
{
  NetIndexMap map;
  int32_t index = 0;
  for (const auto net_id : design.netlistStorage().regularNets()) {
    map.emplace(static_cast<uint32_t>(net_id.packed()), index++);
  }
  for (const auto net_id : design.netlistStorage().specialNets()) {
    map.emplace(static_cast<uint32_t>(net_id.packed()), index++);
  }
  return map;
}

int32_t netIndex(eccdb::DesignNetId net, const NetIndexMap& net_index_map)
{
  if (!net) {
    return -1;
  }
  const auto it = net_index_map.find(static_cast<uint32_t>(net.packed()));
  return it == net_index_map.end() ? -1 : it->second;
}

eccdb::DesignNetId instancePinNet(const eccdb::DesignInstancePin& pin)
{
  return pin.net;
}

eccdb::DesignNetId ioPinNet(const eccdb::DesignIoPin& pin)
{
  return pin.net;
}

void appendShape(std::vector<ids::Shape>& shape_list, const EccdbLayerTable& layers, eccdb::TechLayerId layer,
                 eccdb::Rect rect, int32_t net_idx)
{
  const auto* index = layers.find(layer);
  if (index == nullptr || index->wrap_idx < 0) {
    return;
  }
  ids::Shape shape;
  shape.net_idx = net_idx;
  shape.ll_x = rect.ll_x;
  shape.ll_y = rect.ll_y;
  shape.ur_x = rect.ur_x;
  shape.ur_y = rect.ur_y;
  shape.layer_idx = index->wrap_idx;
  shape.is_routing = index->routing;
  shape_list.push_back(shape);
}

void appendViaShapes(std::vector<ids::Shape>& shape_list, const EccdbLayerTable& layers, const ViaShapes& shapes, int32_t net_idx)
{
  for (const auto& shape : shapes.routing) {
    appendShape(shape_list, layers, shape.layer, shape.rect, net_idx);
  }
  for (const auto& shape : shapes.cut) {
    appendShape(shape_list, layers, shape.layer, shape.rect, net_idx);
  }
}

void appendViaRoutingBoundingBoxes(std::vector<ids::Shape>& shape_list, const EccdbLayerTable& layers, const ViaShapes& shapes, int32_t net_idx)
{
  std::vector<std::pair<eccdb::TechLayerId, std::vector<eccdb::Rect>>> routing_by_layer;
  for (const auto& shape : shapes.routing) {
    auto it = std::find_if(routing_by_layer.begin(), routing_by_layer.end(),
                           [&](const auto& entry) { return entry.first == shape.layer; });
    if (it == routing_by_layer.end()) {
      routing_by_layer.push_back({shape.layer, {shape.rect}});
    } else {
      it->second.push_back(shape.rect);
    }
  }
  for (const auto& [layer, rects] : routing_by_layer) {
    appendShape(shape_list, layers, layer, boundingBox(rects), net_idx);
  }
  for (const auto& shape : shapes.cut) {
    appendShape(shape_list, layers, shape.layer, shape.rect, net_idx);
  }
}

void collectPortGeometry(const eccdb::LibraryStore& library, const eccdb::TechStore& tech,
                         const EccdbLayerTable& layers, const eccdb::LibraryMasterPort& port, eccdb::Point origin,
                         eccdb::DesignOrientation orient, int32_t width, int32_t height, std::vector<LayeredRect>& routing,
                         std::vector<LayeredRect>& cut, std::vector<ViaShapes>& vias)
{
  for (const auto& clause : port.layer_clauses) {
    const auto* index = layers.find(clause.layer);
    for (const auto& rect : geometryRects(library.geometryStorage(), clause.geometry)) {
      LayeredRect placed{.layer = clause.layer, .rect = placeMasterRect(rect, origin, orient, width, height)};
      if (index != nullptr && index->cut) {
        cut.push_back(placed);
      } else {
        routing.push_back(placed);
      }
    }
  }
  for (const auto shape : port.vias) {
    const auto& via = library.geometryRegistry().get<eccdb::LibraryViaPlacement>(shape);
    const auto placed = placeMasterPoint(via.origin, origin, orient, width, height);
    vias.push_back(techViaShapes(tech, via.via, placed));
  }
}

using MacroPinRectMap = std::map<std::pair<int32_t, bool>, std::vector<PlanarRect>>;

void appendMacroPinRect(MacroPinRectMap& pin_rects, const EccdbLayerTable& layers, const LayeredRect& shape)
{
  const auto* index = layers.find(shape.layer);
  if (index == nullptr || index->wrap_idx < 0) {
    return;
  }
  pin_rects[{index->wrap_idx, index->routing}].emplace_back(shape.rect.ll_x, shape.rect.ll_y, shape.rect.ur_x,
                                                             shape.rect.ur_y);
}

MacroPinRectMap collectMacroPinRects(const eccdb::DesignStore& design, const eccdb::TechStore& tech,
                                     const eccdb::LibraryStore& library, const EccdbLayerTable& layers,
                                     eccdb::DesignInstanceId instance_id, const eccdb::DesignInstance& instance,
                                     const eccdb::LibraryCellMaster& master)
{
  MacroPinRectMap pin_rects;
  const auto& netlist = design.netlistStorage();
  const int32_t width = static_cast<int32_t>(master.width);
  const int32_t height = static_cast<int32_t>(master.height);
  for (const auto pin_id : netlist.instancePins(instance_id)) {
    const auto& pin = netlist.instancePin(pin_id);
    const auto& term = library.masterTermStorage().masterTerm(pin.master_term);
    for (const auto port_id : term.ports) {
      const auto& port = library.masterPortStorage().masterPort(port_id);
      for (const auto& clause : port.layer_clauses) {
        for (const auto local : geometryRects(library.geometryStorage(), clause.geometry)) {
          appendMacroPinRect(pin_rects, layers,
                             {.layer = clause.layer,
                              .rect = placeMasterRect(local, instance.origin, instance.orientation, width, height)});
        }
      }
      for (const auto shape : port.vias) {
        const auto& via = library.geometryRegistry().get<eccdb::LibraryViaPlacement>(shape);
        // Expand in master-local coordinates first, then transform every
        // rectangle. Transforming only the VIA origin loses W/E rotation of
        // non-square metal or cut rectangles.
        const auto local_via = techViaShapes(tech, via.via, via.origin);
        const auto append_placed = [&](const std::vector<LayeredRect>& local_shapes) {
          for (const auto& local : local_shapes) {
            appendMacroPinRect(pin_rects, layers,
                               {.layer = local.layer,
                                .rect = placeMasterRect(local.rect, instance.origin, instance.orientation, width, height)});
          }
        };
        append_placed(local_via.routing);
        append_placed(local_via.cut);
      }
    }
  }
  return pin_rects;
}

PlanarRect planarRect(eccdb::Rect rect)
{
  return PlanarRect{rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y};
}

void appendMacroObs(std::vector<MacroObs>& macro_obs_list, const eccdb::DesignStore& design,
                    const eccdb::TechStore& tech, const eccdb::LibraryStore& library,
                    const EccdbLayerTable& layers)
{
  const eccdb::PhysicalDesignService physical(tech, library, design);
  const auto& netlist = design.netlistStorage();
  for (const auto instance_id : netlist.instances()) {
    if (!physical.isPlaced(instance_id)) {
      continue;
    }
    const auto& instance = netlist.instance(instance_id);
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    if (!isBlockMaster(master.type) || !library.cellMasterStorage().hasObs(instance.master)) {
      continue;
    }

    const auto pin_rects = collectMacroPinRects(design, tech, library, layers, instance_id, instance, master);
    const auto& obs = library.cellMasterStorage().obs(instance.master);
    const int32_t width = static_cast<int32_t>(master.width);
    const int32_t height = static_cast<int32_t>(master.height);
    MacroObs macro_obs;
    for (const auto& clause : obs.layer_clauses) {
      if ((clause.flags & eccdb::LibraryObsLayerFlag::kHasSpacing) == 0u) {
        continue;
      }
      const auto* index = layers.find(clause.layer);
      if (index == nullptr || index->wrap_idx < 0) {
        continue;
      }

      const auto local_rects = geometryRects(library.geometryStorage(), clause.geometry);
      if (local_rects.empty()) {
        continue;
      }

      MacroObsLayer layer;
      layer.layer_idx = index->wrap_idx;
      layer.is_routing = index->routing;
      layer.min_spacing = clause.spacing;
      layer.obs_rect_list.reserve(local_rects.size());
      std::vector<eccdb::Rect> placed_rects;
      placed_rects.reserve(local_rects.size());
      for (const auto local : local_rects) {
        const auto placed = placeMasterRect(local, instance.origin, instance.orientation, width, height);
        placed_rects.push_back(placed);
        layer.obs_rect_list.push_back(planarRect(placed));
      }
      layer.bounding_box = planarRect(boundingBox(placed_rects));
      if (const auto found = pin_rects.find({layer.layer_idx, layer.is_routing}); found != pin_rects.end()) {
        layer.pin_rect_list = found->second;
      }
      // Do not coalesce clauses: LEF permits multiple clauses on one layer
      // with distinct spacing semantics.
      macro_obs.obs_layer_list.push_back(std::move(layer));
    }
    if (!macro_obs.obs_layer_list.empty()) {
      macro_obs_list.push_back(std::move(macro_obs));
    }
  }
}

void fillPrlTable(ParallelRunLengthSpacingRule& rule, const std::vector<int32_t>& widths, const std::vector<int32_t>& parallels,
                  const std::vector<int32_t>& cells)
{
  rule.width_list = widths;
  rule.parallel_length_list = parallels;
  rule.width_parallel_length_map.init(static_cast<int32_t>(widths.size()), static_cast<int32_t>(parallels.size()));
  for (int32_t x = 0; x < rule.width_parallel_length_map.get_x_size(); ++x) {
    for (int32_t y = 0; y < rule.width_parallel_length_map.get_y_size(); ++y) {
      const auto offset = static_cast<size_t>(x) * parallels.size() + static_cast<size_t>(y);
      rule.width_parallel_length_map[x][y] = offset < cells.size() ? cells[offset] : 0;
    }
  }
}

void wrapTrackAxisFromEccdb(RoutingLayer& routing_layer, const eccdb::TechRoutingLayer& tech_layer,
                           eccdb::TechRoutingLayerId layer_id, const eccdb::DesignStore& design)
{
  const bool prefer_horizontal = tech_layer.direction == eccdb::TechRoutingDirection::kHorizontal;
  const auto wanted = prefer_horizontal ? eccdb::DesignAxis::kY : eccdb::DesignAxis::kX;
  for (const auto grid_id : design.floorplanStorage().trackGridsForLayer(layer_id)) {
    const auto& grid = design.floorplanStorage().trackGrid(grid_id);
    if (grid.axis == wanted && grid.step > 0) {
      routing_layer.set_pitch(grid.step);
      return;
    }
  }
}

void wrapRoutingDesignRuleFromEccdb(RoutingLayer& routing_layer, eccdb::TechRoutingLayerId layer_id,
                                   const eccdb::TechStore& tech)
{
  std::set<ViolationType>& exist_rule_set = DRCDM.getDatabase().get_exist_rule_set();
  const auto& storage = tech.routingLayerStorage();
  const auto& tech_layer = storage.routingLayer(layer_id);

  exist_rule_set.insert(ViolationType::kMetalShort);

  {
    const auto rules = storage.lef58CornerFillSpacingRules(layer_id);
    if (!rules.empty()) {
      const auto& rule = storage.rule(rules.front());
      auto& corner = routing_layer.get_corner_fill_spacing_rule();
      corner.has_corner_fill = true;
      corner.corner_fill_spacing = rule.spacing;
      corner.edge_length_1 = rule.edge_length1;
      corner.edge_length_2 = rule.edge_length2;
      corner.adjacent_eol = rule.eol_width;
      exist_rule_set.insert(ViolationType::kCornerFillSpacing);
    }
  }

  {
    const auto rules = storage.lef58CornerSpacingRules(layer_id);
    if (!rules.empty()) {
      auto& corner_list = routing_layer.get_corner_spacing_rule_list();
      for (const auto rule_id : rules) {
        const auto& src = storage.rule(rule_id);
        CornerSpacingRule corner;
        corner.has_convex_corner = src.type == eccdb::TechRoutingLef58CornerType::kConvex;
        corner.has_concave_corner = src.type == eccdb::TechRoutingLef58CornerType::kConcave;
        corner.has_except_eol = (src.flags & eccdb::TechRoutingLef58CornerSpacingRuleFlag::kHasExceptEol) != 0;
        if (corner.has_except_eol) {
          corner.except_eol = src.except_eol;
        }
        for (const auto& item : src.width_spacings) {
          corner.width_spacing_list.emplace_back(item.width, item.spacing);
        }
        corner_list.push_back(std::move(corner));
      }
      exist_rule_set.insert(ViolationType::kCornerSpacing);
    }
  }

  {
    const auto eol_ids = storage.lef58SpacingEolRules(layer_id);
    if (!eol_ids.empty()) {
      auto& eol_list = routing_layer.get_end_of_line_spacing_rule_list();
      for (const auto eol_id : eol_ids) {
        const auto& src = storage.rule(eol_id);
        EndOfLineSpacingRule eol;
        eol.eol_spacing = src.eol_space;
        eol.eol_width = src.eol_width;
        eol.eol_within = src.eol_within;
        eol.has_ete = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kHasEndToEnd) != 0;
        if (eol.has_ete) {
          eol.ete_spacing = src.end_to_end_space;
        }
        eol.has_par = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kHasParallelEdge) != 0;
        if (eol.has_par) {
          eol.has_subtrace_eol_width = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kSubtractEolWidth) != 0;
          eol.par_spacing = src.parallel_space;
          eol.par_within = src.parallel_within;
          eol.has_two_edges = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kTwoEdges) != 0;
          eol.has_min_length = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kHasParallelMinLength) != 0;
          if (eol.has_min_length) {
            eol.min_length = src.parallel_min_length;
          }
          eol.has_same_metal = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kSameMetal) != 0;
        }
        eol.has_enclose_cut = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kHasEncloseCut) != 0;
        if (eol.has_enclose_cut) {
          eol.has_below = src.enclose_cut_side == eccdb::CutLayerSide::kBelow;
          eol.has_above = src.enclose_cut_side == eccdb::CutLayerSide::kAbove;
          eol.enclosed_dist = src.enclose_distance;
          eol.cut_to_metal_spacing = src.cut_to_metal_spacing;
          eol.has_all_cuts = (src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kAllCuts) != 0;
        }
        eol_list.push_back(eol);
      }
      exist_rule_set.insert(ViolationType::kEndOfLineSpacing);
    }

    const auto native_eol_ids = storage.endOfLineSpacingRules(layer_id);
    if (!native_eol_ids.empty()) {
      auto& eol_list = routing_layer.get_end_of_line_spacing_rule_list();
      for (const auto eol_id : native_eol_ids) {
        const auto& src = storage.rule(eol_id);
        EndOfLineSpacingRule eol;
        eol.eol_spacing = src.min_spacing;
        eol.eol_width = src.eol_width;
        eol.eol_within = src.eol_within;
        eol.has_par = (src.flags & eccdb::TechRoutingEndOfLineSpacingRuleFlag::kHasParallelEdge) != 0;
        if (eol.has_par) {
          eol.par_spacing = src.parallel_space;
          eol.par_within = src.parallel_within;
          eol.has_two_edges = (src.flags & eccdb::TechRoutingEndOfLineSpacingRuleFlag::kTwoEdges) != 0;
        }
        eol_list.push_back(eol);
      }
      exist_rule_set.insert(ViolationType::kEndOfLineSpacing);
    }
  }

  {
    auto& maximum_width = routing_layer.get_maximum_width_rule();
    maximum_width.max_width = (tech_layer.flags & eccdb::TechRoutingLayerFlag::kHasMaxWidth) != 0 ? tech_layer.max_width
                                                                                                         : std::numeric_limits<int32_t>::max();
    exist_rule_set.insert(ViolationType::kMaximumWidth);
  }

  {
    const auto enclose_ids = storage.minEncloseAreaRules(layer_id);
    if (!enclose_ids.empty()) {
      routing_layer.get_min_hole_rule().min_hole_area = storage.rule(enclose_ids.front()).area;
      exist_rule_set.insert(ViolationType::kMinHole);
    }
  }

  {
    routing_layer.get_minimum_area_rule().min_area = static_cast<int32_t>(tech_layer.area);
    exist_rule_set.insert(ViolationType::kMinimumArea);
  }

  {
    const auto mincut_ids = storage.lef58MinimumCutRules(layer_id);
    if (!mincut_ids.empty()) {
      auto& mincut_list = routing_layer.get_minimum_cut_rule_list();
      for (const auto mincut_id : mincut_ids) {
        const auto& src = storage.rule(mincut_id);
        MinimumCutRule rule;
        if ((src.flags & eccdb::TechRoutingLef58MinimumCutRuleFlag::kHasNumCuts) != 0) {
          rule.num_cuts = src.num_cuts;
        } else {
          for (const auto& cut_class : src.cutclasses) {
            if (cut_class.cutclass_name == "VSINGLECUT") {
              rule.num_cuts = cut_class.num_cuts;
              break;
            }
          }
        }
        rule.width = src.width;
        rule.has_within_cut_distance = (src.flags & eccdb::TechRoutingLef58MinimumCutRuleFlag::kHasWithinCutDistance) != 0;
        if (rule.has_within_cut_distance) {
          rule.within_cut_distance = src.within_cut_distance;
        }
        rule.has_from_above = src.orient == eccdb::TechRoutingMinimumCutOrient::kFromAbove;
        rule.has_from_below = src.orient == eccdb::TechRoutingMinimumCutOrient::kFromBelow;
        rule.has_length = (src.flags & eccdb::TechRoutingLef58MinimumCutRuleFlag::kHasLength) != 0;
        if (rule.has_length) {
          rule.length = src.length;
          rule.distance = src.length_distance;
        }
        mincut_list.push_back(rule);
      }
      exist_rule_set.insert(ViolationType::kMinimumCut);
    }
  }

  {
    routing_layer.get_minimum_width_rule().min_width = effectiveMinWidth(tech_layer);
    exist_rule_set.insert(ViolationType::kMinimumWidth);
  }

  {
    const auto native_ids = storage.minStepRules(layer_id);
    const auto lef58_ids = storage.lef58MinStepRules(layer_id);
    if (!native_ids.empty() && !lef58_ids.empty()) {
      const auto& native = storage.rule(native_ids.front());
      const auto& lef58 = storage.rule(lef58_ids.front());
      auto& min_step = routing_layer.get_min_step_rule();
      min_step.min_step = native.min_step_length;
      min_step.max_edges = native.max_edges;
      min_step.lef58_min_step = lef58.min_step_length;
      min_step.lef58_min_adjacent_length = lef58.min_adjacent_length;
      exist_rule_set.insert(ViolationType::kMinStep);
    }
  }

  {
    routing_layer.get_nonsufficient_metal_overlap_rule().min_width = effectiveMinWidth(tech_layer);
    exist_rule_set.insert(ViolationType::kNonsufficientMetalOverlap);
  }

  {
    const auto notch_ids = storage.spacingNotchLengthRules(layer_id);
    const auto lef58_notch_ids = storage.lef58SpacingNotchLengthRules(layer_id);
    auto& notch = routing_layer.get_notch_spacing_rule();
    if (!notch_ids.empty()) {
      const auto& src = storage.rule(notch_ids.front());
      notch.notch_spacing = src.min_spacing;
      notch.notch_length = src.notch_length;
      exist_rule_set.insert(ViolationType::kNotchSpacing);
    } else if (!lef58_notch_ids.empty()) {
      const auto& src = storage.rule(lef58_notch_ids.front());
      notch.notch_spacing = src.min_spacing;
      notch.notch_length = src.min_notch_length;
      notch.concave_ends = src.concave_ends_side_of_notch_width;
      exist_rule_set.insert(ViolationType::kNotchSpacing);
    }
  }

  {
    auto& spacing_rule = routing_layer.get_parallel_run_length_spacing_rule();
    const auto prl_ids = storage.prlSpacingTableRules(layer_id);
    if (!prl_ids.empty()) {
      const auto prl_id = prl_ids.front();
      const auto& src = storage.rule(prl_id);
      fillPrlTable(spacing_rule, {src.widths.begin(), src.widths.end()}, {src.parallel_run_lengths.begin(), src.parallel_run_lengths.end()},
                   {src.cells.begin(), src.cells.end()});
      spacing_rule.has_spacing_table = true;
      exist_rule_set.insert(ViolationType::kParallelRunLengthSpacing);
    }

    const auto spacing_ids = storage.spacingRules(layer_id);
    for (const auto spacing_id : spacing_ids) {
      const auto& src = storage.rule(spacing_id);
      const auto type = src.type == eccdb::TechRoutingSpacingType::kRange ? LayerSpacingType::kSpacingRange
                                                                                  : LayerSpacingType::kSpacingDefault;
      spacing_rule.spacing_list.push_back({type, src.min_spacing, src.min_width, src.max_width});
    }
    // The legacy iDB owns an allocated spacing-list object even when the LEF
    // layer has no ordinary SPACING clauses, and iDRC treats that as present.
    spacing_rule.has_spacing_list = true;
    exist_rule_set.insert(ViolationType::kParallelRunLengthSpacing);
  }
}

void wrapCutDesignRuleFromEccdb(CutLayer& cut_layer, eccdb::TechCutLayerId layer_id, const eccdb::TechStore& tech)
{
  std::set<ViolationType>& exist_rule_set = DRCDM.getDatabase().get_exist_rule_set();
  const auto& storage = tech.cutLayerStorage();

  exist_rule_set.insert(ViolationType::kCutShort);

  {
    const auto eol_id = storage.lef58EolSpacingRule(layer_id);
    if (eol_id) {
      const auto& src = storage.lef58EolSpacingRule(eol_id);
      auto& rule = cut_layer.get_cut_eol_spacing_rule();
      rule.eol_spacing = src.cut_spacing1;
      rule.eol_prl = src.prl;
      rule.eol_prl_spacing = src.cut_spacing2;
      rule.eol_width = src.eol_width;
      rule.smaller_overhang = src.smaller_overhang;
      rule.equal_overhang = src.equal_overhang;
      rule.side_ext = src.side_ext;
      rule.backward_ext = src.backward_ext;
      rule.span_length = src.span_length;
      exist_rule_set.insert(ViolationType::kCutEOLSpacing);
    }
  }

  {
    const auto table_ids = storage.lef58SpacingTableRules(layer_id);
    const eccdb::TechCutLef58SpacingTableRule* second_layer_table = nullptr;
    for (const auto table_id : table_ids) {
      const auto& table = storage.lef58SpacingTableRule(table_id);
      if ((table.flags & eccdb::TechCutLef58SpacingTableRuleFlag::kHasSecondLayer) != 0) {
        second_layer_table = &table;
      }
    }
    if (second_layer_table != nullptr && !second_layer_table->cells.empty()) {
      const auto& cell = second_layer_table->cells.front();
      auto& different = cut_layer.get_different_layer_cut_spacing_rule();
      different.below_spacing = cell.cut_spacing1;
      different.below_prl = second_layer_table->prl;
      different.below_prl_spacing = cell.cut_spacing2;
      exist_rule_set.insert(ViolationType::kDifferentLayerCutSpacing);
    }
  }

  {
    const auto edge_ids = storage.lef58EnclosureEdgeRules(layer_id);
    if (!edge_ids.empty()) {
      auto& edge_list = cut_layer.get_enclosure_edge_rule_list();
      for (const auto edge_id : edge_ids) {
        const auto& src = storage.lef58EnclosureEdgeRule(edge_id);
        EnclosureEdgeRule rule;
        if ((src.flags & eccdb::TechCutLef58EnclosureEdgeRuleFlag::kHasConvexCorners) != 0) {
          rule.has_convexcorners = true;
          rule.convex_length = src.convex_length;
          rule.adjacent_length = src.adjacent_length;
          rule.convex_par_within = src.convex_par_within;
          rule.length = src.convex_corner_length;
          rule.has_above = src.side == eccdb::CutLayerSide::kAbove;
          rule.has_below = src.side == eccdb::CutLayerSide::kBelow;
          rule.overhang = src.overhang;
          edge_list.push_back(rule);
          continue;
        }
        rule.has_above = src.side == eccdb::CutLayerSide::kAbove;
        rule.has_below = src.side == eccdb::CutLayerSide::kBelow;
        rule.overhang = src.overhang;
        rule.min_width = src.min_width;
        rule.par_length = src.par_length;
        rule.par_within = src.par_within;
        rule.has_except_two_edges = (src.flags & eccdb::TechCutLef58EnclosureEdgeRuleFlag::kExceptTwoEdges) != 0;
        edge_list.push_back(rule);
      }
      exist_rule_set.insert(ViolationType::kEnclosureEdge);
    }
  }

  {
    const auto eol_id = storage.lef58EolEnclosureRule(layer_id);
    if (eol_id) {
      const auto& src = storage.lef58EolEnclosureRule(eol_id);
      auto& rule = cut_layer.get_enclosure_parallel_rule();
      rule.eol_width = src.eol_width;
      rule.has_above = src.side == eccdb::CutLayerSide::kAbove;
      rule.has_below = src.side == eccdb::CutLayerSide::kBelow;
      rule.overhang = src.overhang;
      if ((src.flags & eccdb::TechCutLef58EolEnclosureRuleFlag::kHasParallelSpace) != 0) {
        rule.par_spacing = src.parallel_space;
      }
      if ((src.flags & eccdb::TechCutLef58EolEnclosureRuleFlag::kHasExtension) != 0) {
        rule.backward_ext = src.backward_ext;
        rule.forward_ext = src.forward_ext;
      }
      rule.has_min_length = (src.flags & eccdb::TechCutLef58EolEnclosureRuleFlag::kHasMinLength) != 0;
      if (rule.has_min_length) {
        rule.min_length = src.min_length;
      }
      exist_rule_set.insert(ViolationType::kEnclosureParallel);
    }
  }

  {
    auto& adjacent = cut_layer.get_adjacent_cut_rule();
    for (const auto spacing_id : storage.spacingRules(layer_id)) {
      const auto& src = storage.spacingRule(spacing_id);
      if ((src.flags & eccdb::TechCutSpacingRuleFlag::kHasAdjacentCuts) == 0) {
        continue;
      }
      adjacent.cut_spacing = src.spacing;
      adjacent.adjacnet_cuts = static_cast<int32_t>(src.adjacent_cut_count);
      adjacent.cut_within = src.adjacent_cut_within;
      exist_rule_set.insert(ViolationType::kAdjacentCutSpacing);
    }
  }

  {
    auto& same = cut_layer.get_same_layer_cut_spacing_rule();
    const auto spacing_ids = storage.spacingRules(layer_id);
    if (!spacing_ids.empty()) {
      for (const auto spacing_id : spacing_ids) {
        const auto& spacing_rule = storage.spacingRule(spacing_id);
        if ((spacing_rule.flags & eccdb::TechCutSpacingRuleFlag::kHasAdjacentCuts) != 0) {
          continue;
        }
        same.spacings.push_back({spacing_rule.spacing,
                                 -1,
                                 -1,
                                 (spacing_rule.flags & eccdb::TechCutSpacingRuleFlag::kSameNet) != 0});
      }
      if (!same.spacings.empty()) {
        exist_rule_set.insert(ViolationType::kSameLayerCutSpacing);
      }
    } else {
      const auto table_ids = storage.lef58SpacingTableRules(layer_id);
      const eccdb::TechCutLef58SpacingTableRule* same_table = nullptr;
      for (const auto table_id : table_ids) {
        const auto& table = storage.lef58SpacingTableRule(table_id);
        if ((table.flags & eccdb::TechCutLef58SpacingTableRuleFlag::kHasSecondLayer) == 0) {
          same_table = &table;
        }
      }
      if (same_table != nullptr && !same_table->cells.empty()) {
        const auto& cell = same_table->cells.front();
        same.spacings.push_back({cell.cut_spacing1, same_table->prl, cell.cut_spacing2, false});
        exist_rule_set.insert(ViolationType::kSameLayerCutSpacing);
      }
    }
  }
}

}  // namespace

class EccdbDrcSource final : public DRCDesignSource
{
 public:
  EccdbDrcSource(eccdb::DesignStore& design, eccdb::TechStore& tech, eccdb::LibraryStore& library)
      : _design(&design), _tech(&tech), _library(&library) {}

  void beginRun() override;
  void endRun() noexcept override;
  void wrapDatabase() override;
  int32_t regularNetCount() const override { return _regular_net_count; }
  std::string netName(int32_t net_idx, bool initialized) const override;
  std::vector<ids::Shape> environmentShapes(std::set<size_t>* obs_shape_idx_set) const override;
  std::vector<ids::Shape> resultShapes() const override;

 private:
  eccdb::DesignStore* _design;
  eccdb::TechStore* _tech;
  eccdb::LibraryStore* _library;
  int32_t _regular_net_count = 0;
  std::vector<std::string> _net_names;
};

void EccdbDrcSource::beginRun()
{
  endRun();
  const auto& netlist = _design->netlistStorage();
  const auto regular = netlist.regularNets();
  const auto special = netlist.specialNets();
  _regular_net_count = static_cast<int32_t>(regular.size());
  _net_names.reserve(regular.size() + special.size());
  for (const auto net : regular) {
    _net_names.push_back(netlist.net(net).name);
  }
  for (const auto net : special) {
    _net_names.push_back(netlist.net(net).name);
  }
}

void EccdbDrcSource::endRun() noexcept
{
  _regular_net_count = 0;
  _net_names.clear();
}

std::string EccdbDrcSource::netName(int32_t net_idx, bool initialized) const
{
  if (net_idx == -1) {
    return "obs";
  }
  if (initialized) {
    if (net_idx >= 0 && static_cast<size_t>(net_idx) < _net_names.size()) {
      return _net_names[static_cast<size_t>(net_idx)];
    }
  } else {
    const auto& netlist = _design->netlistStorage();
    const auto regular = netlist.regularNets();
    if (net_idx >= 0 && net_idx < static_cast<int32_t>(regular.size())) {
      return netlist.net(regular[static_cast<size_t>(net_idx)]).name;
    }
    const auto special = netlist.specialNets();
    const auto special_idx = net_idx - static_cast<int32_t>(regular.size());
    if (special_idx >= 0 && special_idx < static_cast<int32_t>(special.size())) {
      return netlist.net(special[static_cast<size_t>(special_idx)]).name;
    }
  }
  return "<invalid_eccdb_net:" + std::to_string(net_idx) + ">";
}

void EccdbDrcSource::wrapDatabase()
{
  if (_design == nullptr || _tech == nullptr || _library == nullptr) {
    DRCLOG.error(Loc::current(), "EccDB design source is incomplete.");
    return;
  }

  const auto& design = *_design;
  const auto& tech = *_tech;
  const EccdbLayerTable layers(tech);

  if (design.globalStorage().hasInfo()) {
    DRCDM.getDatabase().set_design_name(design.globalStorage().info().name);
    DRCDM.getDatabase().set_micron_dbu(design.globalStorage().info().database_units_per_micron);
  }
  if (tech.globalStorage().hasManufacturingGrid()) {
    DRCDM.getDatabase().set_manufacture_grid(tech.globalStorage().getManufacturingGrid().value);
  }
  if (design.globalStorage().hasDieArea()) {
    const auto die_bounds = design.globalStorage().dieBounds();
    auto& die = DRCDM.getDatabase().get_die();
    // dmInst normalizes the whole design when either DIE lower bound is
    // negative. Mirror its wrapped DIE coordinates for the EccDB source.
    if (die_bounds.ll_x < 0 || die_bounds.ll_y < 0) {
      die.set_ll(0, 0);
      die.set_ur(die_bounds.ur_x - die_bounds.ll_x, die_bounds.ur_y - die_bounds.ll_y);
    } else {
      die.set_ll(die_bounds.ll_x, die_bounds.ll_y);
      die.set_ur(die_bounds.ur_x, die_bounds.ur_y);
    }
  }

  {
    auto& exist_rule_set = DRCDM.getDatabase().get_exist_rule_set();
    exist_rule_set.insert(ViolationType::kOutOfDie);
    if (const auto* max_via_stack = tech.globalStorage().tryGetMaxViaStack(); max_via_stack != nullptr) {
      auto& rule = DRCDM.getDatabase().get_max_via_stack_rule();
      rule.max_via_stack_num = static_cast<int32_t>(max_via_stack->max_stack_count);
      rule.bottom_routing_layer_idx = layers.routingWrapIdx(max_via_stack->bottom_layer);
      rule.top_routing_layer_idx = layers.routingWrapIdx(max_via_stack->top_layer);
      exist_rule_set.insert(ViolationType::kMaxViaStack);
    }
    auto& off_grid = DRCDM.getDatabase().get_off_grid_or_wrong_way_rule();
    off_grid.manufacture_grid = tech.globalStorage().hasManufacturingGrid() ? tech.globalStorage().getManufacturingGrid().value : 0;
    exist_rule_set.insert(ViolationType::kOffGridOrWrongWay);
  }

  auto& routing_layer_list = DRCDM.getDatabase().get_routing_layer_list();
  auto& cut_layer_list = DRCDM.getDatabase().get_cut_layer_list();
  for (const auto layer : tech.layerSequence()) {
    const auto* index = layers.find(layer);
    if (index == nullptr) {
      continue;
    }
    if (index->routing) {
      const auto routing_id = eccdb::TechRoutingLayerId{layer.entity()};
      const auto& tech_layer = tech.routingLayerStorage().routingLayer(routing_id);
      RoutingLayer routing_layer;
      routing_layer.set_layer_idx(index->wrap_idx);
      routing_layer.set_layer_order(index->order);
      routing_layer.set_layer_name(tech.layerInfo(layer).name);
      routing_layer.set_prefer_direction(directionFromTech(tech_layer.direction));
      wrapTrackAxisFromEccdb(routing_layer, tech_layer, routing_id, design);
      wrapRoutingDesignRuleFromEccdb(routing_layer, routing_id, tech);
      routing_layer_list.push_back(std::move(routing_layer));
    } else if (index->cut) {
      const auto cut_id = eccdb::TechCutLayerId{layer.entity()};
      CutLayer cut_layer;
      cut_layer.set_layer_idx(index->wrap_idx);
      cut_layer.set_layer_order(index->order);
      cut_layer.set_layer_name(tech.layerInfo(layer).name);
      wrapCutDesignRuleFromEccdb(cut_layer, cut_id, tech);
      cut_layer_list.push_back(std::move(cut_layer));
    }
  }

  DRCI.wrapLayerInfo();

  // MacroObs shares the DesignStore coordinate system already consumed by
  // environmentShapes() and resultShapes(). In particular, do not apply an
  // independent DIE-origin translation here.
  appendMacroObs(DRCDM.getDatabase().get_macro_obs_list(), design, tech, *_library, layers);
}

std::vector<ids::Shape> EccdbDrcSource::environmentShapes(std::set<size_t>* obs_shape_idx_set) const
{
  std::vector<ids::Shape> env_shape_list;
  if (_design == nullptr || _tech == nullptr || _library == nullptr) {
    DRCLOG.error(Loc::current(), "EccDB design source is incomplete.");
    return env_shape_list;
  }

  const auto& design = *_design;
  const auto& tech = *_tech;
  const auto& library = *_library;
  const EccdbLayerTable layers(tech);
  const eccdb::PhysicalDesignService physical(tech, library, design);
  const NetIndexMap net_index_map = netIndexMap(design);
  NetSkipCache skip_cache;

  for (const auto instance_id : design.netlistStorage().instances()) {
    if (!physical.isPlaced(instance_id)) {
      continue;
    }
    const auto& instance = design.netlistStorage().instance(instance_id);
    const auto& master = library.cellMasterStorage().cellMaster(instance.master);
    const int32_t width = static_cast<int32_t>(master.width);
    const int32_t height = static_cast<int32_t>(master.height);

    if (library.cellMasterStorage().hasObs(instance.master)) {
      const auto& obs = library.cellMasterStorage().obs(instance.master);
      for (const auto& clause : obs.layer_clauses) {
        for (const auto& rect : geometryRects(library.geometryStorage(), clause.geometry)) {
          const auto shape_index = env_shape_list.size();
          appendShape(env_shape_list, layers, clause.layer, placeMasterRect(rect, instance.origin, instance.orientation, width, height), -1);
          if (obs_shape_idx_set != nullptr && env_shape_list.size() != shape_index && env_shape_list.back().is_routing) {
            obs_shape_idx_set->insert(shape_index);
          }
        }
      }
    }

    for (const auto pin_id : design.netlistStorage().instancePins(instance_id)) {
      const auto& pin = design.netlistStorage().instancePin(pin_id);
      int32_t net_idx = -1;
      const auto pin_net = instancePinNet(pin);
      if (pin.special_net) {
        net_idx = netIndex(pin.special_net, net_index_map);
      } else if (!netIsSkipping(design, library, pin_net, skip_cache)) {
        net_idx = netIndex(pin_net, net_index_map);
      }

      const auto& term = library.masterTermStorage().masterTerm(pin.master_term);
      for (const auto port_id : term.ports) {
        std::vector<LayeredRect> routing;
        std::vector<LayeredRect> cut;
        std::vector<ViaShapes> vias;
        collectPortGeometry(library, tech, layers, library.masterPortStorage().masterPort(port_id), instance.origin, instance.orientation, width,
                            height, routing, cut, vias);
        for (const auto& shape : routing) {
          appendShape(env_shape_list, layers, shape.layer, shape.rect, net_idx);
        }
        for (const auto& shape : cut) {
          appendShape(env_shape_list, layers, shape.layer, shape.rect, net_idx);
        }
        for (const auto& via : vias) {
          appendViaRoutingBoundingBoxes(env_shape_list, layers, via, net_idx);
        }
      }
    }
  }

  for (const auto pin_id : design.netlistStorage().ioPins()) {
    const auto& io_pin = design.netlistStorage().ioPin(pin_id);
    int32_t net_idx = -1;
    const auto pin_net = ioPinNet(io_pin);
    if (io_pin.special_net) {
      net_idx = netIndex(io_pin.special_net, net_index_map);
    } else if (!netIsSkipping(design, library, pin_net, skip_cache)) {
      net_idx = netIndex(pin_net, net_index_map);
    }
    for (const auto block_pin_id : design.netlistStorage().blockPins(pin_id)) {
      const auto& block_pin = design.netlistStorage().blockPin(block_pin_id);
      const auto& geometry = design.geometryRegistry();
      if (block_pin.placement_status == eccdb::DesignPlacementStatus::kUnplaced
          && (block_pin.flags & eccdb::DesignIoPinPortFlag::kHasPlacement) == 0) {
        continue;
      }
      for (const auto id : block_pin.rectangles) {
        const auto& rect = geometry.get<eccdb::DesignPinRectangle>(id);
        appendShape(env_shape_list, layers, rect.layer,
                    placeMasterRect(rect.rectangle, block_pin.origin, block_pin.orientation, 0, 0), net_idx);
      }
      for (const auto id : block_pin.polygons) {
        const auto& polygon = geometry.get<eccdb::DesignPinPolygon>(id);
        for (const auto& rect : eccdb::decomposePolygonToRectangles(polygon.points)) {
          appendShape(env_shape_list, layers, polygon.layer,
                      placeMasterRect(rect, block_pin.origin, block_pin.orientation, 0, 0), net_idx);
        }
      }
    }
  }

  physical.forEachRoutingBlockage([&](const auto blockage_id, const auto&) {
    physical.forEachRoutingBlockageGeometry(
        blockage_id,
        eccdb::PhysicalGeometryVisitor{
            .rectangle = [&](const eccdb::PhysicalLayerRect& shape) {
              appendShape(env_shape_list, layers, shape.layer, shape.rectangle, -1);
            },
            .polygon = [&](const eccdb::PhysicalLayerPolygon& shape) {
              for (const auto& rect : eccdb::decomposePolygonToRectangles(shape.points)) {
                appendShape(env_shape_list, layers, shape.layer, rect, -1);
              }
            }});
  });

  return env_shape_list;
}

std::vector<ids::Shape> EccdbDrcSource::resultShapes() const
{
  std::vector<ids::Shape> result_shape_list;
  if (_design == nullptr || _tech == nullptr || _library == nullptr) {
    DRCLOG.error(Loc::current(), "EccDB design source is incomplete.");
    return result_shape_list;
  }

  const auto& design = *_design;
  const auto& tech = *_tech;
  const auto& library = *_library;
  const EccdbLayerTable layers(tech);
  const eccdb::PhysicalDesignService physical(tech, library, design);
  const NetIndexMap net_index_map = netIndexMap(design);

  const auto appendVia = [&](const eccdb::DesignRouteVia& route_via, int32_t net_idx) {
    const auto& via = route_via.via;
    const uint32_t rows = std::max(via.rows, 1u);
    const uint32_t columns = std::max(via.columns, 1u);
    for (uint32_t row = 0; row < rows; ++row) {
      for (uint32_t column = 0; column < columns; ++column) {
        const eccdb::Point origin{route_via.origin.position.x + static_cast<int32_t>(column) * via.step_x,
                                  route_via.origin.position.y + static_cast<int32_t>(row) * via.step_y};
        appendViaShapes(result_shape_list, layers,
                        resolveViaShapes(design, tech, via.tech_via, via.design_via, origin), net_idx);
      }
    }
  };

  const auto appendWireShapes = [&](eccdb::DesignWireId wire_id, int32_t net_idx, bool special) {
    design.routingStorage().forEachShape(
        wire_id, [&](eccdb::DesignRouteShapeId, const eccdb::DesignRouteShape& shape,
                     const eccdb::DesignRouteShapeAttributes* attributes) {
          std::visit(
              [&](const auto& geometry) {
                using Geometry = std::decay_t<decltype(geometry)>;
                const auto layer = eccdb::TechLayerId{shape.layer.entity()};
                if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteSegment>) {
                  if (special) {
                    const int32_t width = attributes == nullptr ? 0 : attributes->width;
                    appendShape(result_shape_list, layers, layer,
                                wireSegmentBox(geometry.begin.position, geometry.end.position, width), net_idx);
                  } else {
                    const int32_t width = tech.routingLayerStorage().routingLayer(shape.layer).width;
                    appendShape(result_shape_list, layers, layer, regularWireSegmentBox(geometry, width / 2), net_idx);
                  }
                } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRouteVia>) {
                  appendVia(geometry, net_idx);
                } else if constexpr (std::is_same_v<Geometry, eccdb::DesignRoutePatch>) {
                  appendShape(result_shape_list, layers, layer, geometry.rectangle, net_idx);
                } else {
                  // Point-only Shapes preserve DEF syntax but do not represent metal.
                }
              },
              shape.geometry);
        });
  };

  const auto appendStandaloneGeometry = [&](eccdb::DesignNetId net_id, int32_t net_idx) {
    physical.forEachNetGeometry(
        net_id,
        eccdb::PhysicalNetGeometryVisitor{
            .rectangle = [&](const eccdb::DesignGeometryRectangle& shape) {
              appendShape(result_shape_list, layers, eccdb::TechLayerId{shape.layer.entity()}, shape.rectangle, net_idx);
            },
            .polygon = [&](const eccdb::DesignGeometryPolygon& shape) {
              for (const auto& rect : eccdb::decomposePolygonToRectangles(shape.points)) {
                appendShape(result_shape_list, layers, eccdb::TechLayerId{shape.layer.entity()}, rect, net_idx);
              }
            },
            .via = [&](const eccdb::DesignGeometryNetVia& shape) {
              for (const auto origin : shape.origins) {
                appendViaShapes(result_shape_list, layers,
                                resolveViaShapes(design, tech, shape.tech_via, shape.design_via, origin), net_idx);
              }
            }});
  };

  for (const auto net_id : design.netlistStorage().regularNets()) {
    const int32_t net_idx = netIndex(net_id, net_index_map);
    for (const auto wire_id : design.routingStorage().wires(net_id)) {
      appendWireShapes(wire_id, net_idx, false);
    }
  }

  for (const auto net_id : design.netlistStorage().specialNets()) {
    const int32_t net_idx = netIndex(net_id, net_index_map);
    for (const auto wire_id : design.routingStorage().wires(net_id)) {
      appendWireShapes(wire_id, net_idx, true);
    }
    appendStandaloneGeometry(net_id, net_idx);
  }

  return result_shape_list;
}

}  // namespace idrc

namespace eccdb::adapters {

std::shared_ptr<idrc::DRCDesignSource> makeDrcSource(DesignStore& design, TechStore& tech, LibraryStore& library)
{
  return std::make_shared<idrc::EccdbDrcSource>(design, tech, library);
}

void bindDrcSource(idrc::DRCInterface& tool, DesignStore* design, TechStore* tech, LibraryStore* library)
{
  if (design == nullptr && tech == nullptr && library == nullptr) {
    tool.setDesignSource(nullptr);
    return;
  }
  if (design == nullptr || tech == nullptr || library == nullptr) {
    throw std::invalid_argument("EccDB DRC source requires Design, Tech, and Library stores");
  }
  tool.setDesignSource(makeDrcSource(*design, *tech, *library));
}

void bindDrcSource(idrc::DRCInterface& tool, DatabaseContext* context)
{
  if (context == nullptr) {
    tool.setDesignSource(nullptr);
    return;
  }
  bindDrcSource(tool, &context->design(), context->technology(), context->library());
}

}  // namespace eccdb::adapters
