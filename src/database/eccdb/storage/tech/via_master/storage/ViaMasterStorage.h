#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "geometry/GeometryPool.h"
#include "tech/TechRegistry.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_master/storage/ViaMasterInput.h"

namespace eccdb {

// Facade for concrete technology VIA definitions. Fixed and generated VIAs
// share TechViaMaster and TechViaGeometry; only generated VIAs carry the extra
// TechGeneratedViaMaster component.
class TechViaMasterStorage
{
 public:
  using registry_type = TechRegistry::registry_type;

  TechViaMasterStorage(TechRegistry& registry, GeometryPool& geometry) : _registry(registry.registry()), _geometry(geometry) {}

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

  [[nodiscard]] TechViaMasterId createFixedViaMaster(TechViaMaster master, TechViaMasterShapeInput shapes);
  [[nodiscard]] TechViaMasterId createGeneratedViaMaster(TechViaMaster master, TechGeneratedViaMaster generated,
                                                         TechViaMasterShapeInput shapes);

  [[nodiscard]] bool contains(TechViaMasterId id) const;
  [[nodiscard]] TechViaMasterId findViaMasterById(uint32_t id) const;
  [[nodiscard]] TechViaMasterId findViaMaster(std::string_view name) const;
  [[nodiscard]] std::vector<TechViaMasterId> viaMasters() const;
  [[nodiscard]] std::vector<TechViaMasterId> fixedViaMasters() const;
  [[nodiscard]] std::vector<TechViaMasterId> generatedViaMasters() const;

  [[nodiscard]] TechViaMaster& viaMaster(TechViaMasterId id);
  [[nodiscard]] const TechViaMaster& viaMaster(TechViaMasterId id) const;
  [[nodiscard]] bool hasFixedViaMaster(TechViaMasterId id) const;
  [[nodiscard]] bool hasGeneratedViaMaster(TechViaMasterId id) const;
  [[nodiscard]] TechGeneratedViaMaster& generatedViaMaster(TechViaMasterId id);
  [[nodiscard]] const TechGeneratedViaMaster& generatedViaMaster(TechViaMasterId id) const;
  [[nodiscard]] TechViaGeometry& geometry(TechViaMasterId id);
  [[nodiscard]] const TechViaGeometry& geometry(TechViaMasterId id) const;
  [[nodiscard]] std::span<const Rect> bottomRects(TechViaMasterId id) const;
  [[nodiscard]] std::span<const Rect> cutRects(TechViaMasterId id) const;
  [[nodiscard]] std::span<const Rect> topRects(TechViaMasterId id) const;
  [[nodiscard]] uint32_t bottomPolygonCount(TechViaMasterId id) const;
  [[nodiscard]] uint32_t cutPolygonCount(TechViaMasterId id) const;
  [[nodiscard]] uint32_t topPolygonCount(TechViaMasterId id) const;
  [[nodiscard]] std::span<const Point> bottomPolygonPoints(TechViaMasterId id, uint32_t polygon_index) const;
  [[nodiscard]] std::span<const Point> cutPolygonPoints(TechViaMasterId id, uint32_t polygon_index) const;
  [[nodiscard]] std::span<const Point> topPolygonPoints(TechViaMasterId id, uint32_t polygon_index) const;
  [[nodiscard]] uint32_t shapeCount(TechViaMasterId id) const;

 private:
  [[nodiscard]] TechViaGeometry storeGeometry(TechViaMasterShapeInput shapes);
  void ensureViaMaster(TechViaMasterId id) const;
  void ensureGeneratedViaMaster(TechViaMasterId id) const;
  void validateMaster(const TechViaMaster& master) const;
  void validateGeometryInput(const TechViaMasterShapeInput& shapes) const;
  void validateGenerated(const TechGeneratedViaMaster& generated, const TechViaMasterShapeInput& shapes) const;

  registry_type& _registry;
  GeometryPool& _geometry;
};

}  // namespace eccdb
