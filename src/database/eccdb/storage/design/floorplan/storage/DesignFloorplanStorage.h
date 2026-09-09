#pragma once

#include <string_view>
#include <vector>

#include "design/DesignRegistry.h"
#include "design/floorplan/model/FloorplanComponents.h"

namespace eccdb {

class LibraryRegistry;
class TechRegistry;

// CRUD for DEF ROW, TRACKS and GCELLGRID entities. DieArea is a singleton
// component on DesignRoot and is accessed through DesignGlobalStorage.
class DesignFloorplanStorage
{
 public:
  using registry_type = DesignRegistry::registry_type;

  DesignFloorplanStorage(DesignRegistry& design_registry, const TechRegistry& tech_registry, const LibraryRegistry& library_registry,
                         DesignRootId root)
      : _registry(design_registry.registry()), _tech_registry(tech_registry), _library_registry(library_registry), _root(root)
  {
  }

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }
  void rebindRoot(DesignRootId root) noexcept { _root = root; }

  [[nodiscard]] DesignRowId createRow(DesignRow row);
  void updateRow(DesignRowId id, DesignRow row);
  [[nodiscard]] bool destroyRow(DesignRowId id);
  [[nodiscard]] bool contains(DesignRowId id) const;
  [[nodiscard]] DesignRowId findRow(std::string_view name) const;
  [[nodiscard]] const DesignRow& row(DesignRowId id) const;
  [[nodiscard]] Rect rowBounds(DesignRowId id) const;
  [[nodiscard]] std::vector<DesignRowId> rows() const;

  [[nodiscard]] DesignTrackGridId createTrackGrid(DesignTrackGrid grid);
  void updateTrackGrid(DesignTrackGridId id, DesignTrackGrid grid);
  [[nodiscard]] bool destroyTrackGrid(DesignTrackGridId id);
  [[nodiscard]] bool contains(DesignTrackGridId id) const;
  [[nodiscard]] const DesignTrackGrid& trackGrid(DesignTrackGridId id) const;
  [[nodiscard]] std::vector<DesignTrackGridId> trackGrids() const;
  [[nodiscard]] std::vector<DesignTrackGridId> trackGridsForLayer(TechRoutingLayerId layer) const;
  [[nodiscard]] std::vector<int32_t> trackCoordinates(DesignTrackGridId id) const;
  [[nodiscard]] std::vector<int32_t> trackCoordinates(TechRoutingLayerId layer, DesignAxis axis) const;

  [[nodiscard]] DesignGCellGridId createGCellGrid(DesignGCellGrid grid);
  void updateGCellGrid(DesignGCellGridId id, DesignGCellGrid grid);
  [[nodiscard]] bool destroyGCellGrid(DesignGCellGridId id);
  [[nodiscard]] bool contains(DesignGCellGridId id) const;
  [[nodiscard]] const DesignGCellGrid& gcellGrid(DesignGCellGridId id) const;
  [[nodiscard]] std::vector<DesignGCellGridId> gcellGrids() const;

  [[nodiscard]] bool hasCoreArea() const noexcept;
  [[nodiscard]] const DesignCoreArea& coreArea() const;
  void setCoreArea(DesignCoreArea area);
  [[nodiscard]] Rect coreBounds() const;
  [[nodiscard]] bool referencesSite(LibrarySiteId id) const;
  [[nodiscard]] uint32_t rowCount() const;
  [[nodiscard]] uint32_t trackGridCount() const;
  [[nodiscard]] uint32_t gcellGridCount() const;

 private:
  [[nodiscard]] DesignRow normalizeRow(DesignRow row) const;
  void validateRow(const DesignRow& row, DesignRowId ignored = {}) const;
  void validateTrackGrid(const DesignTrackGrid& grid) const;
  static void validateGCellGrid(const DesignGCellGrid& grid);
  [[nodiscard]] Rect rowBounds(const DesignRow& row) const;
  [[nodiscard]] bool rowNameInUse(std::string_view name, DesignRowId ignored = {}) const;
  void ensureRow(DesignRowId id) const;
  void ensureTrackGrid(DesignTrackGridId id) const;
  void ensureGCellGrid(DesignGCellGridId id) const;

  registry_type& _registry;
  const TechRegistry& _tech_registry;
  const LibraryRegistry& _library_registry;
  DesignRootId _root;
};

}  // namespace eccdb
