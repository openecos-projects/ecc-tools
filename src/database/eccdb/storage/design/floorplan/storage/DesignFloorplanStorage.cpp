#include "design/floorplan/storage/DesignFloorplanStorage.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "design/common/DesignGeometryValidation.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "library/LibraryRegistry.h"
#include "library/site/model/SiteComponents.h"
#include "tech/TechRegistry.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {
namespace {

int32_t checkedUpper(int32_t origin, uint32_t count, int32_t step, int32_t object_dimension)
{
  const int64_t repeated_extent = static_cast<int64_t>(count - 1u) * step;
  const int64_t upper = static_cast<int64_t>(origin) + repeated_extent + object_dimension;
  if (upper > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error("design row bounding box exceeds int32 coordinates");
  }
  return static_cast<int32_t>(upper);
}

int32_t checkedCoordinate(int32_t start, uint32_t index, int32_t step, std::string_view object_name)
{
  const int64_t coordinate = static_cast<int64_t>(start) + static_cast<int64_t>(index) * step;
  if (coordinate < std::numeric_limits<int32_t>::min() || coordinate > std::numeric_limits<int32_t>::max()) {
    throw std::overflow_error(std::string(object_name) + " coordinate exceeds int32");
  }
  return static_cast<int32_t>(coordinate);
}

template <typename Id, typename Component>
std::vector<Id> componentIds(const DesignFloorplanStorage::registry_type& registry)
{
  const auto view = registry.view<const Component>();
  std::vector<Id> result;
  for (const auto entity : view) {
    result.emplace_back(entity);
  }
  std::sort(result.begin(), result.end(), [](Id lhs, Id rhs) { return lhs.packed() < rhs.packed(); });
  return result;
}

}  // namespace

DesignRowId DesignFloorplanStorage::createRow(DesignRow row)
{
  row = normalizeRow(std::move(row));
  validateRow(row);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignRow>(entity, std::move(row));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignRowId{entity};
}

void DesignFloorplanStorage::updateRow(DesignRowId id, DesignRow row)
{
  ensureRow(id);
  row = normalizeRow(std::move(row));
  validateRow(row, id);
  _registry.replace<DesignRow>(id.entity(), std::move(row));
}

