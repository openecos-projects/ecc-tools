#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "library/LibraryRegistry.h"
#include "library/master_term/model/MasterTermComponents.h"

namespace eccdb {

class LibraryMasterTermStorage
{
 public:
  using registry_type = LibraryRegistry::registry_type;

  explicit LibraryMasterTermStorage(LibraryRegistry& registry) : _registry(registry.registry()) {}

  [[nodiscard]] LibraryMasterTermId createMasterTerm(LibraryCellMasterId owner, LibraryMasterTerm term);
  [[nodiscard]] LibraryMasterTermId createMasterTermTrusted(LibraryCellMasterId owner, LibraryMasterTerm term);
  [[nodiscard]] bool contains(LibraryMasterTermId id) const;
  [[nodiscard]] LibraryMasterTermId findMasterTermById(uint32_t id) const;
  [[nodiscard]] LibraryMasterTermId findMasterTerm(LibraryCellMasterId owner, std::string_view name) const;
  [[nodiscard]] std::vector<LibraryMasterTermId> masterTerms() const;
  [[nodiscard]] std::vector<LibraryMasterTermId> masterTerms(LibraryCellMasterId owner) const;
  [[nodiscard]] std::size_t masterTermCount() const;

  [[nodiscard]] const LibraryMasterTerm& masterTerm(LibraryMasterTermId id) const;
  [[nodiscard]] LibraryCellMasterId owner(LibraryMasterTermId id) const;
  void updateMasterTerm(LibraryMasterTermId id, LibraryMasterTerm term);
  [[nodiscard]] bool destroyMasterTerm(LibraryMasterTermId id);

 private:
  void ensureMaster(LibraryCellMasterId id) const;
  void ensureMasterTerm(LibraryMasterTermId id) const;
  void validateMasterTerm(LibraryCellMasterId owner, const LibraryMasterTerm& term, LibraryMasterTermId ignored = {}) const;

  registry_type& _registry;
};

}  // namespace eccdb
