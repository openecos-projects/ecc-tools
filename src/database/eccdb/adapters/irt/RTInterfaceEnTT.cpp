// SPDX-License-Identifier: MulanPSL-2.0

#include "RTInterface.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ConnectType.hpp"
#include "CutLayer.hpp"
#include "DataManager.hpp"
#include "Direction.hpp"
#include "EXTLayerRect.hpp"
#include "LayerRect.hpp"
#include "Logger.hpp"
#include "Net.hpp"
#include "Obstacle.hpp"
#include "Pin.hpp"
#include "PlanarRect.hpp"
#include "RoutingLayer.hpp"
#include "ScaleAxis.hpp"
#include "SpacingTable.hpp"
#include "Utility.hpp"
#include "ViaMaster.hpp"
#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "geometry/PolygonRectDecomposer.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechStore.h"

namespace irt {
namespace {

struct EnttLayerIndex
{
  uint32_t packed = 0;
  int32_t wrap_idx = -1;
  int32_t order = -1;
  bool routing = false;
  bool cut = false;
};

// The EnTT database is immutable while wrapping.  These tables turn the
// repeated entity-id -> sparse-set lookup in the adapter into one contiguous
// entity-indexed load.  They are deliberately local to one wrap operation so
// no storage ownership or invalidation contract changes.
template <typename Component, typename Registry>
class EntityPointerTable
{
 public:
  using entity_type = typename Registry::entity_type;

  explicit EntityPointerTable(const Registry& registry)
  {
    const auto view = registry.template view<const Component>();
    size_t max_entity = 0;
    bool has_component = false;
    for (const auto entity : view) {
      has_component = true;
      max_entity = std::max(max_entity, static_cast<size_t>(entt::to_entity(entity)));
    }
    if (has_component) {
      _values.resize(max_entity + 1u, nullptr);
      view.each([this](const auto entity, const Component& component) {
        _values[static_cast<size_t>(entt::to_entity(entity))] = &component;
      });
    }
  }

  [[nodiscard]] const Component* tryGet(entity_type entity) const noexcept
  {
    const auto index = static_cast<size_t>(entt::to_entity(entity));
    return index < _values.size() ? _values[index] : nullptr;
  }

  [[nodiscard]] const Component& get(entity_type entity) const
  {
    const auto* component = tryGet(entity);
    if (component == nullptr) {
      throw std::out_of_range("entity is missing from wrapper component table");
    }
    return *component;
  }

 private:
  std::vector<const Component*> _values;
};

struct WrapperEntityTables
{
  template <typename DesignRegistry, typename LibraryRegistry>
  WrapperEntityTables(const DesignRegistry& design_registry, const LibraryRegistry& library_registry)
      : instances(design_registry), instance_pins(design_registry), io_pins(design_registry), nets(design_registry), masters(library_registry),
        master_obs(library_registry), master_terms(library_registry), master_ports(library_registry)
  {
  }

  EntityPointerTable<eccdb::DesignInstance, eccdb::DesignRegistry::registry_type> instances;
  EntityPointerTable<eccdb::DesignInstancePin, eccdb::DesignRegistry::registry_type> instance_pins;
  EntityPointerTable<eccdb::DesignIoPin, eccdb::DesignRegistry::registry_type> io_pins;
  EntityPointerTable<eccdb::DesignNet, eccdb::DesignRegistry::registry_type> nets;
  EntityPointerTable<eccdb::LibraryCellMaster, eccdb::LibraryRegistry::registry_type> masters;
  EntityPointerTable<eccdb::LibraryMasterObs, eccdb::LibraryRegistry::registry_type> master_obs;
  EntityPointerTable<eccdb::LibraryMasterTerm, eccdb::LibraryRegistry::registry_type> master_terms;
  EntityPointerTable<eccdb::LibraryMasterPort, eccdb::LibraryRegistry::registry_type> master_ports;
};

class EnttLayerTable
{
 public:
  explicit EnttLayerTable(const eccdb::TechStore& tech)
  {
    int32_t routing_idx = 0;
    int32_t cut_idx = 0;
    const auto& sequence = tech.layerSequence();
    uint32_t max_entity = 0;
    for (const auto layer : sequence) {
      max_entity = std::max(max_entity, static_cast<uint32_t>(entt::to_entity(layer.entity())));
    }
    _by_entity.resize(sequence.empty() ? 0u : static_cast<size_t>(max_entity) + 1u);
    for (int32_t order = 0; order < static_cast<int32_t>(sequence.size()); ++order) {
      const auto layer = sequence[static_cast<size_t>(order)];
      EnttLayerIndex index;
      index.packed = static_cast<uint32_t>(layer.packed());
      index.order = order;
      if (tech.routingLayerStorage().contains(eccdb::TechRoutingLayerId{layer.entity()})) {
        index.routing = true;
        index.wrap_idx = routing_idx++;
      } else if (tech.cutLayerStorage().contains(eccdb::TechCutLayerId{layer.entity()})) {
        index.cut = true;
        index.wrap_idx = cut_idx++;
      }
      _by_entity[static_cast<size_t>(entt::to_entity(layer.entity()))] = index;
    }
    _routing_count = routing_idx;
    _cut_count = cut_idx;
  }

  [[nodiscard]] const EnttLayerIndex* find(eccdb::TechLayerId layer) const
  {
    if (!layer) {
      return nullptr;
    }
    const auto entity = static_cast<size_t>(entt::to_entity(layer.entity()));
    if (entity >= _by_entity.size()) {
      return nullptr;
    }
    const auto& index = _by_entity[entity];
    return index.order < 0 || index.packed != static_cast<uint32_t>(layer.packed()) ? nullptr : &index;
  }

  [[nodiscard]] int32_t routingCount() const { return _routing_count; }