bool DesignFloorplanStorage::destroyRow(DesignRowId id)
{
  if (!contains(id)) {
    return false;
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignFloorplanStorage::contains(DesignRowId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignRow>(id.entity());
}

DesignRowId DesignFloorplanStorage::findRow(std::string_view name) const
{
  const auto view = _registry.view<const DesignRow>();
  for (const auto entity : view) {
    if (view.get<const DesignRow>(entity).name == name) {
      return DesignRowId{entity};
    }
  }
  return {};
}

const DesignRow& DesignFloorplanStorage::row(DesignRowId id) const
{
  ensureRow(id);
  return _registry.get<const DesignRow>(id.entity());
}

Rect DesignFloorplanStorage::rowBounds(DesignRowId id) const
{
  return rowBounds(row(id));
}

std::vector<DesignRowId> DesignFloorplanStorage::rows() const
{
  return componentIds<DesignRowId, DesignRow>(_registry);
}

DesignTrackGridId DesignFloorplanStorage::createTrackGrid(DesignTrackGrid grid)
{
  validateTrackGrid(grid);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignTrackGrid>(entity, std::move(grid));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignTrackGridId{entity};
}

void DesignFloorplanStorage::updateTrackGrid(DesignTrackGridId id, DesignTrackGrid grid)
{
  ensureTrackGrid(id);
  validateTrackGrid(grid);
  _registry.replace<DesignTrackGrid>(id.entity(), std::move(grid));
}

bool DesignFloorplanStorage::destroyTrackGrid(DesignTrackGridId id)
{
  if (!contains(id)) {
    return false;
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignFloorplanStorage::contains(DesignTrackGridId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignTrackGrid>(id.entity());
}

const DesignTrackGrid& DesignFloorplanStorage::trackGrid(DesignTrackGridId id) const
{
  ensureTrackGrid(id);
  return _registry.get<const DesignTrackGrid>(id.entity());
}

std::vector<DesignTrackGridId> DesignFloorplanStorage::trackGrids() const
{
  return componentIds<DesignTrackGridId, DesignTrackGrid>(_registry);
}

std::vector<DesignTrackGridId> DesignFloorplanStorage::trackGridsForLayer(TechRoutingLayerId layer) const
{
  const auto& tech = _tech_registry.registry();
  if (!layer || !tech.valid(layer.entity()) || !tech.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity())) {
    throw std::invalid_argument("track grid query requires a valid routing layer");
  }

  std::vector<DesignTrackGridId> result;
  const auto view = _registry.view<const DesignTrackGrid>();
  for (const auto entity : view) {
    const auto& layers = view.get<const DesignTrackGrid>(entity).layers;
    if (std::find(layers.begin(), layers.end(), layer) != layers.end()) {
      result.emplace_back(entity);
    }
  }
  std::sort(result.begin(), result.end(), [](DesignTrackGridId lhs, DesignTrackGridId rhs) { return lhs.packed() < rhs.packed(); });
  return result;
}

std::vector<int32_t> DesignFloorplanStorage::trackCoordinates(DesignTrackGridId id) const
{
  const auto& grid = trackGrid(id);
  std::vector<int32_t> coordinates;
  coordinates.reserve(grid.track_count);
  for (uint32_t index = 0; index < grid.track_count; ++index) {
    coordinates.push_back(checkedCoordinate(grid.start, index, grid.step, "design track grid"));
  }
  return coordinates;
}

std::vector<int32_t> DesignFloorplanStorage::trackCoordinates(TechRoutingLayerId layer, DesignAxis axis) const
{
  if (!isValidDesignAxis(axis)) {
    throw std::invalid_argument("track coordinate query requires a valid axis");
  }
  std::vector<int32_t> coordinates;
  for (const auto id : trackGridsForLayer(layer)) {
    const auto& grid = trackGrid(id);
    if (grid.axis != axis) {
      continue;
    }
    auto pattern = trackCoordinates(id);
    coordinates.insert(coordinates.end(), pattern.begin(), pattern.end());
  }
  std::sort(coordinates.begin(), coordinates.end());
  coordinates.erase(std::unique(coordinates.begin(), coordinates.end()), coordinates.end());
  return coordinates;
}

DesignGCellGridId DesignFloorplanStorage::createGCellGrid(DesignGCellGrid grid)
{
  validateGCellGrid(grid);
  const auto entity = _registry.create();
  try {
    _registry.emplace<DesignGCellGrid>(entity, std::move(grid));
  } catch (...) {
    _registry.destroy(entity);
    throw;
  }
  return DesignGCellGridId{entity};
}

void DesignFloorplanStorage::updateGCellGrid(DesignGCellGridId id, DesignGCellGrid grid)
{
  ensureGCellGrid(id);
  validateGCellGrid(grid);
  _registry.replace<DesignGCellGrid>(id.entity(), std::move(grid));
}

bool DesignFloorplanStorage::destroyGCellGrid(DesignGCellGridId id)
{
  if (!contains(id)) {
    return false;
  }
  _registry.destroy(id.entity());
  return true;
}

bool DesignFloorplanStorage::contains(DesignGCellGridId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<DesignGCellGrid>(id.entity());
}

const DesignGCellGrid& DesignFloorplanStorage::gcellGrid(DesignGCellGridId id) const
{
  ensureGCellGrid(id);
  return _registry.get<const DesignGCellGrid>(id.entity());
}

std::vector<DesignGCellGridId> DesignFloorplanStorage::gcellGrids() const
{
  return componentIds<DesignGCellGridId, DesignGCellGrid>(_registry);
}

bool DesignFloorplanStorage::hasCoreArea() const noexcept
{
  return _registry.valid(_root.entity()) && _registry.all_of<DesignRoot, DesignCoreArea>(_root.entity());
}

const DesignCoreArea& DesignFloorplanStorage::coreArea() const
{
  if (!hasCoreArea()) {
    throw std::out_of_range("design core area is absent");
  }
  return _registry.get<const DesignCoreArea>(_root.entity());
}

void DesignFloorplanStorage::setCoreArea(DesignCoreArea area)
{
  if (!_registry.valid(_root.entity()) || !_registry.all_of<DesignRoot>(_root.entity())) {
    throw std::logic_error("invalid design root id");
  }
  static_cast<void>(validateDesignOrthogonalBoundary(area.boundary, "design core area"));
  _registry.emplace_or_replace<DesignCoreArea>(_root.entity(), std::move(area));
}

Rect DesignFloorplanStorage::coreBounds() const
{
  if (hasCoreArea()) {
    return validateDesignOrthogonalBoundary(coreArea().boundary, "design core area");
  }

  const auto view = _registry.view<const DesignRow>();
  const auto& library = _library_registry.registry();
  Rect bounds;
  bool has_row = false;
  for (const auto entity : view) {
    const auto& value = view.get<const DesignRow>(entity);
    const auto row_box = rowBounds(value);
    const auto& site = library.get<const LibrarySite>(value.site.entity());
    if (site.site_class == LibrarySiteClass::kPad) {
      continue;
    }
    bounds = has_row ? bounds.united(row_box) : row_box;
    has_row = true;
  }
  if (has_row) {
    return bounds;
  }

  if (!_registry.valid(_root.entity()) || !_registry.all_of<DesignRoot, DesignDieArea>(_root.entity())) {
    throw std::logic_error("design core bounds require rows or a die area");
  }
  return validateDesignOrthogonalBoundary(_registry.get<const DesignDieArea>(_root.entity()).boundary, "design die area");
}

bool DesignFloorplanStorage::referencesSite(LibrarySiteId id) const
{
  if (!id) {
    return false;
  }
  const auto view = _registry.view<const DesignRow>();
  for (const auto entity : view) {
    if (view.get<const DesignRow>(entity).site == id) {
      return true;
    }
  }
  return false;
}

uint32_t DesignFloorplanStorage::rowCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignRow>().size());
}

uint32_t DesignFloorplanStorage::trackGridCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignTrackGrid>().size());
}

