#include "tech/via_master/storage/ViaMasterStorage.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {

namespace {

void normalizeAndValidateRects(std::vector<Rect>& rects, const char* role)
{
  for (auto& rect : rects) {
    rect = rect.normalized();
    if (!rect.hasArea()) {
      throw std::invalid_argument(std::string{"via master "} + role + " rectangle has no area");
    }
  }
}

}  // namespace

TechViaMasterId TechViaMasterStorage::createFixedViaMaster(TechViaMaster master, TechViaMasterShapeInput shapes)
{
  const auto mark = _geometry.checkpoint();
  try {
    validateMaster(master);
    validateGeometryInput(shapes);
    const auto geometry = storeGeometry(std::move(shapes));

    const auto entity = _registry.create();
    try {
      _registry.emplace<TechViaMaster>(entity, std::move(master));
      _registry.emplace<TechViaGeometry>(entity, geometry);
      return TechViaMasterId{entity};
    } catch (...) {
      if (_registry.valid(entity)) {
        _registry.destroy(entity);
      }
      throw;
    }
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

TechViaMasterId TechViaMasterStorage::createGeneratedViaMaster(TechViaMaster master, TechGeneratedViaMaster generated,
                                                               TechViaMasterShapeInput shapes)
{
  const auto mark = _geometry.checkpoint();
  try {
    validateMaster(master);
    validateGenerated(generated, shapes);
    validateGeometryInput(shapes);
    const auto geometry = storeGeometry(std::move(shapes));

    const auto entity = _registry.create();
    try {
      _registry.emplace<TechViaMaster>(entity, std::move(master));
      _registry.emplace<TechViaGeometry>(entity, geometry);
      _registry.emplace<TechGeneratedViaMaster>(entity, std::move(generated));
      return TechViaMasterId{entity};
    } catch (...) {
      if (_registry.valid(entity)) {
        _registry.destroy(entity);
      }
      throw;
    }
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

bool TechViaMasterStorage::contains(TechViaMasterId id) const
{
  return _registry.valid(id.entity()) && _registry.all_of<TechViaMaster, TechViaGeometry>(id.entity());
}

TechViaMasterId TechViaMasterStorage::findViaMasterById(uint32_t id) const
{
  const auto entity = static_cast<TechEntity>(id);
  return _registry.valid(entity) && _registry.all_of<TechViaMaster, TechViaGeometry>(entity) ? TechViaMasterId{entity} : TechViaMasterId{};
}

TechViaMasterId TechViaMasterStorage::findViaMaster(std::string_view name) const
{
  const auto view = _registry.view<const TechViaMaster, const TechViaGeometry>();
  for (const auto entity : view) {
    if (!_registry.all_of<TechNdrViaDefinition>(entity) && view.get<const TechViaMaster>(entity).name == name) {
      return TechViaMasterId{entity};
    }
  }
  return {};
}

std::vector<TechViaMasterId> TechViaMasterStorage::viaMasters() const
{
  std::vector<TechViaMasterId> result;
  const auto view = _registry.view<const TechViaMaster, const TechViaGeometry>();
  result.reserve(view.size_hint());
  for (const auto entity : view) {
    if (!_registry.all_of<TechNdrViaDefinition>(entity)) {
      result.emplace_back(entity);
    }
  }
  return result;
}

std::vector<TechViaMasterId> TechViaMasterStorage::fixedViaMasters() const
{
  std::vector<TechViaMasterId> result;
  const auto view = _registry.view<const TechViaMaster, const TechViaGeometry>(entt::exclude<TechGeneratedViaMaster>);
  result.reserve(view.size_hint());
  for (const auto entity : view) {
    if (!_registry.all_of<TechNdrViaDefinition>(entity)) {
      result.emplace_back(entity);
    }
  }
  return result;
}

std::vector<TechViaMasterId> TechViaMasterStorage::generatedViaMasters() const
{
  std::vector<TechViaMasterId> result;
  const auto view = _registry.view<const TechViaMaster, const TechViaGeometry, const TechGeneratedViaMaster>();
  result.reserve(view.size_hint());
  for (const auto entity : view) {
    if (!_registry.all_of<TechNdrViaDefinition>(entity)) {
      result.emplace_back(entity);
    }
  }
  return result;
}

TechViaMaster& TechViaMasterStorage::viaMaster(TechViaMasterId id)
{
  ensureViaMaster(id);
  return _registry.get<TechViaMaster>(id.entity());
}

const TechViaMaster& TechViaMasterStorage::viaMaster(TechViaMasterId id) const
{
  ensureViaMaster(id);
  return _registry.get<TechViaMaster>(id.entity());
}

bool TechViaMasterStorage::hasFixedViaMaster(TechViaMasterId id) const
{
  return contains(id) && !_registry.all_of<TechGeneratedViaMaster>(id.entity());
}

bool TechViaMasterStorage::hasGeneratedViaMaster(TechViaMasterId id) const
{
  return contains(id) && _registry.all_of<TechGeneratedViaMaster>(id.entity());
}

TechGeneratedViaMaster& TechViaMasterStorage::generatedViaMaster(TechViaMasterId id)
{
  ensureGeneratedViaMaster(id);
  return _registry.get<TechGeneratedViaMaster>(id.entity());
}

const TechGeneratedViaMaster& TechViaMasterStorage::generatedViaMaster(TechViaMasterId id) const
{
  ensureGeneratedViaMaster(id);
  return _registry.get<TechGeneratedViaMaster>(id.entity());
}

TechViaGeometry& TechViaMasterStorage::geometry(TechViaMasterId id)
{
  ensureViaMaster(id);
  return _registry.get<TechViaGeometry>(id.entity());
}

const TechViaGeometry& TechViaMasterStorage::geometry(TechViaMasterId id) const
{
  ensureViaMaster(id);
  return _registry.get<TechViaGeometry>(id.entity());
}

std::span<const Rect> TechViaMasterStorage::bottomRects(TechViaMasterId id) const
{
  return _geometry.rectangles(geometry(id).bottom_geometry);
}

std::span<const Rect> TechViaMasterStorage::cutRects(TechViaMasterId id) const
{
  return _geometry.rectangles(geometry(id).cut_geometry);
}

std::span<const Rect> TechViaMasterStorage::topRects(TechViaMasterId id) const
{
  return _geometry.rectangles(geometry(id).top_geometry);
}

uint32_t TechViaMasterStorage::bottomPolygonCount(TechViaMasterId id) const
{
  return _geometry.polygonCount(geometry(id).bottom_geometry);
}

uint32_t TechViaMasterStorage::cutPolygonCount(TechViaMasterId id) const
{
  return _geometry.polygonCount(geometry(id).cut_geometry);
}

uint32_t TechViaMasterStorage::topPolygonCount(TechViaMasterId id) const
{
  return _geometry.polygonCount(geometry(id).top_geometry);
}

std::span<const Point> TechViaMasterStorage::bottomPolygonPoints(TechViaMasterId id, uint32_t polygon_index) const
{
  return _geometry.polygonPoints(geometry(id).bottom_geometry, polygon_index);
}

std::span<const Point> TechViaMasterStorage::cutPolygonPoints(TechViaMasterId id, uint32_t polygon_index) const
{
  return _geometry.polygonPoints(geometry(id).cut_geometry, polygon_index);
}

std::span<const Point> TechViaMasterStorage::topPolygonPoints(TechViaMasterId id, uint32_t polygon_index) const
{
  return _geometry.polygonPoints(geometry(id).top_geometry, polygon_index);
}

uint32_t TechViaMasterStorage::shapeCount(TechViaMasterId id) const
{
  const auto& shapes = geometry(id);
  const auto count = static_cast<uint64_t>(_geometry.shapeCount(shapes.bottom_geometry)) + _geometry.shapeCount(shapes.cut_geometry)
                     + _geometry.shapeCount(shapes.top_geometry);
  if (count > std::numeric_limits<uint32_t>::max()) {
    throw std::overflow_error("too many shapes in tech via master");
  }
  return static_cast<uint32_t>(count);
}

TechViaGeometry TechViaMasterStorage::storeGeometry(TechViaMasterShapeInput shapes)
{
  normalizeAndValidateRects(shapes.bottom_geometry.rects, "bottom");
  normalizeAndValidateRects(shapes.cut_geometry.rects, "cut");
  normalizeAndValidateRects(shapes.top_geometry.rects, "top");

  const auto mark = _geometry.checkpoint();
  try {
    TechViaGeometry geometry;
    geometry.bottom_layer = shapes.bottom_layer;
    geometry.bottom_geometry = _geometry.append(shapes.bottom_geometry);
    geometry.cut_layer = shapes.cut_layer;
    geometry.cut_geometry = _geometry.append(shapes.cut_geometry);
    geometry.top_layer = shapes.top_layer;
    geometry.top_geometry = _geometry.append(shapes.top_geometry);
    geometry.bounding_box = _geometry.bounds(geometry.bottom_geometry)
                                .united(_geometry.bounds(geometry.cut_geometry))
                                .united(_geometry.bounds(geometry.top_geometry));
    return geometry;
  } catch (...) {
    _geometry.rollback(mark);
    throw;
  }
}

void TechViaMasterStorage::ensureViaMaster(TechViaMasterId id) const
{
  if (!contains(id)) {
    throw std::out_of_range("invalid tech via master id");
  }
}

void TechViaMasterStorage::ensureGeneratedViaMaster(TechViaMasterId id) const
{
  if (!hasGeneratedViaMaster(id)) {
    throw std::out_of_range("invalid generated tech via master id");
  }
}

void TechViaMasterStorage::validateMaster(const TechViaMaster& master) const
{
  if (master.name.empty()) {
    throw std::invalid_argument("tech via master name is required");
  }
  const auto view = _registry.view<const TechViaMaster>();
  for (const auto entity : view) {
    if (view.get<const TechViaMaster>(entity).name == master.name) {
      throw std::invalid_argument("duplicate tech via master name");
    }
  }
  if ((master.flags & TechViaMasterFlag::kHasResistance) != 0u && master.resistance < 0.0) {
    throw std::invalid_argument("tech via master resistance is invalid");
  }
  for (const auto& property : master.properties) {
    if (property.name.empty()) {
      throw std::invalid_argument("tech via master property name is required");
    }
  }
}

void TechViaMasterStorage::validateGeometryInput(const TechViaMasterShapeInput& shapes) const
{
  const auto is_valid_conductor = [this](TechConductorLayerRef layer) {
    if (!layer || !_registry.valid(layer.entity)) {
      return false;
    }
    switch (layer.kind) {
      case TechConductorLayerKind::kRouting:
        return _registry.all_of<TechLayerInfo, TechRoutingLayer>(layer.entity);
      case TechConductorLayerKind::kMasterslice:
        return _registry.all_of<TechLayerInfo, TechMastersliceLayer>(layer.entity);
      case TechConductorLayerKind::kNone:
        return false;
    }
    return false;
  };

  if (!is_valid_conductor(shapes.bottom_layer) || !_registry.valid(shapes.cut_layer.entity())
      || !_registry.all_of<TechLayerInfo, TechCutLayer>(shapes.cut_layer.entity()) || !is_valid_conductor(shapes.top_layer)) {
    throw std::invalid_argument("tech via master requires valid bottom, cut, and top layers");
  }
  if (shapes.bottom_geometry.empty() || shapes.cut_geometry.empty() || shapes.top_geometry.empty()) {
    throw std::invalid_argument("tech via master requires geometry on bottom, cut, and top layers");
  }
}

void TechViaMasterStorage::validateGenerated(const TechGeneratedViaMaster& generated, const TechViaMasterShapeInput& shapes) const
{
  if (generated.via_rule_generate) {
    const auto rule = generated.via_rule_generate.entity();
    if (!_registry.valid(rule)
        || !_registry.all_of<TechViaRuleGenerate, TechViaRuleGenerateBottomLayer, TechViaRuleGenerateCutLayer, TechViaRuleGenerateTopLayer>(
            rule)) {
      throw std::invalid_argument("generated tech via references an invalid via generate rule");
    }
    if (_registry.get<TechViaRuleGenerateBottomLayer>(rule).layer != shapes.bottom_layer
        || _registry.get<TechViaRuleGenerateCutLayer>(rule).layer != shapes.cut_layer
        || _registry.get<TechViaRuleGenerateTopLayer>(rule).layer != shapes.top_layer) {
      throw std::invalid_argument("generated tech via layers do not match its generate rule");
    }
  }
  if (generated.row_count == 0 || generated.column_count == 0) {
    throw std::invalid_argument("generated tech via row and column count must be positive");
  }
  if (generated.cut_size_x < 0 || generated.cut_size_y < 0 || generated.cut_spacing_x < 0 || generated.cut_spacing_y < 0) {
    throw std::invalid_argument("generated tech via cut dimensions are invalid");
  }
}

}  // namespace eccdb
