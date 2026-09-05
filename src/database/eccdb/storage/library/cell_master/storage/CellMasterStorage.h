#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "geometry/GeometryPool.h"
#include "library/LibraryRegistry.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/cell_master/storage/MasterObsInput.h"

namespace eccdb {

class TechRegistry;

class LibraryCellMasterStorage
{
 public:
  using registry_type = LibraryRegistry::registry_type;

  LibraryCellMasterStorage(LibraryRegistry& registry, const TechRegistry& tech_registry, GeometryPool& geometry)
      : _registry(registry.registry()), _tech_registry(tech_registry), _geometry(geometry)
  {
  }

  [[nodiscard]] LibraryCellMasterId createCellMaster(LibraryCellMaster master);
  [[nodiscard]] LibraryCellMasterId createCellMasterTrusted(LibraryCellMaster master);
  [[nodiscard]] bool contains(LibraryCellMasterId id) const;
  [[nodiscard]] LibraryCellMasterId findCellMasterById(uint32_t id) const;
  [[nodiscard]] LibraryCellMasterId findCellMaster(std::string_view name) const;
  [[nodiscard]] std::vector<LibraryCellMasterId> cellMasters() const;
  [[nodiscard]] std::size_t cellMasterCount() const;

  [[nodiscard]] const LibraryCellMaster& cellMaster(LibraryCellMasterId id) const;
  void updateCellMaster(LibraryCellMasterId id, LibraryCellMaster master);
  void updateCellMasterTrusted(LibraryCellMasterId id, LibraryCellMaster master);
  [[nodiscard]] bool destroyCellMaster(LibraryCellMasterId id);

  [[nodiscard]] bool hasObs(LibraryCellMasterId owner) const;
  [[nodiscard]] const LibraryMasterObs& obs(LibraryCellMasterId owner) const;
  void setObs(LibraryCellMasterId owner, LibraryMasterObsInput obs);
  void appendObs(LibraryCellMasterId owner, LibraryMasterObsInput obs);
  void appendObsTrusted(LibraryCellMasterId owner, LibraryMasterObsInput obs);
  void clearObs(LibraryCellMasterId owner);

 private:
  void ensureCellMaster(LibraryCellMasterId id) const;
  void validateCellMaster(const LibraryCellMaster& master, LibraryCellMasterId ignored = {}) const;
  void validateObs(const LibraryMasterObsInput& obs) const;
  void validateObsLayerClause(const LibraryObsLayerClauseInput& clause) const;
  [[nodiscard]] LibraryMasterObs materializeObs(LibraryMasterObsInput obs, bool validate_geometry);

  registry_type& _registry;
  const TechRegistry& _tech_registry;
  GeometryPool& _geometry;
};

}  // namespace eccdb
