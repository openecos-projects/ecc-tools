#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "geometry/GeometryPool.h"
#include "library/LibraryRegistry.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_port/storage/MasterPortInput.h"

namespace eccdb {

class TechRegistry;

class LibraryMasterPortStorage
{
 public:
  using registry_type = LibraryRegistry::registry_type;

  LibraryMasterPortStorage(LibraryRegistry& registry, const TechRegistry& tech_registry, GeometryPool& geometry)
      : _registry(registry.registry()), _tech_registry(tech_registry), _geometry(geometry)
  {
  }

  [[nodiscard]] LibraryMasterPortId createMasterPort(LibraryMasterTermId owner, LibraryMasterPortInput port = {});
  [[nodiscard]] LibraryMasterPortId createMasterPortTrusted(LibraryMasterTermId owner, LibraryMasterPortInput port = {});
  [[nodiscard]] bool contains(LibraryMasterPortId id) const;
  [[nodiscard]] LibraryMasterPortId findMasterPortById(uint32_t id) const;
  [[nodiscard]] std::vector<LibraryMasterPortId> masterPorts() const;
  [[nodiscard]] std::vector<LibraryMasterPortId> masterPorts(LibraryMasterTermId owner) const;
  [[nodiscard]] std::size_t masterPortCount() const;

  [[nodiscard]] const LibraryMasterPort& masterPort(LibraryMasterPortId id) const;
  [[nodiscard]] LibraryMasterTermId owner(LibraryMasterPortId id) const;
  [[nodiscard]] std::optional<Rect> boundingBox(LibraryMasterPortId id) const;
  void updateMasterPort(LibraryMasterPortId id, LibraryMasterPortInput port);
  [[nodiscard]] bool destroyMasterPort(LibraryMasterPortId id);

 private:
  void ensureMasterTerm(LibraryMasterTermId id) const;
  void ensureMasterPort(LibraryMasterPortId id) const;
  void validateMasterPort(const LibraryMasterPortInput& port) const;
  [[nodiscard]] LibraryMasterPort materializeMasterPort(LibraryMasterPortInput port, bool validate_geometry);

  registry_type& _registry;
  const TechRegistry& _tech_registry;
  GeometryPool& _geometry;
};

}  // namespace eccdb