uint32_t DesignFloorplanStorage::gcellGridCount() const
{
  return static_cast<uint32_t>(_registry.storage<DesignGCellGrid>().size());
}

DesignRow DesignFloorplanStorage::normalizeRow(DesignRow row) const
{
  constexpr uint32_t kSyntaxFlags = DesignRowFlag::kHasDo | DesignRowFlag::kHasStep;
  if ((row.flags & kSyntaxFlags) == 0u) {
    if (row.repeat_count_x != 1u || row.repeat_count_y != 1u || row.step_x != 0 || row.step_y != 0) {
      row.flags |= DesignRowFlag::kHasDo;
    }
    if (row.step_x != 0 || row.step_y != 0) {
      row.flags |= DesignRowFlag::kHasStep;
    }
  }

  if ((row.flags & DesignRowFlag::kHasDo) != 0u && (row.flags & DesignRowFlag::kHasStep) == 0u
      && ((row.repeat_count_x > 1u && row.step_x == 0) || (row.repeat_count_y > 1u && row.step_y == 0))) {
    const auto& library = _library_registry.registry();
    if (!row.site || !library.valid(row.site.entity()) || !library.all_of<LibrarySite>(row.site.entity())) {
      throw std::invalid_argument("design row references an invalid library site");
    }
    const auto& site = library.get<const LibrarySite>(row.site.entity());
    const int32_t width = isQuarterTurnOrientation(row.orientation) ? site.height : site.width;
    const int32_t height = isQuarterTurnOrientation(row.orientation) ? site.width : site.height;
    if (row.repeat_count_x > 1u && row.step_x == 0) {
      row.step_x = width;
    }
    if (row.repeat_count_y > 1u && row.step_y == 0) {
      row.step_y = height;
    }
  }
  return row;
}

void DesignFloorplanStorage::validateRow(const DesignRow& row, DesignRowId ignored) const
{
  constexpr uint32_t kKnownFlags = DesignRowFlag::kHasDo | DesignRowFlag::kHasStep;
  if (row.name.empty() || rowNameInUse(row.name, ignored)) {
    throw std::invalid_argument("design row name is empty or duplicated");
  }
  if (!isValidDesignOrientation(row.orientation)) {
    throw std::invalid_argument("design row orientation is invalid");
  }
  if ((row.flags & ~kKnownFlags) != 0u || row.repeat_count_x == 0u || row.repeat_count_y == 0u || row.step_x < 0 || row.step_y < 0) {
    throw std::invalid_argument("design row repetition is invalid");
  }
  const bool has_do = (row.flags & DesignRowFlag::kHasDo) != 0u;
  const bool has_step = (row.flags & DesignRowFlag::kHasStep) != 0u;
  if (has_step && !has_do) {
    throw std::invalid_argument("design row STEP requires DO/BY");
  }
  if (!has_do && (row.repeat_count_x != 1u || row.repeat_count_y != 1u || row.step_x != 0 || row.step_y != 0)) {
    throw std::invalid_argument("design row without DO/BY must describe one site");
  }
  if (has_do && row.repeat_count_x != 1u && row.repeat_count_y != 1u) {
    throw std::invalid_argument("design row DO/BY requires at least one repetition count to equal one");
  }
  if ((row.repeat_count_x > 1u && row.step_x == 0) || (row.repeat_count_y > 1u && row.step_y == 0)) {
    throw std::invalid_argument("repeated design row axes require a positive step");
  }
  for (const auto& property : row.properties) {
    if (property.name.empty()
        || (property.type != DesignPropertyType::kString && property.type != DesignPropertyType::kInteger
            && property.type != DesignPropertyType::kReal)) {
      throw std::invalid_argument("design row property is invalid");
    }
  }
  static_cast<void>(rowBounds(row));
}

