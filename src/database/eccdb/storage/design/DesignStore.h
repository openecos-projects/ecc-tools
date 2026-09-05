#pragma once

#include "design/DesignRegistry.h"
#include "design/constraint/storage/DesignConstraintStorage.h"
#include "design/fill/storage/DesignFillStorage.h"
#include "design/floorplan/storage/DesignFloorplanStorage.h"
#include "design/global/storage/DesignGlobalStorage.h"
#include "design/netlist/storage/DesignNetlistStorage.h"
#include "design/routing/storage/DesignRoutingStorage.h"
#include "library/LibraryRegistry.h"
#include "tech/TechRegistry.h"

namespace eccdb {

class BinaryDatabaseImporter;

// Composition root for one physical design. Tech and Library registries are
// non-owning dependencies and must outlive this database.
class DesignStore
{
 public:
  DesignStore(const TechRegistry& tech_registry, const LibraryRegistry& library_registry);
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
  DesignConstraintStorage _constraints;
  DesignFillStorage _fills;
};

}  // namespace eccdb
