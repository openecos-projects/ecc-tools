#pragma once

#include "design/DesignRegistry.h"
#include "design/constraint/storage/DesignConstraintStorage.h"
#include "design/fill/storage/DesignFillStorage.h"
#include "design/floorplan/storage/DesignFloorplanStorage.h"
#include "design/geometry/DesignGeometryStorage.h"
#include "design/geometry/DesignGeometrySpatialIndex.h"
#include "design/geometry/DesignGeometryOverview.h"
#include "design/global/storage/DesignGlobalStorage.h"
#include "design/guide/storage/DesignGuideStorage.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/pin_access/storage/DesignPinAccessStorage.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "library/LibraryRegistry.h"
#include "tech/TechRegistry.h"

namespace eccdb {

class BinaryDatabaseImporter;

// Composition root for one physical design. Tech and Library registries are
// non-owning dependencies and must outlive this database. While Design pins
// cache pointers, no LibraryMasterTerm may be removed and its storage must not
// be reordered; removing even an unrelated term can move a referenced term.
class DesignStore
{
 public:
  // The optional registry is borrowed from TechStore. Legacy callers that
  // only have a TechRegistry remain valid, but cannot expand technology-VIA
  // primitives into per-layer design overview geometry.
  DesignStore(const TechRegistry& tech_registry, const LibraryRegistry& library_registry,
              const GeometryRegistry* tech_geometry_registry = nullptr);
  DesignStore(const DesignStore&) = delete;
  DesignStore& operator=(const DesignStore&) = delete;
  DesignStore(DesignStore&&) = delete;
  DesignStore& operator=(DesignStore&&) = delete;

  [[nodiscard]] const TechRegistry& techRegistry() const noexcept { return _tech_registry; }
  [[nodiscard]] const LibraryRegistry& libraryRegistry() const noexcept { return _library_registry; }
  [[nodiscard]] DesignRegistry& designRegistry() noexcept { return _registry; }
  [[nodiscard]] const DesignRegistry& designRegistry() const noexcept { return _registry; }
  [[nodiscard]] DesignRootId rootId() const noexcept { return _root; }

  [[nodiscard]] DesignGlobalStorage& globalStorage() noexcept { return _global; }
  [[nodiscard]] const DesignGlobalStorage& globalStorage() const noexcept { return _global; }
  [[nodiscard]] DesignFloorplanStorage& floorplanStorage() noexcept { return _floorplan; }
  [[nodiscard]] const DesignFloorplanStorage& floorplanStorage() const noexcept { return _floorplan; }
  [[nodiscard]] DesignNetlistStorage& netlistStorage() noexcept { return _netlist; }
  [[nodiscard]] const DesignNetlistStorage& netlistStorage() const noexcept { return _netlist; }
  [[nodiscard]] DesignRoutingStorage& routingStorage() noexcept { return _routing; }
  [[nodiscard]] const DesignRoutingStorage& routingStorage() const noexcept { return _routing; }
  void setTechnologyGeometryRegistry(const GeometryRegistry* tech_geometry_registry) noexcept
  {
    _routing.setTechnologyGeometryRegistry(tech_geometry_registry);
  }
  [[nodiscard]] DesignGeometryStorage& geometryStorage() noexcept { return _geometry; }
  [[nodiscard]] const DesignGeometryStorage& geometryStorage() const noexcept { return _geometry; }
  // Derived and lazy: constructing a DesignStore does not build this index.
  [[nodiscard]] DesignGeometrySpatialIndex& geometryIndex() noexcept { return _geometry_index; }
  [[nodiscard]] const DesignGeometrySpatialIndex& geometryIndex() const noexcept { return _geometry_index; }
  // Runtime-only level-of-detail summaries for GUI overview rendering.  This
  // cache is derived from GeometryRegistry and is never part of persistence.
  [[nodiscard]] DesignGeometryOverview& geometryOverview() noexcept { return _geometry_overview; }
  [[nodiscard]] const DesignGeometryOverview& geometryOverview() const noexcept { return _geometry_overview; }
  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _geometry.registry(); }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _geometry.registry(); }
  [[nodiscard]] DesignGuideStorage& guideStorage() noexcept { return _guides; }
  [[nodiscard]] const DesignGuideStorage& guideStorage() const noexcept { return _guides; }
  [[nodiscard]] DesignPinAccessStorage& pinAccessStorage() noexcept { return _pin_access; }
  [[nodiscard]] const DesignPinAccessStorage& pinAccessStorage() const noexcept { return _pin_access; }
  [[nodiscard]] DesignConstraintStorage& constraintStorage() noexcept { return _constraints; }
  [[nodiscard]] const DesignConstraintStorage& constraintStorage() const noexcept { return _constraints; }
  [[nodiscard]] DesignFillStorage& fillStorage() noexcept { return _fills; }
  [[nodiscard]] const DesignFillStorage& fillStorage() const noexcept { return _fills; }

 private:
  friend class BinaryDatabaseImporter;

  void resetForBinaryLoad();
  void bindBinaryLoadedRoot();

  const TechRegistry& _tech_registry;
  const LibraryRegistry& _library_registry;
  DesignRegistry _registry;
  DesignRootId _root;
  DesignGlobalStorage _global;
  DesignFloorplanStorage _floorplan;
  DesignNetlistStorage _netlist;
  DesignRoutingStorage _routing;
  DesignGeometryStorage _geometry;
  DesignGeometrySpatialIndex _geometry_index;
  DesignGeometryOverview _geometry_overview;
  DesignGuideStorage _guides;
  DesignPinAccessStorage _pin_access;
  DesignConstraintStorage _constraints;
  DesignFillStorage _fills;
};

}  // namespace eccdb