void DesignFloorplanStorage::validateTrackGrid(const DesignTrackGrid& grid) const
{
  constexpr uint32_t kKnownFlags = DesignTrackGridFlag::kHasMask | DesignTrackGridFlag::kSameMask;
  if (!isValidDesignAxis(grid.axis) || grid.track_count == 0u || grid.step <= 0 || (grid.flags & ~kKnownFlags) != 0u) {
    throw std::invalid_argument("design track grid count, step or flags are invalid");
  }
  const bool has_mask = (grid.flags & DesignTrackGridFlag::kHasMask) != 0u;
  if (has_mask != (grid.mask != 0u) || ((grid.flags & DesignTrackGridFlag::kSameMask) != 0u && !has_mask)) {
    throw std::invalid_argument("design track grid mask fields are inconsistent");
  }
  if (has_mask && grid.mask > 3u) {
    throw std::invalid_argument("design track grid mask must be in the DEF 5.8 range 1 through 3");
  }
  static_cast<void>(checkedCoordinate(grid.start, grid.track_count - 1u, grid.step, "design track grid"));

  const auto& registry = _tech_registry.registry();
  for (auto current = grid.layers.begin(); current != grid.layers.end(); ++current) {
    const auto layer = *current;
    if (!layer || !registry.valid(layer.entity()) || !registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity())) {
      throw std::invalid_argument("design track grid references an invalid routing layer");
    }
    if (std::find(grid.layers.begin(), current, layer) != current) {
      throw std::invalid_argument("design track grid contains a duplicate routing layer");
    }
  }
}

void DesignFloorplanStorage::validateGCellGrid(const DesignGCellGrid& grid)
{
  if (!isValidDesignAxis(grid.axis) || grid.line_count == 0u || grid.step <= 0) {
    throw std::invalid_argument("design gcell grid count and step must be positive");
  }
}

Rect DesignFloorplanStorage::rowBounds(const DesignRow& row) const
{
  const auto& registry = _library_registry.registry();
  if (!row.site || !registry.valid(row.site.entity()) || !registry.all_of<LibrarySite>(row.site.entity())) {
    throw std::invalid_argument("design row references an invalid library site");
  }
  const auto& site = registry.get<const LibrarySite>(row.site.entity());
  if (site.width <= 0 || site.height <= 0) {
    throw std::invalid_argument("design row site dimensions must be positive");
  }

  const int32_t width = isQuarterTurnOrientation(row.orientation) ? site.height : site.width;
  const int32_t height = isQuarterTurnOrientation(row.orientation) ? site.width : site.height;
  return Rect{.ll_x = row.origin.x,
              .ll_y = row.origin.y,
              .ur_x = checkedUpper(row.origin.x, row.repeat_count_x, row.step_x, width),
              .ur_y = checkedUpper(row.origin.y, row.repeat_count_y, row.step_y, height)};
}

bool DesignFloorplanStorage::rowNameInUse(std::string_view name, DesignRowId ignored) const
{
  const auto view = _registry.view<const DesignRow>();
  for (const auto entity : view) {
    if (entity != ignored.entity() && view.get<const DesignRow>(entity).name == name) {
      return true;
    }
  }
  return false;
}

void DesignFloorplanStorage::ensureRow(DesignRowId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design row id");
  }
}

void DesignFloorplanStorage::ensureTrackGrid(DesignTrackGridId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design track grid id");
  }
}

void DesignFloorplanStorage::ensureGCellGrid(DesignGCellGridId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid design gcell grid id");
  }
}

}  // namespace eccdb