 private:
  std::vector<EnttLayerIndex> _by_entity;
  int32_t _routing_count = 0;
  int32_t _cut_count = 0;
};

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

ConnectType connectTypeFromUse(eccdb::DesignSignalUse use)
{
  return use == eccdb::DesignSignalUse::kClock ? ConnectType::kClock : ConnectType::kSignal;
}

bool isCoreMaster(eccdb::LibraryCellMasterType type)
{
  return type >= eccdb::LibraryCellMasterType::kCore && type <= eccdb::LibraryCellMasterType::kEndcapBottomRight;
}

bool isPadMaster(eccdb::LibraryCellMasterType type)
{
  return type >= eccdb::LibraryCellMasterType::kPad && type <= eccdb::LibraryCellMasterType::kPadAreaIo;
}

int32_t irtOrientation(eccdb::DesignOrientation orientation)
{
  switch (orientation) {
    case eccdb::DesignOrientation::kN:
      return 1;
    case eccdb::DesignOrientation::kW:
      return 2;
    case eccdb::DesignOrientation::kS:
      return 3;
    case eccdb::DesignOrientation::kE:
      return 4;
    case eccdb::DesignOrientation::kFN:
      return 5;
    case eccdb::DesignOrientation::kFE:
      return 6;
    case eccdb::DesignOrientation::kFS:
      return 7;
    case eccdb::DesignOrientation::kFW:
      return 8;
  }
  return 0;
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

std::vector<eccdb::Rect> geometryRects(const eccdb::GeometryPool& pool, eccdb::GeometryHandle handle)
{
  std::vector<eccdb::Rect> rects;
  const auto stored = pool.rectangles(handle);
  rects.insert(rects.end(), stored.begin(), stored.end());
  for (uint32_t index = 0; index < pool.polygonCount(handle); ++index) {
    const auto points = pool.polygonPoints(handle, index);
    auto decomposed = eccdb::decomposePolygonToRectangles(points);
    rects.insert(rects.end(), decomposed.begin(), decomposed.end());
  }
  return rects;
}

template <typename Function>
void forEachGeometryRect(const eccdb::GeometryPool& pool, eccdb::GeometryHandle handle, Function&& function)
{
  for (const auto& rect : pool.rectangles(handle)) {
    std::invoke(function, rect);
  }
  for (uint32_t index = 0; index < pool.polygonCount(handle); ++index) {
    const auto points = pool.polygonPoints(handle, index);
    for (const auto& rect : eccdb::decomposePolygonToRectangles(points)) {
      std::invoke(function, rect);
    }
  }
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
  const EnttLayerIndex* index = nullptr;
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
  append(geometry.bottom_layer.layer(), geometryRects(tech.geometryPool(), geometry.bottom_geometry), false);
  append(eccdb::TechLayerId{geometry.cut_layer.entity()}, geometryRects(tech.geometryPool(), geometry.cut_geometry), true);
  append(geometry.top_layer.layer(), geometryRects(tech.geometryPool(), geometry.top_geometry), false);
  return shapes;
}

ViaShapes generatedViaShapes(const eccdb::DesignVia& via, eccdb::Point origin)
{
  // Match IdbViaMasterGenerate / IdbViaMaster::set_layer_shape: center the cut
  // array, then grow top/bottom enclosures from the cut bounding box. OFFSET is
  // stored by iDB but not applied when building wrap shapes.
  ViaShapes shapes;
  const auto& generated = via.generated;
  const int32_t rows = static_cast<int32_t>(std::max(generated.row_count, 1u));
  const int32_t columns = static_cast<int32_t>(std::max(generated.column_count, 1u));
  const int32_t origin_x
      = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOrigin) != 0 ? generated.origin.x : 0;
  const int32_t origin_y
      = (generated.flags & eccdb::DesignGeneratedViaFlag::kHasOrigin) != 0 ? generated.origin.y : 0;
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
         eccdb::Rect{.ll_x = cut_box.ll_x - generated.bottom_enclosure_x,
                             .ll_y = cut_box.ll_y - generated.bottom_enclosure_y,
                             .ur_x = cut_box.ur_x + generated.bottom_enclosure_x,
                             .ur_y = cut_box.ur_y + generated.bottom_enclosure_y},
         false);
  append(eccdb::TechLayerId{generated.top_layer.entity()},
         eccdb::Rect{.ll_x = cut_box.ll_x - generated.top_enclosure_x,
                             .ll_y = cut_box.ll_y - generated.top_enclosure_y,
                             .ur_x = cut_box.ur_x + generated.top_enclosure_x,
                             .ur_y = cut_box.ur_y + generated.top_enclosure_y},
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
  auto append_rect = [&](eccdb::TechLayerId layer, eccdb::Rect rect) {
    LayeredRect placed{.layer = layer, .rect = rect.offset(origin.x, origin.y)};
    if (tech.cutLayerStorage().contains(eccdb::TechCutLayerId{layer.entity()})) {
      shapes.cut.push_back(placed);
    } else {
      shapes.routing.push_back(placed);
    }
  };
  for (const auto& rect : via.rectangles) {
    append_rect(rect.layer, rect.rectangle);
  }
  for (const auto& polygon : via.polygons) {
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

void fillPrlTable(SpacingTable& table, const std::vector<int32_t>& widths, const std::vector<int32_t>& parallels,
                  const std::vector<int32_t>& cells)
{
  table.get_width_list() = widths;
  table.get_parallel_length_list() = parallels;
  auto& map = table.get_width_parallel_length_map();
  map.init(static_cast<int32_t>(widths.size()), static_cast<int32_t>(parallels.size()));
  for (int32_t x = 0; x < map.get_x_size(); ++x) {
    for (int32_t y = 0; y < map.get_y_size(); ++y) {
      const auto offset = static_cast<size_t>(x) * parallels.size() + static_cast<size_t>(y);
      map[x][y] = offset < cells.size() ? cells[offset] : 0;
    }
  }
}

int32_t effectiveMinWidth(const eccdb::TechRoutingLayer& tech_layer)
{
  // iDB lef_read defaults omitted MINWIDTH to WIDTH.
  return (tech_layer.flags & eccdb::TechRoutingLayerFlag::kHasMinWidth) != 0 ? tech_layer.min_width : tech_layer.width;
}

void wrapTrackAxisFromEnTT(RoutingLayer& routing_layer, const eccdb::TechRoutingLayer& tech_layer)
{
  // iDB lef_read defaults a missing OFFSET to pitch/2. EnTT stores the omitted
  // clause as offset_form=kNone and offset=0; apply the same default here.
  const int32_t offset_x = tech_layer.offset_form == eccdb::TechRoutingAxisValueForm::kNone ? tech_layer.pitch_x / 2
                                                                                                   : tech_layer.offset_x;
  const int32_t offset_y = tech_layer.offset_form == eccdb::TechRoutingAxisValueForm::kNone ? tech_layer.pitch_y / 2
                                                                                                   : tech_layer.offset_y;
  ScaleAxis& track_axis = routing_layer.get_track_axis();
  ScaleGrid x_track_grid;
  x_track_grid.set_start_line(offset_x);
  x_track_grid.set_step_length(tech_layer.pitch_x);
  track_axis.get_x_grid_list().push_back(x_track_grid);
  ScaleGrid y_track_grid;
  y_track_grid.set_start_line(offset_y);
  y_track_grid.set_step_length(tech_layer.pitch_y);
  track_axis.get_y_grid_list().push_back(y_track_grid);
}

void wrapRoutingDesignRuleFromEnTT(RoutingLayer& routing_layer, eccdb::TechRoutingLayerId layer_id,
                                   const eccdb::TechStore& tech)
{
  const auto& storage = tech.routingLayerStorage();
  const auto& tech_layer = storage.routingLayer(layer_id);
  const auto& name = tech.layerInfo(eccdb::TechLayerId{layer_id.entity()}).name;

  routing_layer.set_min_width(effectiveMinWidth(tech_layer));
  routing_layer.set_min_area(static_cast<int32_t>(tech_layer.area));

  const auto notch_ids = storage.spacingNotchLengthRules(layer_id);
  const auto lef58_notch_ids = storage.lef58SpacingNotchLengthRules(layer_id);
  if (!notch_ids.empty()) {
    routing_layer.set_notch_spacing(storage.rule(notch_ids.front()).min_spacing);
  } else if (!lef58_notch_ids.empty()) {
    routing_layer.set_notch_spacing(storage.rule(lef58_notch_ids.front()).min_spacing);
  } else {
    RTLOG.warn(Loc::current(), "The idb layer ", name, " notch spacing is empty!");
    routing_layer.set_notch_spacing(0);
  }

  const auto prl_ids = storage.prlSpacingTableRules(layer_id);
  if (!prl_ids.empty()) {
    const auto& src = storage.rule(prl_ids.front());
    fillPrlTable(routing_layer.get_prl_spacing_table(), {src.widths.begin(), src.widths.end()},
                 {src.parallel_run_lengths.begin(), src.parallel_run_lengths.end()}, {src.cells.begin(), src.cells.end()});
  } else {
    const auto spacing_ids = storage.spacingRules(layer_id);
    if (!spacing_ids.empty()) {
      struct SpacingRow
      {
        bool is_default = false;
        int32_t min_width = 0;
        int32_t min_spacing = 0;
      };
      std::vector<SpacingRow> rows;
      for (const auto spacing_id : spacing_ids) {
        const auto& src = storage.rule(spacing_id);
        rows.push_back(SpacingRow{.is_default = src.type == eccdb::TechRoutingSpacingType::kDefault,
                                  .min_width = src.min_width,
                                  .min_spacing = src.min_spacing});
      }
      std::sort(rows.begin(), rows.end(), [](const SpacingRow& lhs, const SpacingRow& rhs) {
        if (lhs.is_default != rhs.is_default) {
          return lhs.is_default;
        }
        return lhs.min_width < rhs.min_width;
      });
      std::vector<int32_t> widths;
      std::vector<int32_t> cells;
      for (const auto& row : rows) {
        widths.push_back(row.is_default ? 0 : row.min_width);
        cells.push_back(row.min_spacing);
      }
      fillPrlTable(routing_layer.get_prl_spacing_table(), widths, {0}, cells);
    } else {
      RTLOG.warn(Loc::current(), "The idb layer ", name, " spacing table is empty!");
      fillPrlTable(routing_layer.get_prl_spacing_table(), {0}, {0}, {0});
    }
  }

  const auto eol_ids = storage.lef58SpacingEolRules(layer_id);
  if (!eol_ids.empty()) {
    const auto& src = storage.rule(eol_ids.front());
    routing_layer.set_eol_spacing(src.eol_space);
    routing_layer.set_eol_ete((src.flags & eccdb::TechRoutingLef58SpacingEolRuleFlag::kHasEndToEnd) != 0 ? src.end_to_end_space
                                                                                                                 : 0);
    routing_layer.set_eol_within(src.eol_within);
  } else {
    RTLOG.warn(Loc::current(), "The idb layer ", name, " eol_spacing is empty!");
    routing_layer.set_eol_spacing(0);
    routing_layer.set_eol_ete(0);
    routing_layer.set_eol_within(0);
  }
}

void wrapCutDesignRuleFromEnTT(CutLayer& cut_layer, eccdb::TechCutLayerId layer_id, const eccdb::TechStore& tech)
{
  const auto& storage = tech.cutLayerStorage();
  const auto& name = tech.layerInfo(eccdb::TechLayerId{layer_id.entity()}).name;
  const auto spacing_ids = storage.spacingRules(layer_id);
  const auto table_ids = storage.lef58SpacingTableRules(layer_id);

  if (!spacing_ids.empty()) {
    const auto spacing = storage.spacingRule(spacing_ids.front()).spacing;
    cut_layer.set_curr_spacing(spacing);
    cut_layer.set_curr_prl(0);
    cut_layer.set_curr_prl_spacing(spacing);
  } else {
    const eccdb::TechCutLef58SpacingTableRule* same_table = nullptr;
    for (const auto table_id : table_ids) {
      const auto& table = storage.lef58SpacingTableRule(table_id);
      if ((table.flags & eccdb::TechCutLef58SpacingTableRuleFlag::kHasSecondLayer) == 0) {
        same_table = &table;
      }
    }
    if (same_table != nullptr && !same_table->cells.empty()) {
      const auto& cell = same_table->cells.front();
      cut_layer.set_curr_spacing(cell.cut_spacing1);
      cut_layer.set_curr_prl(-1 * same_table->prl);
      cut_layer.set_curr_prl_spacing(cell.cut_spacing2);
    } else {
      RTLOG.warn(Loc::current(), "The idb layer ", name, " curr layer spacing is empty!");
      cut_layer.set_curr_spacing(0);
      cut_layer.set_curr_prl(0);
      cut_layer.set_curr_prl_spacing(0);
    }
  }

  const auto eol_id = storage.lef58EolSpacingRule(layer_id);
  if (eol_id) {
    const auto& src = storage.lef58EolSpacingRule(eol_id);
    cut_layer.set_curr_eol_spacing(src.cut_spacing1);
    cut_layer.set_curr_eol_prl(-1 * src.prl);
    cut_layer.set_curr_eol_prl_spacing(src.cut_spacing2);
  } else {
    RTLOG.warn(Loc::current(), "The idb layer ", name, " eol_spacing is empty!");
    cut_layer.set_curr_eol_spacing(0);
    cut_layer.set_curr_eol_prl(0);
    cut_layer.set_curr_eol_prl_spacing(0);
  }

  const eccdb::TechCutLef58SpacingTableRule* below_table = nullptr;
  for (const auto table_id : table_ids) {
    const auto& table = storage.lef58SpacingTableRule(table_id);
    if ((table.flags & eccdb::TechCutLef58SpacingTableRuleFlag::kHasSecondLayer) != 0) {
      below_table = &table;
    }
  }
  if (below_table != nullptr && !below_table->cells.empty()) {
    const auto& cell = below_table->cells.front();
    cut_layer.set_below_spacing(cell.cut_spacing1);
    cut_layer.set_below_prl(-1 * below_table->prl);
    cut_layer.set_below_prl_spacing(cell.cut_spacing2);
  } else {
    cut_layer.set_below_spacing(0);
    cut_layer.set_below_prl(0);
    cut_layer.set_below_prl_spacing(0);
    RTLOG.warn(Loc::current(), "The idb layer ", name, " below layer spacing is empty!");
  }
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

struct ObstacleSink
{
  std::vector<Obstacle>& routing;
  std::vector<Obstacle>& cut;
  const EnttLayerTable& layers;
};

void appendObstacle(ObstacleSink& sink, const EnttLayerIndex& index, eccdb::Rect rect)
{
  if (index.wrap_idx < 0) {
    return;
  }
  std::vector<Obstacle>* target = nullptr;
  if (index.routing) {
    target = &sink.routing;
  } else if (index.cut) {
    target = &sink.cut;
  }
  if (target == nullptr) {
    return;
  }
  target->emplace_back(rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y, index.wrap_idx);
}

void appendObstacle(ObstacleSink& sink, eccdb::TechLayerId layer, eccdb::Rect rect)
{
  const auto* index = sink.layers.find(layer);
  if (index != nullptr) {
    appendObstacle(sink, *index, rect);
  }
}

void appendObstacle(ObstacleSink& sink, const LayeredRect& shape)
{
  if (shape.index != nullptr) {
    appendObstacle(sink, *shape.index, shape.rect);
  } else {
    appendObstacle(sink, shape.layer, shape.rect);
  }
}

struct LayerBox
{
  LayeredRect shape;
};

template <typename Function>
void forEachViaRoutingBox(const ViaShapes& shapes, std::vector<LayerBox>& boxes, Function&& function)
{
  boxes.clear();
  for (const auto& shape : shapes.routing) {
    auto it = std::find_if(boxes.begin(), boxes.end(), [&](const auto& entry) { return entry.shape.layer == shape.layer; });
    if (it == boxes.end()) {
      boxes.push_back(LayerBox{shape});
    } else {
      it->shape.rect = it->shape.rect.united(shape.rect);
    }
  }
  for (const auto& box : boxes) {
    std::invoke(function, box.shape);
  }
}

struct NetSkipInfo
{
  bool skip = false;
  int32_t port_pin_count = 0;
};

struct NetSkipCache
{
  struct Entry
  {
    uint32_t packed = 0;
    NetSkipInfo info;
    bool valid = false;
  };

  std::vector<Entry> values;

  [[nodiscard]] const NetSkipInfo* find(eccdb::DesignNetId net) const noexcept
  {
    const auto entity = static_cast<size_t>(entt::to_entity(net.entity()));
    if (entity >= values.size() || !values[entity].valid || values[entity].packed != static_cast<uint32_t>(net.packed())) {
      return nullptr;
    }
    return &values[entity].info;
  }
};

NetSkipInfo analyzeNetSkip(const eccdb::DesignStore& design, const WrapperEntityTables& entities, eccdb::DesignNetId net)
{
  NetSkipInfo info;
  if (!net) {
    info.skip = true;
    return info;
  }
  const auto& design_registry = design.designRegistry().registry();
  const auto* io_pin_component = design_registry.try_get<const eccdb::DesignNetIoPins>(net.entity());
  const auto* instance_pin_component = design_registry.try_get<const eccdb::DesignNetInstancePins>(net.entity());
  const auto io_pins = io_pin_component == nullptr ? std::span<const eccdb::DesignIoPinId>{}
                                                    : std::span<const eccdb::DesignIoPinId>{io_pin_component->values};
  const auto instance_pins = instance_pin_component == nullptr ? std::span<const eccdb::DesignInstancePinId>{}
                                                                : std::span<const eccdb::DesignInstancePinId>{instance_pin_component->values};
  const bool has_io_pin = io_pins.size() == 1;
  bool has_io_cell = false;
  if (instance_pins.size() == 1) {
    const auto& instance_pin = entities.instance_pins.get(instance_pins.front().entity());
    const auto& instance = entities.instances.get(instance_pin.instance.entity());
    const auto& master = entities.masters.get(instance.master.entity());
    has_io_cell = isPadMaster(master.type);
  }
  if (has_io_pin && has_io_cell) {
    info.skip = true;
    return info;
  }

  for (const auto pin_id : instance_pins) {
    const auto& pin = entities.instance_pins.get(pin_id.entity());
    if (entities.master_terms.get(pin.master_term.entity()).ports.empty()) {
      continue;
    }
    ++info.port_pin_count;
  }
  for (const auto pin_id : io_pins) {
    if (entities.io_pins.get(pin_id.entity()).ports.empty()) {
      continue;
    }
    ++info.port_pin_count;
  }
  info.skip = info.port_pin_count <= 1;
  return info;
}

bool netIsSkipping(const eccdb::DesignStore& design, const WrapperEntityTables& entities, eccdb::DesignNetId net, NetSkipCache& cache,
                   bool with_log)
{
  if (!net) {
    return true;
  }
  const auto* info = cache.find(net);
  if (info == nullptr) {
    const auto fallback = analyzeNetSkip(design, entities, net);
    if (with_log && fallback.port_pin_count >= 500) {
      RTLOG.warn(Loc::current(), "The ultra large net: ", design.netlistStorage().net(net).name, " has ", fallback.port_pin_count, " pins!");
    }
    return fallback.skip;
  }
  if (with_log && info->port_pin_count >= 500) {
    RTLOG.warn(Loc::current(), "The ultra large net: ", design.netlistStorage().net(net).name, " has ", info->port_pin_count, " pins!");
  }
  return info->skip;
}

// iDB wrapObstacleList calls isSkipping(pin->get_net()), which is the regular
// net only. Special-net-only pins (VDD/VSS) have a null regular net and become
// obstacles. Do not fall back to special_net here.
eccdb::DesignNetId instancePinNet(const eccdb::DesignInstancePin& pin)
{
  return pin.net;
}

eccdb::DesignNetId ioPinNet(const eccdb::DesignIoPin& pin)
{
  return pin.net;
}

void appendPinShapes(Pin& pin, const std::vector<LayeredRect>& routing, const std::vector<LayeredRect>& cut, const EnttLayerTable& layers)
{
  auto& routing_shapes = pin.get_routing_shape_list();
  auto& cut_shapes = pin.get_cut_shape_list();
  for (const auto& shape : routing) {
    const auto* index = shape.index != nullptr ? shape.index : layers.find(shape.layer);
    if (index == nullptr || !index->routing) {
      continue;
    }
    EXTLayerRect rect;
    rect.set_real_ll(shape.rect.ll_x, shape.rect.ll_y);
    rect.set_real_ur(shape.rect.ur_x, shape.rect.ur_y);
    rect.set_layer_idx(index->wrap_idx);
    routing_shapes.push_back(std::move(rect));
  }
  for (const auto& shape : cut) {
    const auto* index = shape.index != nullptr ? shape.index : layers.find(shape.layer);
    if (index == nullptr || !index->cut) {
      continue;
    }
    EXTLayerRect rect;
    rect.set_real_ll(shape.rect.ll_x, shape.rect.ll_y);
    rect.set_real_ur(shape.rect.ur_x, shape.rect.ur_y);
    rect.set_layer_idx(index->wrap_idx);
    cut_shapes.push_back(std::move(rect));
  }
}

MacroPinEdge macroPinEdge(Pin& pin)
{
  const auto& inst_bbox = pin.get_inst_bbox();
  int32_t north_count = 0;
  int32_t south_count = 0;
  int32_t east_count = 0;
  int32_t west_count = 0;
  for (const auto& shape : pin.get_routing_shape_list()) {
    const int32_t north_dist = inst_bbox.get_ur_y() - shape.get_real_ur_y();
    const int32_t south_dist = shape.get_real_ll_y() - inst_bbox.get_ll_y();
    const int32_t east_dist = inst_bbox.get_ur_x() - shape.get_real_ur_x();
    const int32_t west_dist = shape.get_real_ll_x() - inst_bbox.get_ll_x();
    const int32_t min_dist = std::min({north_dist, south_dist, east_dist, west_dist});
    if (north_dist == min_dist) {
      ++north_count;
    } else if (south_dist == min_dist) {
      ++south_count;
    } else if (east_dist == min_dist) {
      ++east_count;
    } else {
      ++west_count;
    }
  }
  const int32_t max_count = std::max({north_count, south_count, east_count, west_count});
  if (max_count == 0) {
    return MacroPinEdge::kNone;
  }
  if (north_count == max_count) {
    return MacroPinEdge::kNorth;
  }
  if (south_count == max_count) {
    return MacroPinEdge::kSouth;
  }
  if (east_count == max_count) {
    return MacroPinEdge::kEast;
  }
  return MacroPinEdge::kWest;
}

int32_t preferredConnectionLayer(Pin& pin, std::vector<RoutingLayer>& routing_layers)
{
  const auto preferred_direction = pin.get_preferred_escape_direction();
  int32_t best_layer_idx = -1;
  int32_t best_layer_order = -1;
  int32_t fallback_layer_idx = -1;
  int32_t fallback_layer_order = -1;
  for (const auto& shape : pin.get_routing_shape_list()) {
    for (auto& layer : routing_layers) {
      if (layer.get_layer_idx() != shape.get_layer_idx()) {
        continue;
      }
      if (layer.get_layer_order() > fallback_layer_order) {
        fallback_layer_order = layer.get_layer_order();
        fallback_layer_idx = layer.get_layer_idx();
      }
      if (layer.get_prefer_direction() == preferred_direction && layer.get_layer_order() > best_layer_order) {
        best_layer_order = layer.get_layer_order();
        best_layer_idx = layer.get_layer_idx();
      }
    }
  }
  return best_layer_idx == -1 ? fallback_layer_idx : best_layer_idx;
}

void appendViaToPin(Pin& pin, const ViaShapes& shapes, const EnttLayerTable& layers, std::vector<LayerBox>& boxes)
{
  auto& routing_shapes = pin.get_routing_shape_list();
  auto append_box = [&](const LayeredRect& source) {
    const auto* index = source.index != nullptr ? source.index : layers.find(source.layer);
    if (index == nullptr || !index->routing) {
      return;
    }
    EXTLayerRect target;
    target.set_real_ll(source.rect.ll_x, source.rect.ll_y);
    target.set_real_ur(source.rect.ur_x, source.rect.ur_y);
    target.set_layer_idx(index->wrap_idx);
    routing_shapes.push_back(std::move(target));
  };
  forEachViaRoutingBox(shapes, boxes, append_box);
  appendPinShapes(pin, {}, shapes.cut, layers);
}

void appendOffsetViaToPin(Pin& pin, const ViaShapes& shapes, eccdb::Point offset, const EnttLayerTable& layers,
                          std::vector<LayerBox>& boxes)
{
  boxes.clear();
  for (const auto& shape : shapes.routing) {
    LayeredRect placed = shape;
    placed.rect = placed.rect.offset(offset.x, offset.y);
    auto it = std::find_if(boxes.begin(), boxes.end(), [&](const auto& entry) { return entry.shape.layer == placed.layer; });
    if (it == boxes.end()) {
      boxes.push_back(LayerBox{placed});
    } else {
      it->shape.rect = it->shape.rect.united(placed.rect);
    }
  }
  for (const auto& box : boxes) {
    const auto* index = box.shape.index != nullptr ? box.shape.index : layers.find(box.shape.layer);
    if (index == nullptr || !index->routing) {
      continue;
    }
    EXTLayerRect target;
    target.set_real_ll(box.shape.rect.ll_x, box.shape.rect.ll_y);
    target.set_real_ur(box.shape.rect.ur_x, box.shape.rect.ur_y);
    target.set_layer_idx(index->wrap_idx);
    pin.get_routing_shape_list().push_back(std::move(target));
  }
  for (const auto& shape : shapes.cut) {
    LayeredRect placed = shape;
    placed.rect = placed.rect.offset(offset.x, offset.y);
    const auto* index = placed.index != nullptr ? placed.index : layers.find(placed.layer);
    if (index == nullptr || !index->cut) {
      continue;
    }
    EXTLayerRect target;
    target.set_real_ll(placed.rect.ll_x, placed.rect.ll_y);
    target.set_real_ur(placed.rect.ur_x, placed.rect.ur_y);
    target.set_layer_idx(index->wrap_idx);
    pin.get_cut_shape_list().push_back(std::move(target));
  }
}

template <typename Geometry>
class OrientedGeometryCache
{
 public:
  template <typename Id, typename Builder>
  const Geometry& get(Id id, eccdb::DesignOrientation orientation, Builder&& builder)
  {
    auto& entries = _values[static_cast<uint32_t>(id.packed())];
    auto& entry = entries[static_cast<size_t>(orientation)];
    if (!entry.has_value()) {
      entry.emplace(std::invoke(std::forward<Builder>(builder)));
    }
    return *entry;
  }

  template <typename Id>
  [[nodiscard]] const Geometry& at(Id id, eccdb::DesignOrientation orientation) const
  {
    return _values.at(static_cast<uint32_t>(id.packed()))[static_cast<size_t>(orientation)].value();
  }

 private:
  std::unordered_map<uint32_t, std::array<std::optional<Geometry>, 8>> _values;
};

struct CachedMasterGeometry
{
  std::vector<LayeredRect> routing;
  std::vector<LayeredRect> cut;
  std::vector<ViaShapes> vias;
  std::vector<LayeredRect> obstacle_routing;
  std::vector<LayeredRect> obstacle_cut;
};

void resolveCachedLayers(ViaShapes& shapes, const EnttLayerTable& layers)
{
  auto resolve = [&layers](LayeredRect& shape) { shape.index = layers.find(shape.layer); };
  for (auto& shape : shapes.routing) {
    resolve(shape);
  }
  for (auto& shape : shapes.cut) {
    resolve(shape);
  }
}

void finalizeCachedObstacleGeometry(CachedMasterGeometry& geometry)
{
  const auto append_routing = [&geometry](const LayeredRect& shape) {
    if (shape.index != nullptr && shape.index->routing && shape.index->wrap_idx >= 0) {
      geometry.obstacle_routing.push_back(shape);
    }
  };
  const auto append_cut = [&geometry](const LayeredRect& shape) {
    if (shape.index != nullptr && shape.index->cut && shape.index->wrap_idx >= 0) {
      geometry.obstacle_cut.push_back(shape);
    }
  };

  geometry.obstacle_routing.reserve(geometry.routing.size() + geometry.vias.size() * 2u);
  geometry.obstacle_cut.reserve(geometry.cut.size());
  for (const auto& shape : geometry.routing) {
    append_routing(shape);
  }
  for (const auto& shape : geometry.cut) {
    append_cut(shape);
  }

  std::vector<LayerBox> boxes;
  for (const auto& via : geometry.vias) {
    forEachViaRoutingBox(via, boxes, append_routing);
    for (const auto& shape : via.cut) {
      append_cut(shape);
    }
  }
}

class FixedObstacleSink
{
 public:
  FixedObstacleSink(std::span<Obstacle> routing, std::span<Obstacle> cut) : _routing(routing), _cut(cut) {}

  void append(const CachedMasterGeometry& geometry, eccdb::Point origin)
  {
    for (const auto& shape : geometry.obstacle_routing) {
      const auto rect = shape.rect.offset(origin.x, origin.y);
      _routing[_routing_index++] = Obstacle(rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y, shape.index->wrap_idx);
    }
    for (const auto& shape : geometry.obstacle_cut) {
      const auto rect = shape.rect.offset(origin.x, origin.y);
      _cut[_cut_index++] = Obstacle(rect.ll_x, rect.ll_y, rect.ur_x, rect.ur_y, shape.index->wrap_idx);
    }
  }

 private:
  std::span<Obstacle> _routing;
  std::span<Obstacle> _cut;
  size_t _routing_index = 0;
  size_t _cut_index = 0;
};

struct InstanceObstacleWork
{
  eccdb::LibraryCellMasterId master;
  eccdb::Point origin;
  eccdb::DesignOrientation orientation = eccdb::DesignOrientation::kN;
  std::span<const eccdb::DesignInstancePinId> pins;
  const CachedMasterGeometry* obs_geometry = nullptr;
  size_t routing_offset = 0;
  size_t routing_count = 0;
  size_t cut_offset = 0;
  size_t cut_count = 0;
};

}  // namespace

void RTInterface::wrapDatabaseFromEnTT()
{
  if (_design == nullptr || _tech == nullptr || _library == nullptr) {
    RTLOG.error(Loc::current(), "EnTT design source is incomplete.");
    return;
  }

  const auto& design = *_design;
  const auto& tech = *_tech;
  const auto& library = *_library;
  // The wrapper only reads the imported registries; use direct component views
  // in its hot loops to avoid repeating storage-layer validity checks.
  const auto& design_registry = design.designRegistry().registry();
  const auto& library_registry = library.libraryRegistry().registry();
  const WrapperEntityTables entities{design_registry, library_registry};
  const auto net_instance_pins_for = [&design_registry](eccdb::DesignNetId net) {
    const auto* pins = design_registry.try_get<const eccdb::DesignNetInstancePins>(net.entity());
    return pins == nullptr ? std::span<const eccdb::DesignInstancePinId>{}
                           : std::span<const eccdb::DesignInstancePinId>{pins->values};
  };
  const auto net_io_pins_for = [&design_registry](eccdb::DesignNetId net) {
    const auto* pins = design_registry.try_get<const eccdb::DesignNetIoPins>(net.entity());
    return pins == nullptr ? std::span<const eccdb::DesignIoPinId>{}
                           : std::span<const eccdb::DesignIoPinId>{pins->values};
  };
  const EnttLayerTable layers(tech);
  OrientedGeometryCache<CachedMasterGeometry> obs_geometry_cache;
  OrientedGeometryCache<CachedMasterGeometry> term_geometry_cache;
  const auto build_obs_geometry = [&](eccdb::LibraryCellMasterId master_id, eccdb::DesignOrientation orientation) {
    CachedMasterGeometry geometry;
    const auto& master = entities.masters.get(master_id.entity());
    const int32_t width = static_cast<int32_t>(master.width);
    const int32_t height = static_cast<int32_t>(master.height);
    const auto* obs = entities.master_obs.tryGet(master_id.entity());
    if (obs != nullptr) {
      for (const auto& clause : obs->layer_clauses) {
        const auto* index = layers.find(clause.layer);
        if (index == nullptr) {
          continue;
        }
        forEachGeometryRect(library.geometryPool(), clause.geometry, [&](const eccdb::Rect& rect) {
          LayeredRect shape{.layer = clause.layer, .rect = placeMasterRect(rect, {}, orientation, width, height), .index = index};
          if (index->cut) {
            geometry.cut.push_back(shape);
          } else {
            geometry.routing.push_back(shape);
          }
        });
      }
      for (const auto& via : obs->vias) {
        const auto placed = placeMasterPoint(via.origin, {}, orientation, width, height);
        auto shapes = techViaShapes(tech, via.via, placed);
        resolveCachedLayers(shapes, layers);
        geometry.vias.push_back(std::move(shapes));
      }
    }
    finalizeCachedObstacleGeometry(geometry);
    return geometry;
  };
  const auto build_term_geometry = [&](eccdb::LibraryMasterTermId term_id, eccdb::DesignOrientation orientation) {
    CachedMasterGeometry geometry;
    const auto& term = entities.master_terms.get(term_id.entity());
    const auto& master = entities.masters.get(term.master.entity());
    const int32_t width = static_cast<int32_t>(master.width);
    const int32_t height = static_cast<int32_t>(master.height);
    for (const auto port_id : term.ports) {
      const auto& port = entities.master_ports.get(port_id.entity());
      for (const auto& clause : port.layer_clauses) {
        const auto* index = layers.find(clause.layer);
        if (index == nullptr) {
          continue;
        }
        forEachGeometryRect(library.geometryPool(), clause.geometry, [&](const eccdb::Rect& rect) {
          LayeredRect shape{.layer = clause.layer, .rect = placeMasterRect(rect, {}, orientation, width, height), .index = index};
          if (index->cut) {
            geometry.cut.push_back(shape);
          } else {
            geometry.routing.push_back(shape);
          }
        });
      }
      for (const auto& via : port.vias) {
        const auto placed = placeMasterPoint(via.origin, {}, orientation, width, height);
        auto shapes = techViaShapes(tech, via.via, placed);
        resolveCachedLayers(shapes, layers);
        geometry.vias.push_back(std::move(shapes));
      }
    }
    finalizeCachedObstacleGeometry(geometry);
    return geometry;
  };
  NetSkipCache skip_cache;
  uint32_t max_net_entity = 0;
  bool has_regular_net = false;
  design.netlistStorage().forEachRegularNet([&](const auto net_id, const auto&) {
    has_regular_net = true;
    max_net_entity = std::max(max_net_entity, static_cast<uint32_t>(entt::to_entity(net_id.entity())));
  });
  skip_cache.values.resize(has_regular_net ? static_cast<size_t>(max_net_entity) + 1u : 0u);
  design.netlistStorage().forEachRegularNet([&](const auto net_id, const auto&) {
    auto& entry = skip_cache.values[static_cast<size_t>(entt::to_entity(net_id.entity()))];
    entry.packed = static_cast<uint32_t>(net_id.packed());
    entry.info = analyzeNetSkip(design, entities, net_id);
    entry.valid = true;
  });

  if (design.globalStorage().hasInfo()) {
    RTDM.getDatabase().set_design_name(design.globalStorage().info().name);
    RTDM.getDatabase().set_micron_dbu(design.globalStorage().info().database_units_per_micron);
  }
  if (tech.globalStorage().hasManufacturingGrid()) {
    RTDM.getDatabase().set_manufacture_grid(tech.globalStorage().getManufacturingGrid().value);
  }
  if (design.globalStorage().hasDieArea()) {
    const auto die_bounds = design.globalStorage().dieBounds();
    auto& die = RTDM.getDatabase().get_die();
    die.set_real_ll(die_bounds.ll_x, die_bounds.ll_y);
    die.set_real_ur(die_bounds.ur_x, die_bounds.ur_y);
  }

  {
    int32_t start_x = std::numeric_limits<int32_t>::max();
    int32_t start_y = std::numeric_limits<int32_t>::max();
    int32_t height = -1;
    const auto rows = design.floorplanStorage().rows();
    for (const auto row_id : rows) {
      const auto& row = design.floorplanStorage().row(row_id);
      start_x = std::min(start_x, row.origin.x);
      start_y = std::min(start_y, row.origin.y);
    }
    if (!rows.empty()) {
      const auto& first = design.floorplanStorage().row(rows.front());
      if (first.site && library.siteStorage().contains(first.site)) {
        const auto& site = library.siteStorage().site(first.site);
        height = first.repeat_count_y == 1 ? site.height : site.width;
      } else {
        height = first.step_y;
      }
    }
    auto& row = RTDM.getDatabase().get_row();
    row.set_start_x(start_x);
    row.set_start_y(start_y);
    row.set_height(height);
  }

  auto& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  auto& cut_layer_list = RTDM.getDatabase().get_cut_layer_list();
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
      wrapTrackAxisFromEnTT(routing_layer, tech_layer);
      wrapRoutingDesignRuleFromEnTT(routing_layer, routing_id, tech);
      routing_layer_list.push_back(std::move(routing_layer));
    } else if (index->cut) {
      const auto cut_id = eccdb::TechCutLayerId{layer.entity()};
      CutLayer cut_layer;
      cut_layer.set_layer_idx(index->wrap_idx);
      cut_layer.set_layer_order(index->order);
      cut_layer.set_layer_name(tech.layerInfo(layer).name);
      wrapCutDesignRuleFromEnTT(cut_layer, cut_id, tech);
      cut_layer_list.push_back(std::move(cut_layer));
    }
  }
  wrapLayerInfo();

  {
    auto& layer_via_master_list = RTDM.getDatabase().get_layer_via_master_list();
    layer_via_master_list.resize(static_cast<size_t>(std::max(layers.routingCount(), 0)));
    if (layer_via_master_list.empty()) {
      RTLOG.error(Loc::current(), "Via list in tech lef is empty!");
    }
    for (const auto via_id : tech.viaMasterStorage().viaMasters()) {
      const auto& master = tech.viaMasterStorage().viaMaster(via_id);
      if ((master.flags & eccdb::TechViaMasterFlag::kDefault) == 0) {
        continue;
      }
      const auto& geometry = tech.viaMasterStorage().geometry(via_id);
      const auto top_rects = geometryRects(tech.geometryPool(), geometry.top_geometry);
      const auto bottom_rects = geometryRects(tech.geometryPool(), geometry.bottom_geometry);
      const auto cut_rects = geometryRects(tech.geometryPool(), geometry.cut_geometry);
      const auto* top_index = layers.find(geometry.top_layer.layer());
      const auto* bottom_index = layers.find(geometry.bottom_layer.layer());
      const auto* cut_index = layers.find(eccdb::TechLayerId{geometry.cut_layer.entity()});
      if (top_index == nullptr || bottom_index == nullptr || cut_index == nullptr) {
        continue;
      }
      const auto top_box = boundingBox(top_rects);
      const auto bottom_box = boundingBox(bottom_rects);
      ViaMaster via_master;
      via_master.set_via_name(master.name);
      via_master.set_above_enclosure(LayerRect(top_box.ll_x, top_box.ll_y, top_box.ur_x, top_box.ur_y, top_index->wrap_idx));
      if (geometry.top_layer.kind == eccdb::TechConductorLayerKind::kRouting) {
        via_master.set_above_direction(directionFromTech(
            tech.routingLayerStorage().routingLayer(eccdb::TechRoutingLayerId{geometry.top_layer.entity}).direction));
      }
      via_master.set_below_enclosure(LayerRect(bottom_box.ll_x, bottom_box.ll_y, bottom_box.ur_x, bottom_box.ur_y, bottom_index->wrap_idx));
      if (geometry.bottom_layer.kind == eccdb::TechConductorLayerKind::kRouting) {
        via_master.set_below_direction(directionFromTech(
            tech.routingLayerStorage().routingLayer(eccdb::TechRoutingLayerId{geometry.bottom_layer.entity}).direction));
      }
      std::vector<PlanarRect> cut_shape_list;
      for (const auto& rect : cut_rects) {
        PlanarRect cut_shape;
        cut_shape.set_ll(rect.ll_x, rect.ll_y);
        cut_shape.set_ur(rect.ur_x, rect.ur_y);
        cut_shape_list.push_back(std::move(cut_shape));
      }
      via_master.set_cut_shape_list(cut_shape_list);
      via_master.set_cut_layer_idx(cut_index->wrap_idx);
      layer_via_master_list.front().push_back(std::move(via_master));
    }
  }
  {
    auto& routing_obstacle_list = RTDM.getDatabase().get_routing_obstacle_list();
    auto& cut_obstacle_list = RTDM.getDatabase().get_cut_obstacle_list();
    std::vector<int32_t> active_routing_layer_indices;
    if (!routing_layer_list.empty()) {
      int32_t bottom_layer_idx = 0;
      int32_t top_layer_idx = static_cast<int32_t>(routing_layer_list.size()) - 1;
      const std::string& bottom_layer_name = RTDM.getConfig().bottom_routing_layer;
      const std::string& top_layer_name = RTDM.getConfig().top_routing_layer;
      for (int32_t index = 0; index < static_cast<int32_t>(routing_layer_list.size()); ++index) {
        if (!bottom_layer_name.empty() && routing_layer_list[static_cast<size_t>(index)].get_layer_name() == bottom_layer_name) {
          bottom_layer_idx = index;
        }
        if (!top_layer_name.empty() && routing_layer_list[static_cast<size_t>(index)].get_layer_name() == top_layer_name) {
          top_layer_idx = index;
        }
      }
      for (int32_t index = bottom_layer_idx; index <= top_layer_idx; ++index) {
        active_routing_layer_indices.push_back(routing_layer_list[static_cast<size_t>(index)].get_layer_idx());
      }
    }
    std::vector<InstanceObstacleWork> instance_work;
    instance_work.reserve(design.netlistStorage().instanceCount());
    size_t instance_routing_count = 0;
    size_t instance_cut_count = 0;

    design.netlistStorage().forEachInstance([&](const auto, const auto& instance, const auto instance_pins) {
      const auto& obs_geometry = obs_geometry_cache.get(instance.master, instance.orientation, [&] {
        return build_obs_geometry(instance.master, instance.orientation);
      });
      InstanceObstacleWork work{.master = instance.master,
                                .origin = instance.origin,
                                .orientation = instance.orientation,
                                .pins = instance_pins,
                                .obs_geometry = &obs_geometry,
                                .routing_offset = instance_routing_count,
                                .routing_count = obs_geometry.obstacle_routing.size(),
                                .cut_offset = instance_cut_count,
                                .cut_count = obs_geometry.obstacle_cut.size()};
      for (const auto pin_id : instance_pins) {
        const auto& pin = entities.instance_pins.get(pin_id.entity());
        const auto& term = entities.master_terms.get(pin.master_term.entity());
        if (term.ports.empty()) {
          continue;
        }
        // This serial traversal sees every instance pin.  Populate the cache
        // here so the later OpenMP net loop performs const lookups only.
        const auto& term_geometry = term_geometry_cache.get(pin.master_term, instance.orientation, [&] {
          return build_term_geometry(pin.master_term, instance.orientation);
        });
        if (!netIsSkipping(design, entities, instancePinNet(pin), skip_cache, false)) {
          continue;
        }
        work.routing_count += term_geometry.obstacle_routing.size();
        work.cut_count += term_geometry.obstacle_cut.size();
      }
      instance_routing_count += work.routing_count;
      instance_cut_count += work.cut_count;
      instance_work.push_back(work);
    });
    std::vector<Obstacle> trailing_routing_obstacles;
    std::vector<Obstacle> trailing_cut_obstacles;
    ObstacleSink sink{trailing_routing_obstacles, trailing_cut_obstacles, layers};

    design.netlistStorage().forEachSpecialNet([&](const auto net_id, const auto&) {
      design.routingStorage().forEachWire(net_id, [&](const auto wire_id, const auto&) {
        const auto path_count = design.routingStorage().pathCount(wire_id);
        for (std::size_t path_index = 0; path_index < path_count; ++path_index) {
          const auto path = design.routingStorage().path(wire_id, path_index);
          const auto points = path.points();
          const auto* path_layer = layers.find(eccdb::TechLayerId{path.layer().entity()});
          if (path.vias().empty()) {
            // Keep compatibility with DefRead::parse_pdn_wire: FLUSHPOINT and
            // VIRTUALPOINT tokens are ignored there, and every non-via path
            // still contributes one segment (a zero box when fewer than two
            // ordinary POINT tokens remain).
            eccdb::Point first;
            eccdb::Point second;
            std::size_t ordinary_point_count = 0;
            for (const auto& point : points) {
              if (point.flags != 0u) {
                continue;
              }
              if (ordinary_point_count == 0) {
                first = point.position;
              } else {
                second = point.position;
                ordinary_point_count = 2;
                break;
              }
              ordinary_point_count = 1;
            }
            const auto rectangle = ordinary_point_count == 2 ? wireSegmentBox(first, second, path.width()) : eccdb::Rect{};
            if (path_layer != nullptr) {
              appendObstacle(sink, *path_layer, rectangle);
            }
          }
          for (const auto via : path.vias()) {
            if (via.point_index >= points.size()) {
              continue;
            }
            const auto base = points[via.point_index].position;
            const uint32_t rows = std::max(via.rows, 1u);
            const uint32_t columns = std::max(via.columns, 1u);
            for (uint32_t row = 0; row < rows; ++row) {
              for (uint32_t column = 0; column < columns; ++column) {
                const eccdb::Point origin{base.x + static_cast<int32_t>(column) * via.step_x,
                                                  base.y + static_cast<int32_t>(row) * via.step_y};
                const auto shapes = resolveViaShapes(design, tech, via.tech_via, via.design_via, origin);
                for (const auto& shape : shapes.routing) {
                  appendObstacle(sink, shape);
                }
                for (const auto& shape : shapes.cut) {
                  appendObstacle(sink, shape);
                }
              }
            }
          }
          for (const auto& rectangle : path.rectangles()) {
            if (rectangle.point_index >= points.size()) {
              continue;
            }
            const auto base = points[rectangle.point_index].position;
            if (path_layer != nullptr) {
              appendObstacle(sink, *path_layer, rectangle.delta.offset(base.x, base.y));
            }
          }
        }
      });
      if (const auto* geometry = design.routingStorage().netGeometry(net_id); geometry != nullptr) {
        for (const auto& rect : geometry->rectangles) {
          appendObstacle(sink, eccdb::TechLayerId{rect.layer.entity()}, rect.rectangle);
        }
        for (const auto& polygon : geometry->polygons) {
          for (const auto& rect : eccdb::decomposePolygonToRectangles(polygon.points)) {
            appendObstacle(sink, eccdb::TechLayerId{polygon.layer.entity()}, rect);
          }
        }
        for (const auto& via : geometry->vias) {
          for (const auto& origin : via.origins) {
                const auto shapes = resolveViaShapes(design, tech, via.tech_via, via.design_via, origin);
                for (const auto& shape : shapes.routing) {
                  appendObstacle(sink, shape);
                }
                for (const auto& shape : shapes.cut) {
                  appendObstacle(sink, shape);
                }
          }
        }
      }
    });

    design.netlistStorage().forEachIoPin([&](const auto, const auto& io_pin) {
      if (!netIsSkipping(design, entities, ioPinNet(io_pin), skip_cache, false)) {
        return;
      }
      for (const auto& port : io_pin.ports) {
        if (port.placement_status == eccdb::DesignPlacementStatus::kUnplaced
            && (port.flags & eccdb::DesignIoPinPortFlag::kHasPlacement) == 0) {
          continue;
        }
        for (const auto& rect : port.rectangles) {
          appendObstacle(sink, rect.layer, placeMasterRect(rect.rectangle, port.origin, port.orientation, 0, 0));
        }
        for (const auto& polygon : port.polygons) {
          for (const auto& rect : eccdb::decomposePolygonToRectangles(polygon.points)) {
            appendObstacle(sink, polygon.layer, placeMasterRect(rect, port.origin, port.orientation, 0, 0));
          }
        }
      }
    });
    routing_obstacle_list.reserve(instance_routing_count + trailing_routing_obstacles.size());
    cut_obstacle_list.reserve(instance_cut_count + trailing_cut_obstacles.size());
    routing_obstacle_list.resize(instance_routing_count);
    cut_obstacle_list.resize(instance_cut_count);
    const auto routing_span = std::span<Obstacle>{routing_obstacle_list};
    const auto cut_span = std::span<Obstacle>{cut_obstacle_list};
#pragma omp parallel for schedule(static)
    for (size_t index = 0; index < instance_work.size(); ++index) {
      const auto& work = instance_work[index];
      FixedObstacleSink instance_sink(routing_span.subspan(work.routing_offset, work.routing_count),
                                      cut_span.subspan(work.cut_offset, work.cut_count));
      instance_sink.append(*work.obs_geometry, work.origin);
      for (const auto pin_id : work.pins) {
        const auto& pin = entities.instance_pins.get(pin_id.entity());
        const auto& term = entities.master_terms.get(pin.master_term.entity());
        if (term.ports.empty() || !netIsSkipping(design, entities, instancePinNet(pin), skip_cache, false)) {
          continue;
        }
        instance_sink.append(term_geometry_cache.at(pin.master_term, work.orientation), work.origin);
      }
    }
    std::vector<Obstacle> derived_macro_obstacles;
    for (const auto& work : instance_work) {
      const auto& master = entities.masters.get(work.master.entity());
      const bool is_block = master.type == eccdb::LibraryCellMasterType::kBlock
                            || master.type == eccdb::LibraryCellMasterType::kBlockBlackbox
                            || master.type == eccdb::LibraryCellMasterType::kBlockSoft;
      if (!is_block || active_routing_layer_indices.empty()) {
        continue;
      }

      const auto body = placeMasterRect(eccdb::Rect{.ll_x = 0,
                                                            .ll_y = 0,
                                                            .ur_x = static_cast<int32_t>(master.width),
                                                            .ur_y = static_cast<int32_t>(master.height)},
                                        work.origin, work.orientation, static_cast<int32_t>(master.width),
                                        static_cast<int32_t>(master.height));
      std::vector<std::vector<eccdb::Rect>> obstacles_by_layer(static_cast<size_t>(layers.routingCount()));
      std::vector<int32_t> x_coordinates{body.ll_x, body.ur_x};
      std::vector<int32_t> y_coordinates{body.ll_y, body.ur_y};
      for (const auto& shape : work.obs_geometry->routing) {
        if (shape.index == nullptr || !shape.index->routing
            || std::ranges::find(active_routing_layer_indices, shape.index->wrap_idx) == active_routing_layer_indices.end()) {
          continue;
        }
        const auto placed = shape.rect.offset(work.origin.x, work.origin.y);
        if (!body.intersects(placed, false)) {
          continue;
        }
        const eccdb::Rect overlap{.ll_x = std::max(body.ll_x, placed.ll_x),
                                          .ll_y = std::max(body.ll_y, placed.ll_y),
                                          .ur_x = std::min(body.ur_x, placed.ur_x),
                                          .ur_y = std::min(body.ur_y, placed.ur_y)};
        obstacles_by_layer[static_cast<size_t>(shape.index->wrap_idx)].push_back(overlap);
        x_coordinates.push_back(overlap.ll_x);
        x_coordinates.push_back(overlap.ur_x);
        y_coordinates.push_back(overlap.ll_y);
        y_coordinates.push_back(overlap.ur_y);
      }
      std::ranges::sort(x_coordinates);
      x_coordinates.erase(std::ranges::unique(x_coordinates).begin(), x_coordinates.end());
      std::ranges::sort(y_coordinates);
      y_coordinates.erase(std::ranges::unique(y_coordinates).begin(), y_coordinates.end());
      for (size_t x = 0; x + 1 < x_coordinates.size(); ++x) {
        for (size_t y = 0; y + 1 < y_coordinates.size(); ++y) {
          const eccdb::Rect tile{.ll_x = x_coordinates[x],
                                         .ll_y = y_coordinates[y],
                                         .ur_x = x_coordinates[x + 1],
                                         .ur_y = y_coordinates[y + 1]};
          int32_t available_layer = -1;
          int32_t available_count = 0;
          for (const int32_t layer_idx : active_routing_layer_indices) {
            const auto& layer_obstacles = obstacles_by_layer[static_cast<size_t>(layer_idx)];
            const bool blocked = std::ranges::any_of(
                layer_obstacles, [&](const eccdb::Rect obstacle) { return tile.intersects(obstacle, false); });
            if (!blocked) {
              available_layer = layer_idx;
              ++available_count;
            }
          }
          if (available_count == 1) {
            derived_macro_obstacles.emplace_back(tile.ll_x, tile.ll_y, tile.ur_x, tile.ur_y, available_layer);
          }
        }
      }
    }
    routing_obstacle_list.reserve(routing_obstacle_list.size() + derived_macro_obstacles.size() + trailing_routing_obstacles.size());
    routing_obstacle_list.insert(routing_obstacle_list.end(), std::make_move_iterator(derived_macro_obstacles.begin()),
                                 std::make_move_iterator(derived_macro_obstacles.end()));
    routing_obstacle_list.insert(routing_obstacle_list.end(), std::make_move_iterator(trailing_routing_obstacles.begin()),
                                 std::make_move_iterator(trailing_routing_obstacles.end()));
    cut_obstacle_list.insert(cut_obstacle_list.end(), std::make_move_iterator(trailing_cut_obstacles.begin()),
                             std::make_move_iterator(trailing_cut_obstacles.end()));
  }

  {
    auto& macro_list = RTDM.getDatabase().get_macro_list();
    design.netlistStorage().forEachInstance([&](const auto, const auto& instance, const auto) {
      const auto& master = entities.masters.get(instance.master.entity());
      if (master.type != eccdb::LibraryCellMasterType::kBlock
          && master.type != eccdb::LibraryCellMasterType::kBlockBlackbox
          && master.type != eccdb::LibraryCellMasterType::kBlockSoft) {
        return;
      }
      const auto body = placeMasterRect(eccdb::Rect{.ll_x = 0,
                                                            .ll_y = 0,
                                                            .ur_x = static_cast<int32_t>(master.width),
                                                            .ur_y = static_cast<int32_t>(master.height)},
                                        instance.origin, instance.orientation, static_cast<int32_t>(master.width),
                                        static_cast<int32_t>(master.height));
      Macro macro;
      macro.set_inst_name(instance.name);
      macro.set_body_rect(PlanarRect{body.ll_x, body.ll_y, body.ur_x, body.ur_y});
      macro_list.push_back(std::move(macro));
    });
  }

  {
    auto& net_list = RTDM.getDatabase().get_net_list();
    std::vector<eccdb::DesignNetId> valid_nets;
    design.netlistStorage().forEachRegularNet([&](const auto net_id, const auto&) {
      if (netIsSkipping(design, entities, net_id, skip_cache, true)) {
        return;
      }
      valid_nets.push_back(net_id);
    });
    // Keep ECCDB adapter output deterministic. The differential fixture
    // applies the same ordering to its iDB source before invoking iRT.
    std::sort(valid_nets.begin(), valid_nets.end(), [&entities](const auto lhs, const auto rhs) {
      const auto& lhs_net = entities.nets.get(lhs.entity());
      const auto& rhs_net = entities.nets.get(rhs.entity());
      if (lhs_net.name != rhs_net.name) {
        return lhs_net.name < rhs_net.name;
      }
      return lhs.packed() < rhs.packed();
    });

    net_list.resize(valid_nets.size());
#pragma omp parallel
    {
      std::vector<LayeredRect> routing_scratch;
      std::vector<LayeredRect> cut_scratch;
      std::vector<LayerBox> via_boxes_scratch;
#pragma omp for schedule(guided, 256)
      for (size_t index = 0; index < valid_nets.size(); ++index) {
        const auto net_id = valid_nets[index];
        const auto& src = entities.nets.get(net_id.entity());
        Net& net = net_list[index];
        net.set_net_name(src.name);
        net.set_connect_type(connectTypeFromUse(src.use));

        auto& pin_list = net.get_pin_list();
        const auto instance_pins = net_instance_pins_for(net_id);
        const auto io_pins = net_io_pins_for(net_id);
        for (const auto pin_id : instance_pins) {
          const auto& instance_pin = entities.instance_pins.get(pin_id.entity());
          const auto& term = entities.master_terms.get(instance_pin.master_term.entity());
          if (term.ports.empty()) {
            continue;
          }
          const auto& instance = entities.instances.get(instance_pin.instance.entity());
          const auto& master = entities.masters.get(instance.master.entity());
          Pin pin;
          pin.set_pin_name(RTUTIL.getString(instance.name, ":", term.name));
          pin.set_inst_name(instance.name);
          pin.set_cell_master_name(master.name);
          pin.set_orient(irtOrientation(instance.orientation));
          pin.set_inst_origin(PlanarCoord{instance.origin.x, instance.origin.y});
          pin.set_local_pin_name(term.name);
          pin.set_is_core(isCoreMaster(master.type));
          pin.set_is_macro(master.type == eccdb::LibraryCellMasterType::kBlock
                           || master.type == eccdb::LibraryCellMasterType::kBlockBlackbox
                           || master.type == eccdb::LibraryCellMasterType::kBlockSoft);
          pin.set_is_pad(isPadMaster(master.type));
          const auto& term_geometry = term_geometry_cache.at(instance_pin.master_term, instance.orientation);
          routing_scratch.clear();
          cut_scratch.clear();
          routing_scratch.reserve(term_geometry.routing.size());
          cut_scratch.reserve(term_geometry.cut.size());
          for (const auto& shape : term_geometry.routing) {
            LayeredRect placed = shape;
            placed.rect = placed.rect.offset(instance.origin.x, instance.origin.y);
            routing_scratch.push_back(placed);
          }
          for (const auto& shape : term_geometry.cut) {
            LayeredRect placed = shape;
            placed.rect = placed.rect.offset(instance.origin.x, instance.origin.y);
            cut_scratch.push_back(placed);
          }
          appendPinShapes(pin, routing_scratch, cut_scratch, layers);
          for (const auto& via : term_geometry.vias) {
            appendOffsetViaToPin(pin, via, instance.origin, layers, via_boxes_scratch);
          }
          if (pin.get_is_macro() || pin.get_is_pad()) {
            const auto body = placeMasterRect(eccdb::Rect{.ll_x = 0,
                                                                  .ll_y = 0,
                                                                  .ur_x = static_cast<int32_t>(master.width),
                                                                  .ur_y = static_cast<int32_t>(master.height)},
                                              instance.origin, instance.orientation, static_cast<int32_t>(master.width),
                                              static_cast<int32_t>(master.height));
            pin.set_inst_bbox(PlanarRect{body.ll_x, body.ll_y, body.ur_x, body.ur_y});
            const auto edge = macroPinEdge(pin);
            pin.set_macro_pin_edge(edge);
            if (edge == MacroPinEdge::kNorth || edge == MacroPinEdge::kSouth) {
              pin.set_preferred_escape_direction(Direction::kVertical);
            } else if (edge == MacroPinEdge::kEast || edge == MacroPinEdge::kWest) {
              pin.set_preferred_escape_direction(Direction::kHorizontal);
            }
            pin.set_preferred_conn_layer_idx(preferredConnectionLayer(pin, routing_layer_list));
          }
          pin_list.push_back(std::move(pin));
        }
        for (const auto pin_id : io_pins) {
          const auto& io_pin = entities.io_pins.get(pin_id.entity());
          if (io_pin.ports.empty()) {
            continue;
          }
          Pin pin;
          pin.set_pin_name(io_pin.name);
          pin.set_is_core(false);
          for (const auto& port : io_pin.ports) {
            if (port.placement_status == eccdb::DesignPlacementStatus::kUnplaced
                && (port.flags & eccdb::DesignIoPinPortFlag::kHasPlacement) == 0) {
              continue;
            }
            routing_scratch.clear();
            cut_scratch.clear();
            for (const auto& rect : port.rectangles) {
              const auto* index = layers.find(rect.layer);
              LayeredRect placed{.layer = rect.layer, .rect = placeMasterRect(rect.rectangle, port.origin, port.orientation, 0, 0), .index = index};
              if (index != nullptr && index->cut) {
                cut_scratch.push_back(placed);
              } else {
                routing_scratch.push_back(placed);
              }
            }
            for (const auto& polygon : port.polygons) {
              for (const auto& rect : eccdb::decomposePolygonToRectangles(polygon.points)) {
                const auto* index = layers.find(polygon.layer);
                LayeredRect placed{.layer = polygon.layer, .rect = placeMasterRect(rect, port.origin, port.orientation, 0, 0), .index = index};
                if (index != nullptr && index->cut) {
                  cut_scratch.push_back(placed);
                } else {
                  routing_scratch.push_back(placed);
                }
              }
            }
            appendPinShapes(pin, routing_scratch, cut_scratch, layers);
            for (const auto& via : port.vias) {
              const auto placed = placeMasterPoint(via.origin, port.origin, port.orientation, 0, 0);
              appendViaToPin(pin, resolveViaShapes(design, tech, via.tech_via, via.design_via, placed), layers, via_boxes_scratch);
            }
          }
          pin_list.push_back(std::move(pin));
        }

        bool has_shape = false;
        BoundingBox bbox;
        for (auto& pin : pin_list) {
          auto extend = [&](const auto& shape) {
            if (!has_shape) {
              bbox.set_real_rect(shape.get_real_rect());
              has_shape = true;
              return;
            }
            bbox.set_real_ll_x(std::min(bbox.get_real_ll_x(), shape.get_real_ll_x()));
            bbox.set_real_ll_y(std::min(bbox.get_real_ll_y(), shape.get_real_ll_y()));
            bbox.set_real_ur_x(std::max(bbox.get_real_ur_x(), shape.get_real_ur_x()));
            bbox.set_real_ur_y(std::max(bbox.get_real_ur_y(), shape.get_real_ur_y()));
          };
          for (const auto& shape : pin.get_routing_shape_list()) {
            extend(shape);
          }
          for (const auto& shape : pin.get_cut_shape_list()) {
            extend(shape);
          }
        }
        if (has_shape) {
          net.set_bounding_box(bbox);
        }

        std::string driven_name;
        bool found_driven = false;
        for (const auto pin_id : instance_pins) {
          const auto& instance_pin = entities.instance_pins.get(pin_id.entity());
          const auto& term = entities.master_terms.get(instance_pin.master_term.entity());
          if (term.direction == eccdb::LibraryMasterTermDirection::kOutput
              || term.direction == eccdb::LibraryMasterTermDirection::kOutputTriState) {
            driven_name = RTUTIL.getString(entities.instances.get(instance_pin.instance.entity()).name, ":", term.name);
            found_driven = true;
            break;
          }
        }
        if (!found_driven) {
          for (const auto pin_id : io_pins) {
            const auto& io_pin = entities.io_pins.get(pin_id.entity());
            if (io_pin.direction == eccdb::DesignIoPinDirection::kInput) {
              driven_name = io_pin.name;
              found_driven = true;
              break;
            }
          }
        }
        if (!found_driven) {
          for (const auto pin_id : io_pins) {
            const auto& io_pin = entities.io_pins.get(pin_id.entity());
            if (io_pin.direction == eccdb::DesignIoPinDirection::kInOut) {
              driven_name = io_pin.name;
              found_driven = true;
              break;
            }
          }
        }
        if (!found_driven) {
          if (!io_pins.empty()) {
            driven_name = entities.io_pins.get(io_pins.front().entity()).name;
            found_driven = true;
          } else if (!instance_pins.empty()) {
            const auto& instance_pin = entities.instance_pins.get(instance_pins.front().entity());
            driven_name = RTUTIL.getString(entities.instances.get(instance_pin.instance.entity()).name, ":",
                                           entities.master_terms.get(instance_pin.master_term.entity()).name);
            found_driven = true;
          }
        }
        if (found_driven) {
          for (Pin& pin : net.get_pin_list()) {
            if (pin.get_pin_name() == driven_name) {
              pin.set_is_driven(true);
            }
          }
        }
      }
    }
  }
}

namespace {

eccdb::TechRoutingLayerId requireEnttRoutingLayer(const eccdb::TechStore& tech,
                                                          std::vector<RoutingLayer>& routing_layers, int32_t layer_idx)
{
  if (layer_idx < 0 || layer_idx >= static_cast<int32_t>(routing_layers.size())) {
    throw std::out_of_range("iRT output routing layer index is out of range: " + std::to_string(layer_idx));
  }
  const auto layer = tech.findLayer(routing_layers[static_cast<std::size_t>(layer_idx)].get_layer_name());
  const eccdb::TechRoutingLayerId routing_layer{layer.entity()};
  if (!layer || !tech.routingLayerStorage().contains(routing_layer)) {
    throw std::runtime_error("iRT output cannot resolve routing layer: "
                             + routing_layers[static_cast<std::size_t>(layer_idx)].get_layer_name());
  }
  return routing_layer;
}

auto canonicalSegmentKey(const Segment<LayerCoord>* segment)
{
  auto first = std::tuple{segment->get_first().get_layer_idx(), segment->get_first().get_x(), segment->get_first().get_y()};
  auto second = std::tuple{segment->get_second().get_layer_idx(), segment->get_second().get_x(), segment->get_second().get_y()};
  if (second < first) {
    std::swap(first, second);
  }
  return std::tuple{std::get<0>(first), std::get<1>(first), std::get<2>(first), std::get<0>(second), std::get<1>(second),
                    std::get<2>(second)};
}

auto canonicalPatchKey(const EXTLayerRect* patch)
{
  return std::tuple{patch->get_layer_idx(), patch->get_real_ll_x(), patch->get_real_ll_y(), patch->get_real_ur_x(),
                    patch->get_real_ur_y()};
}

}  // namespace

void RTInterface::outputTrackGridToEnTT()
{
  auto& floorplan = _design->floorplanStorage();
  const auto old_grids = floorplan.trackGrids();
  auto& routing_layers = RTDM.getDatabase().get_routing_layer_list();
  std::vector<eccdb::DesignTrackGrid> replacements;
  for (int32_t index = static_cast<int32_t>(routing_layers.size()) - 1; index >= 0; --index) {
    const auto layer = requireEnttRoutingLayer(*_tech, routing_layers, index);
    auto& axis = routing_layers[static_cast<std::size_t>(index)].get_track_axis();
    for (const ScaleGrid& grid : axis.get_x_grid_list()) {
      replacements.push_back(eccdb::DesignTrackGrid{.axis = eccdb::DesignAxis::kX,
                                                            .start = grid.get_start_line(),
                                                            .track_count = static_cast<uint32_t>(grid.get_step_num() + 1),
                                                            .step = grid.get_step_length(),
                                                            .layers = {layer}});
    }
    for (const ScaleGrid& grid : axis.get_y_grid_list()) {
      replacements.push_back(eccdb::DesignTrackGrid{.axis = eccdb::DesignAxis::kY,
                                                            .start = grid.get_start_line(),
                                                            .track_count = static_cast<uint32_t>(grid.get_step_num() + 1),
                                                            .step = grid.get_step_length(),
                                                            .layers = {layer}});
    }
  }

  std::vector<eccdb::DesignTrackGridId> created;
  try {
    created.reserve(replacements.size());
    for (auto& grid : replacements) {
      created.push_back(floorplan.createTrackGrid(std::move(grid)));
    }
  } catch (...) {
    for (const auto id : created) {
      static_cast<void>(floorplan.destroyTrackGrid(id));
    }
    throw;
  }
  for (const auto id : old_grids) {
    static_cast<void>(floorplan.destroyTrackGrid(id));
  }
}

void RTInterface::outputGCellGridToEnTT()
{
  auto& floorplan = _design->floorplanStorage();
  const auto old_grids = floorplan.gcellGrids();
  auto& axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<eccdb::DesignGCellGrid> replacements;
  for (const ScaleGrid& grid : axis.get_x_grid_list()) {
    replacements.push_back(eccdb::DesignGCellGrid{.axis = eccdb::DesignAxis::kX,
                                                          .start = grid.get_start_line(),
                                                          .line_count = static_cast<uint32_t>(grid.get_step_num() + 1),
                                                          .step = grid.get_step_length()});
  }
  for (const ScaleGrid& grid : axis.get_y_grid_list()) {
    replacements.push_back(eccdb::DesignGCellGrid{.axis = eccdb::DesignAxis::kY,
                                                          .start = grid.get_start_line(),
                                                          .line_count = static_cast<uint32_t>(grid.get_step_num() + 1),
                                                          .step = grid.get_step_length()});
  }

  std::vector<eccdb::DesignGCellGridId> created;
  try {
    created.reserve(replacements.size());
    for (auto& grid : replacements) {
      created.push_back(floorplan.createGCellGrid(std::move(grid)));
    }
  } catch (...) {
    for (const auto id : created) {
      static_cast<void>(floorplan.destroyGCellGrid(id));
    }
    throw;
  }
  for (const auto id : old_grids) {
    static_cast<void>(floorplan.destroyGCellGrid(id));
  }
}

void RTInterface::outputNetListToEnTT()
{
  struct PendingWire
  {
    eccdb::DesignNetId net;
    eccdb::DesignWireRoutingInput routing;
  };

  auto& routing = _design->routingStorage();
  auto& netlist = _design->netlistStorage();
  auto& die = RTDM.getDatabase().get_die();
  auto& rt_nets = RTDM.getDatabase().get_net_list();
  auto& routing_layers = RTDM.getDatabase().get_routing_layer_list();
  const auto result_map = RTDM.getNetDetailedResultMap(die);
  const auto patch_map = RTDM.getNetDetailedPatchMap(die);

  std::vector<int32_t> net_order(rt_nets.size());
  for (int32_t index = 0; index < static_cast<int32_t>(rt_nets.size()); ++index) {
    net_order[static_cast<std::size_t>(index)] = index;
  }
  std::sort(net_order.begin(), net_order.end(), [&](int32_t lhs, int32_t rhs) {
    return rt_nets[static_cast<std::size_t>(lhs)].get_net_name() < rt_nets[static_cast<std::size_t>(rhs)].get_net_name();
  });

  std::vector<PendingWire> pending;
  pending.reserve(net_order.size());
  for (const int32_t net_idx : net_order) {
    const std::string& net_name = rt_nets[static_cast<std::size_t>(net_idx)].get_net_name();
    const auto net = netlist.findRegularNet(net_name);
    if (!net) {
      RTLOG.info(Loc::current(), "The EnTT regular net named ", net_name, " cannot be found!");
      continue;
    }

    eccdb::DesignWireRoutingInput input;
    if (const auto found = result_map.find(net_idx); found != result_map.end()) {
      std::vector<Segment<LayerCoord>*> segments(found->second.begin(), found->second.end());
      std::sort(segments.begin(), segments.end(), [](const auto* lhs, const auto* rhs) {
        return canonicalSegmentKey(lhs) < canonicalSegmentKey(rhs);
      });
      for (const auto* segment : segments) {
        const LayerCoord& first = segment->get_first();
        const LayerCoord& second = segment->get_second();
        eccdb::DesignWirePath path;
        if (first.get_layer_idx() == second.get_layer_idx()) {
          path.layer = requireEnttRoutingLayer(*_tech, routing_layers, first.get_layer_idx());
          path.points = {{{first.get_x(), first.get_y()}}, {{second.get_x(), second.get_y()}}};
        } else {
          const int32_t below_layer = std::min(first.get_layer_idx(), second.get_layer_idx());
          const int32_t top_layer = std::max(first.get_layer_idx(), second.get_layer_idx());
          const auto& layer_vias = RTDM.getDatabase().get_layer_via_master_list();
          if (below_layer < 0 || below_layer >= static_cast<int32_t>(layer_vias.size())
              || layer_vias[static_cast<std::size_t>(below_layer)].empty()) {
            throw std::runtime_error("iRT output cannot resolve a via below routing layer " + std::to_string(below_layer));
          }
          const std::string& via_name = layer_vias[static_cast<std::size_t>(below_layer)].front().get_via_name();
          const auto tech_via = _tech->viaMasterStorage().findViaMaster(via_name);
          const auto design_via = routing.findVia(via_name);
          if (static_cast<bool>(tech_via) == static_cast<bool>(design_via)) {
            throw std::runtime_error("iRT output VIA must resolve exactly once in EnTTDB: " + via_name);
          }
          path.layer = requireEnttRoutingLayer(*_tech, routing_layers, top_layer);
          path.points = {{{first.get_x(), first.get_y()}}};
          path.vias.push_back(eccdb::DesignWireVia{.point_index = 0, .tech_via = tech_via, .design_via = design_via});
        }
        input.appendPath(std::move(path));
      }
    }
    if (const auto found = patch_map.find(net_idx); found != patch_map.end()) {
      std::vector<EXTLayerRect*> patches(found->second.begin(), found->second.end());
      std::sort(patches.begin(), patches.end(), [](const auto* lhs, const auto* rhs) {
        return canonicalPatchKey(lhs) < canonicalPatchKey(rhs);
      });
      for (const auto* patch : patches) {
        eccdb::DesignWirePath path;
        path.layer = requireEnttRoutingLayer(*_tech, routing_layers, patch->get_layer_idx());
        path.points = {{{patch->get_real_ll_x(), patch->get_real_ll_y()}}};
        path.rectangles.push_back(eccdb::DesignWireRectangle{
            .point_index = 0,
            .delta = {.ll_x = 0,
                      .ll_y = 0,
                      .ur_x = patch->get_real_ur_x() - patch->get_real_ll_x(),
                      .ur_y = patch->get_real_ur_y() - patch->get_real_ll_y()}});
        input.appendPath(std::move(path));
      }
    }
    if (input.pathCount() != 0u) {
      pending.push_back(PendingWire{.net = net, .routing = std::move(input)});
    }
  }

  std::vector<eccdb::DesignWireId> old_wires;
  netlist.forEachRegularNet([&](const auto net, const auto&) {
    routing.forEachWire(net, [&](const auto wire, const auto&) { old_wires.push_back(wire); });
  });

  std::vector<eccdb::DesignWireId> created;
  try {
    created.reserve(pending.size());
    for (auto& wire : pending) {
      created.push_back(routing.createWire(eccdb::DesignWire{.net = wire.net, .status = eccdb::DesignWireStatus::kRouted},
                                           std::move(wire.routing)));
    }
  } catch (...) {
    for (const auto id : created) {
      static_cast<void>(routing.destroyWire(id));
    }
    throw;
  }
  for (const auto id : old_wires) {
    static_cast<void>(routing.destroyWire(id));
  }
}

namespace {

std::string layerName(std::vector<RoutingLayer>& layers, int32_t idx)
{
  if (idx < 0 || idx >= static_cast<int32_t>(layers.size())) {
    return "<invalid_routing:" + std::to_string(idx) + ">";
  }
  return layers[static_cast<size_t>(idx)].get_layer_name();
}

std::string cutLayerName(std::vector<CutLayer>& layers, int32_t idx)
{
  if (idx < 0 || idx >= static_cast<int32_t>(layers.size())) {
    return "<invalid_cut:" + std::to_string(idx) + ">";
  }
  return layers[static_cast<size_t>(idx)].get_layer_name();
}

bool equalGridMap(GridMap<int32_t>& left, GridMap<int32_t>& right)
{
  if (left.get_x_size() != right.get_x_size() || left.get_y_size() != right.get_y_size()) {
    return false;
  }
  for (int32_t x = 0; x < left.get_x_size(); ++x) {
    for (int32_t y = 0; y < left.get_y_size(); ++y) {
      if (left[x][y] != right[x][y]) {
        return false;
      }
    }
  }
  return true;
}

bool equalTrackAxis(ScaleAxis& left, ScaleAxis& right)
{
  auto equal_grids = [](const std::vector<ScaleGrid>& lhs, const std::vector<ScaleGrid>& rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
      if (lhs[i].get_start_line() != rhs[i].get_start_line() || lhs[i].get_step_length() != rhs[i].get_step_length()) {
        return false;
      }
    }
    return true;
  };
  return equal_grids(left.get_x_grid_list(), right.get_x_grid_list()) && equal_grids(left.get_y_grid_list(), right.get_y_grid_list());
}

bool equalSpacingTable(SpacingTable& left, SpacingTable& right)
{
  return left.get_width_list() == right.get_width_list() && left.get_parallel_length_list() == right.get_parallel_length_list()
         && equalGridMap(left.get_width_parallel_length_map(), right.get_width_parallel_length_map());
}

int32_t mappedLayerIdx(const std::map<int32_t, int32_t>& id_to_idx, int32_t layer_idx)
{
  const auto it = id_to_idx.find(layer_idx);
  return it == id_to_idx.end() ? -1 : it->second;
}

template <typename Rect>
void sortRectsByMappedLayer(std::vector<Rect>& rects, const std::map<int32_t, int32_t>& id_to_idx)
{
  std::sort(rects.begin(), rects.end(), [&](Rect& lhs, Rect& rhs) {
    return std::make_tuple(mappedLayerIdx(id_to_idx, lhs.get_layer_idx()), lhs.get_real_ll_x(), lhs.get_real_ll_y(), lhs.get_real_ur_x(),
                           lhs.get_real_ur_y())
           < std::make_tuple(mappedLayerIdx(id_to_idx, rhs.get_layer_idx()), rhs.get_real_ll_x(), rhs.get_real_ll_y(), rhs.get_real_ur_x(),
                             rhs.get_real_ur_y());
  });
}

template <typename Rect>
std::string firstRectMismatch(const std::string& tag, std::vector<Rect>& left, std::vector<Rect>& right,
                              const std::map<int32_t, int32_t>& left_id_to_idx, const std::map<int32_t, int32_t>& right_id_to_idx,
                              const std::function<std::string(int32_t)>& left_name, const std::function<std::string(int32_t)>& right_name)
{
  sortRectsByMappedLayer(left, left_id_to_idx);
  sortRectsByMappedLayer(right, right_id_to_idx);
  if (left.size() != right.size()) {
    std::ostringstream out;
    out << tag << " " << left.size() << " vs " << right.size();
    const size_t n = std::min(left.size(), right.size());
    size_t first = 0;
    for (; first < n; ++first) {
      const int32_t left_idx = mappedLayerIdx(left_id_to_idx, left[first].get_layer_idx());
      const int32_t right_idx = mappedLayerIdx(right_id_to_idx, right[first].get_layer_idx());
      if (left_idx != right_idx || left[first].get_real_ll_x() != right[first].get_real_ll_x()
          || left[first].get_real_ll_y() != right[first].get_real_ll_y() || left[first].get_real_ur_x() != right[first].get_real_ur_x()
          || left[first].get_real_ur_y() != right[first].get_real_ur_y()) {
        break;
      }
    }
    out << " first_diff=" << first;
    if (first < left.size()) {
      out << " left[" << first << "]=" << left_name(left[first].get_layer_idx()) << ' ' << left[first].get_real_ll_x() << ' '
          << left[first].get_real_ll_y() << ' ' << left[first].get_real_ur_x() << ' ' << left[first].get_real_ur_y();
    }
    if (first < right.size()) {
      out << " right[" << first << "]=" << right_name(right[first].get_layer_idx()) << ' ' << right[first].get_real_ll_x() << ' '
          << right[first].get_real_ll_y() << ' ' << right[first].get_real_ur_x() << ' ' << right[first].get_real_ur_y();
    }
    return out.str();
  }
  for (size_t i = 0; i < left.size(); ++i) {
    const int32_t left_idx = mappedLayerIdx(left_id_to_idx, left[i].get_layer_idx());
    const int32_t right_idx = mappedLayerIdx(right_id_to_idx, right[i].get_layer_idx());
    if (left_idx != right_idx || left[i].get_real_ll_x() != right[i].get_real_ll_x() || left[i].get_real_ll_y() != right[i].get_real_ll_y()
        || left[i].get_real_ur_x() != right[i].get_real_ur_x() || left[i].get_real_ur_y() != right[i].get_real_ur_y()) {
      auto left_key = [&](size_t index) {
        return std::make_tuple(mappedLayerIdx(left_id_to_idx, left[index].get_layer_idx()), left[index].get_real_ll_x(),
                               left[index].get_real_ll_y(), left[index].get_real_ur_x(), left[index].get_real_ur_y());
      };
      auto right_key = [&](size_t index) {
        return std::make_tuple(mappedLayerIdx(right_id_to_idx, right[index].get_layer_idx()), right[index].get_real_ll_x(),
                               right[index].get_real_ll_y(), right[index].get_real_ur_x(), right[index].get_real_ur_y());
      };
      size_t left_cursor = i;
      size_t right_cursor = i;
      size_t left_only = left.size();
      size_t right_only = right.size();
      while (left_cursor < left.size() && right_cursor < right.size()
             && (left_only == left.size() || right_only == right.size())) {
        const auto lhs = left_key(left_cursor);
        const auto rhs = right_key(right_cursor);
        if (lhs == rhs) {
          ++left_cursor;
          ++right_cursor;
        } else if (lhs < rhs) {
          if (left_only == left.size()) {
            left_only = left_cursor;
          }
          ++left_cursor;
        } else {
          if (right_only == right.size()) {
            right_only = right_cursor;
          }
          ++right_cursor;
        }
      }
      if (left_only == left.size() && left_cursor < left.size()) {
        left_only = left_cursor;
      }
      if (right_only == right.size() && right_cursor < right.size()) {
        right_only = right_cursor;
      }
      std::ostringstream out;
      out << tag << "[" << i << "] " << left_name(left[i].get_layer_idx()) << ' ' << left[i].get_real_ll_x() << ' '
          << left[i].get_real_ll_y() << ' ' << left[i].get_real_ur_x() << ' ' << left[i].get_real_ur_y() << " vs "
          << right_name(right[i].get_layer_idx()) << ' ' << right[i].get_real_ll_x() << ' ' << right[i].get_real_ll_y() << ' '
          << right[i].get_real_ur_x() << ' ' << right[i].get_real_ur_y();
      if (left_only < left.size()) {
        out << " left_only[" << left_only << "]=" << left_name(left[left_only].get_layer_idx()) << ' '
            << left[left_only].get_real_ll_x() << ' ' << left[left_only].get_real_ll_y() << ' ' << left[left_only].get_real_ur_x() << ' '
            << left[left_only].get_real_ur_y();
      }
      if (right_only < right.size()) {
        out << " right_only[" << right_only << "]=" << right_name(right[right_only].get_layer_idx()) << ' '
            << right[right_only].get_real_ll_x() << ' ' << right[right_only].get_real_ll_y() << ' ' << right[right_only].get_real_ur_x()
            << ' ' << right[right_only].get_real_ur_y();
      }
      return out.str();
    }
  }
  return {};
}

}  // namespace

std::string RTInterface::compareWrappedDatabase(Database& left, Database& right)
{
  auto fail = [](const std::string& message) { return message; };

  if (left.get_design_name() != right.get_design_name()) {
    return fail("design " + left.get_design_name() + " vs " + right.get_design_name());
  }
  if (left.get_micron_dbu() != right.get_micron_dbu()) {
    return fail("micron_dbu " + std::to_string(left.get_micron_dbu()) + " vs " + std::to_string(right.get_micron_dbu()));
  }
  if (left.get_manufacture_grid() != right.get_manufacture_grid()) {
    return fail("manufacture_grid " + std::to_string(left.get_manufacture_grid()) + " vs " + std::to_string(right.get_manufacture_grid()));
  }

  Die& left_die = left.get_die();
  Die& right_die = right.get_die();
  if (left_die.get_real_ll_x() != right_die.get_real_ll_x() || left_die.get_real_ll_y() != right_die.get_real_ll_y()
      || left_die.get_real_ur_x() != right_die.get_real_ur_x() || left_die.get_real_ur_y() != right_die.get_real_ur_y()) {
    std::ostringstream out;
    out << "die " << left_die.get_real_ll_x() << ' ' << left_die.get_real_ll_y() << ' ' << left_die.get_real_ur_x() << ' '
        << left_die.get_real_ur_y() << " vs " << right_die.get_real_ll_x() << ' ' << right_die.get_real_ll_y() << ' '
        << right_die.get_real_ur_x() << ' ' << right_die.get_real_ur_y();
    return fail(out.str());
  }

  auto& left_macros = left.get_macro_list();
  auto& right_macros = right.get_macro_list();
  std::sort(left_macros.begin(), left_macros.end(), [](Macro& lhs, Macro& rhs) { return lhs.get_inst_name() < rhs.get_inst_name(); });
  std::sort(right_macros.begin(), right_macros.end(), [](Macro& lhs, Macro& rhs) { return lhs.get_inst_name() < rhs.get_inst_name(); });
  if (left_macros.size() != right_macros.size()) {
    return fail("macro_count " + std::to_string(left_macros.size()) + " vs " + std::to_string(right_macros.size()));
  }
  for (size_t i = 0; i < left_macros.size(); ++i) {
    Macro& lhs = left_macros[i];
    Macro& rhs = right_macros[i];
    if (lhs.get_inst_name() != rhs.get_inst_name() || lhs.get_body_rect() != rhs.get_body_rect()) {
      return fail("macro[" + std::to_string(i) + "] " + lhs.get_inst_name() + " fields differ");
    }
  }

  Row& left_row = left.get_row();
  Row& right_row = right.get_row();
  if (left_row.get_start_x() != right_row.get_start_x() || left_row.get_start_y() != right_row.get_start_y()
      || left_row.get_height() != right_row.get_height()) {
    std::ostringstream out;
    out << "row " << left_row.get_start_x() << ' ' << left_row.get_start_y() << ' ' << left_row.get_height() << " vs "
        << right_row.get_start_x() << ' ' << right_row.get_start_y() << ' ' << right_row.get_height();
    return fail(out.str());
  }
  if (!equalTrackAxis(left.get_gcell_axis(), right.get_gcell_axis())) {
    return fail("gcell axis differs");
  }

  std::vector<RoutingLayer>& left_routing = left.get_routing_layer_list();
  std::vector<RoutingLayer>& right_routing = right.get_routing_layer_list();
  if (left_routing.size() != right_routing.size()) {
    return fail("routing_layer_count " + std::to_string(left_routing.size()) + " vs " + std::to_string(right_routing.size()));
  }
  for (size_t i = 0; i < left_routing.size(); ++i) {
    RoutingLayer& lhs = left_routing[i];
    RoutingLayer& rhs = right_routing[i];
    if (lhs.get_layer_name() != rhs.get_layer_name()) {
      return fail("routing_layer[" + std::to_string(i) + "] " + lhs.get_layer_name() + " vs " + rhs.get_layer_name());
    }
    if (lhs.get_prefer_direction() != rhs.get_prefer_direction() || lhs.get_min_width() != rhs.get_min_width()
        || lhs.get_min_area() != rhs.get_min_area() || lhs.get_notch_spacing() != rhs.get_notch_spacing()
        || lhs.get_eol_spacing() != rhs.get_eol_spacing() || lhs.get_eol_ete() != rhs.get_eol_ete()
        || lhs.get_eol_within() != rhs.get_eol_within()) {
      return fail("routing_layer " + lhs.get_layer_name() + " fields differ");
    }
    if (!equalTrackAxis(lhs.get_track_axis(), rhs.get_track_axis())) {
      return fail("routing_layer " + lhs.get_layer_name() + " track axis differs");
    }
    if (!equalSpacingTable(lhs.get_prl_spacing_table(), rhs.get_prl_spacing_table())) {
      return fail("routing_layer " + lhs.get_layer_name() + " prl table differs");
    }
  }

  std::vector<CutLayer>& left_cuts = left.get_cut_layer_list();
  std::vector<CutLayer>& right_cuts = right.get_cut_layer_list();
  if (left_cuts.size() != right_cuts.size()) {
    return fail("cut_layer_count " + std::to_string(left_cuts.size()) + " vs " + std::to_string(right_cuts.size()));
  }
  for (size_t i = 0; i < left_cuts.size(); ++i) {
    CutLayer& lhs = left_cuts[i];
    CutLayer& rhs = right_cuts[i];
    if (lhs.get_layer_name() != rhs.get_layer_name()) {
      return fail("cut_layer[" + std::to_string(i) + "] " + lhs.get_layer_name() + " vs " + rhs.get_layer_name());
    }
    if (lhs.get_curr_spacing() != rhs.get_curr_spacing() || lhs.get_curr_prl() != rhs.get_curr_prl()
        || lhs.get_curr_prl_spacing() != rhs.get_curr_prl_spacing() || lhs.get_curr_eol_spacing() != rhs.get_curr_eol_spacing()
        || lhs.get_curr_eol_prl() != rhs.get_curr_eol_prl() || lhs.get_curr_eol_prl_spacing() != rhs.get_curr_eol_prl_spacing()
        || lhs.get_above_spacing() != rhs.get_above_spacing() || lhs.get_above_prl() != rhs.get_above_prl()
        || lhs.get_above_prl_spacing() != rhs.get_above_prl_spacing() || lhs.get_below_spacing() != rhs.get_below_spacing()
        || lhs.get_below_prl() != rhs.get_below_prl() || lhs.get_below_prl_spacing() != rhs.get_below_prl_spacing()) {
      return fail("cut_layer " + lhs.get_layer_name() + " fields differ");
    }
  }

  std::vector<ViaMaster> left_vias;
  std::vector<ViaMaster> right_vias;
  for (std::vector<ViaMaster>& layer_vias : left.get_layer_via_master_list()) {
    left_vias.insert(left_vias.end(), layer_vias.begin(), layer_vias.end());
  }
  for (std::vector<ViaMaster>& layer_vias : right.get_layer_via_master_list()) {
    right_vias.insert(right_vias.end(), layer_vias.begin(), layer_vias.end());
  }
  std::sort(left_vias.begin(), left_vias.end(), [](ViaMaster& a, ViaMaster& b) { return a.get_via_name() < b.get_via_name(); });
  std::sort(right_vias.begin(), right_vias.end(), [](ViaMaster& a, ViaMaster& b) { return a.get_via_name() < b.get_via_name(); });
  if (left_vias.size() != right_vias.size()) {
    return fail("via_master_count " + std::to_string(left_vias.size()) + " vs " + std::to_string(right_vias.size()));
  }
  for (size_t i = 0; i < left_vias.size(); ++i) {
    ViaMaster& lhs = left_vias[i];
    ViaMaster& rhs = right_vias[i];
    if (lhs.get_via_name() != rhs.get_via_name()) {
      return fail("via[" + std::to_string(i) + "] " + lhs.get_via_name() + " vs " + rhs.get_via_name());
    }
    LayerRect& left_above = lhs.get_above_enclosure();
    LayerRect& right_above = rhs.get_above_enclosure();
    LayerRect& left_below = lhs.get_below_enclosure();
    LayerRect& right_below = rhs.get_below_enclosure();
    if (layerName(left_routing, left_above.get_layer_idx()) != layerName(right_routing, right_above.get_layer_idx())
        || left_above.get_ll_x() != right_above.get_ll_x() || left_above.get_ll_y() != right_above.get_ll_y()
        || left_above.get_ur_x() != right_above.get_ur_x() || left_above.get_ur_y() != right_above.get_ur_y()
        || layerName(left_routing, left_below.get_layer_idx()) != layerName(right_routing, right_below.get_layer_idx())
        || left_below.get_ll_x() != right_below.get_ll_x() || left_below.get_ll_y() != right_below.get_ll_y()
        || left_below.get_ur_x() != right_below.get_ur_x() || left_below.get_ur_y() != right_below.get_ur_y()
        || cutLayerName(left_cuts, lhs.get_cut_layer_idx()) != cutLayerName(right_cuts, rhs.get_cut_layer_idx())
        || lhs.get_above_direction() != rhs.get_above_direction() || lhs.get_below_direction() != rhs.get_below_direction()
        || lhs.get_cut_shape_list() != rhs.get_cut_shape_list()) {
      return fail("via " + lhs.get_via_name() + " fields differ");
    }
  }

  if (const auto mismatch = firstRectMismatch(
          "routing_obstacles", left.get_routing_obstacle_list(), right.get_routing_obstacle_list(),
          left.get_routing_idb_layer_id_to_idx_map(), right.get_routing_idb_layer_id_to_idx_map(),
          [&](int32_t idx) { return layerName(left_routing, mappedLayerIdx(left.get_routing_idb_layer_id_to_idx_map(), idx)); },
          [&](int32_t idx) { return layerName(right_routing, mappedLayerIdx(right.get_routing_idb_layer_id_to_idx_map(), idx)); });
      !mismatch.empty()) {
    return fail(mismatch);
  }
  if (const auto mismatch = firstRectMismatch(
          "cut_obstacles", left.get_cut_obstacle_list(), right.get_cut_obstacle_list(), left.get_cut_idb_layer_id_to_idx_map(),
          right.get_cut_idb_layer_id_to_idx_map(),
          [&](int32_t idx) { return cutLayerName(left_cuts, mappedLayerIdx(left.get_cut_idb_layer_id_to_idx_map(), idx)); },
          [&](int32_t idx) { return cutLayerName(right_cuts, mappedLayerIdx(right.get_cut_idb_layer_id_to_idx_map(), idx)); });
      !mismatch.empty()) {
    return fail(mismatch);
  }

  std::vector<Net>& left_nets = left.get_net_list();
  std::vector<Net>& right_nets = right.get_net_list();
  std::sort(left_nets.begin(), left_nets.end(), [](Net& a, Net& b) { return a.get_net_name() < b.get_net_name(); });
  std::sort(right_nets.begin(), right_nets.end(), [](Net& a, Net& b) { return a.get_net_name() < b.get_net_name(); });
  if (left_nets.size() != right_nets.size()) {
    return fail("net_count " + std::to_string(left_nets.size()) + " vs " + std::to_string(right_nets.size()));
  }
  for (size_t i = 0; i < left_nets.size(); ++i) {
    Net& lhs = left_nets[i];
    Net& rhs = right_nets[i];
    if (lhs.get_net_name() != rhs.get_net_name()) {
      return fail("net[" + std::to_string(i) + "] " + lhs.get_net_name() + " vs " + rhs.get_net_name());
    }
    if (lhs.get_connect_type() != rhs.get_connect_type()) {
      return fail("net " + lhs.get_net_name() + " connect_type differs");
    }
    if (lhs.get_bounding_box().get_real_ll_x() != rhs.get_bounding_box().get_real_ll_x()
        || lhs.get_bounding_box().get_real_ll_y() != rhs.get_bounding_box().get_real_ll_y()
        || lhs.get_bounding_box().get_real_ur_x() != rhs.get_bounding_box().get_real_ur_x()
        || lhs.get_bounding_box().get_real_ur_y() != rhs.get_bounding_box().get_real_ur_y()) {
      return fail("net " + lhs.get_net_name() + " bounding_box differs");
    }
    std::vector<Pin>& left_pins = lhs.get_pin_list();
    std::vector<Pin>& right_pins = rhs.get_pin_list();
    std::sort(left_pins.begin(), left_pins.end(), [](Pin& a, Pin& b) { return a.get_pin_name() < b.get_pin_name(); });
    std::sort(right_pins.begin(), right_pins.end(), [](Pin& a, Pin& b) { return a.get_pin_name() < b.get_pin_name(); });
    if (left_pins.size() != right_pins.size()) {
      return fail("net " + lhs.get_net_name() + " pin_count " + std::to_string(left_pins.size()) + " vs "
                  + std::to_string(right_pins.size()));
    }
    for (size_t pin_idx = 0; pin_idx < left_pins.size(); ++pin_idx) {
      Pin& left_pin = left_pins[pin_idx];
      Pin& right_pin = right_pins[pin_idx];
      if (left_pin.get_pin_name() != right_pin.get_pin_name() || left_pin.get_is_core() != right_pin.get_is_core()
          || left_pin.get_is_driven() != right_pin.get_is_driven() || left_pin.get_inst_name() != right_pin.get_inst_name()
          || left_pin.get_cell_master_name() != right_pin.get_cell_master_name()
          || left_pin.get_orient() != right_pin.get_orient()
          || left_pin.get_inst_origin() != right_pin.get_inst_origin()
          || left_pin.get_local_pin_name() != right_pin.get_local_pin_name() || left_pin.get_is_macro() != right_pin.get_is_macro()
          || left_pin.get_is_pad() != right_pin.get_is_pad() || left_pin.get_inst_bbox() != right_pin.get_inst_bbox()
          || left_pin.get_macro_pin_edge() != right_pin.get_macro_pin_edge()
          || left_pin.get_preferred_escape_direction() != right_pin.get_preferred_escape_direction()
          || layerName(left_routing, left_pin.get_preferred_conn_layer_idx())
                 != layerName(right_routing, right_pin.get_preferred_conn_layer_idx())) {
        return fail("net " + lhs.get_net_name() + " pin " + left_pin.get_pin_name() + " vs " + right_pin.get_pin_name());
      }
      if (const auto mismatch = firstRectMismatch(
              "net " + lhs.get_net_name() + " pin " + left_pin.get_pin_name() + " routing_shapes", left_pin.get_routing_shape_list(),
              right_pin.get_routing_shape_list(), left.get_routing_idb_layer_id_to_idx_map(), right.get_routing_idb_layer_id_to_idx_map(),
              [&](int32_t idx) { return layerName(left_routing, mappedLayerIdx(left.get_routing_idb_layer_id_to_idx_map(), idx)); },
              [&](int32_t idx) { return layerName(right_routing, mappedLayerIdx(right.get_routing_idb_layer_id_to_idx_map(), idx)); });
          !mismatch.empty()) {
        return fail(mismatch);
      }
      if (const auto mismatch = firstRectMismatch(
              "net " + lhs.get_net_name() + " pin " + left_pin.get_pin_name() + " cut_shapes", left_pin.get_cut_shape_list(),
              right_pin.get_cut_shape_list(), left.get_cut_idb_layer_id_to_idx_map(), right.get_cut_idb_layer_id_to_idx_map(),
              [&](int32_t idx) { return cutLayerName(left_cuts, mappedLayerIdx(left.get_cut_idb_layer_id_to_idx_map(), idx)); },
              [&](int32_t idx) { return cutLayerName(right_cuts, mappedLayerIdx(right.get_cut_idb_layer_id_to_idx_map(), idx)); });
          !mismatch.empty()) {
        return fail(mismatch);
      }
    }
  }
  return {};
}

}  // namespace irt
