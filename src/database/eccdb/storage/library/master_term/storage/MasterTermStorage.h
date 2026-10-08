// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "geometry/DomainGeometryStorage.h"
#include "index/LibraryMasterTermNameIndex.h"
#include "library/LibraryRegistry.h"
#include "library/master_term/model/MasterTermComponents.h"

namespace eccdb {

[[nodiscard]] std::optional<LibraryMasterTermNameIndexEntry> findLibraryMasterTermName(
    const LibraryRegistry::registry_type& registry, LibraryCellMasterId owner, std::string_view name);

class LibraryMasterTermStorage
{
 public:
  using registry_type = LibraryRegistry::registry_type;

  LibraryMasterTermStorage(LibraryRegistry& registry, DomainGeometryStorage& geometry)
      : _registry(registry.registry()), _geometry(geometry)
  {
  }

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

  // Rebuild non-persistent per-master name indexes after direct registry restores.
  void rebuildNameIndexes();

 private:
  void ensureMaster(LibraryCellMasterId id) const;
  void ensureMasterTerm(LibraryMasterTermId id) const;
  void validateMasterTerm(LibraryCellMasterId owner, const LibraryMasterTerm& term, LibraryMasterTermId ignored = {}) const;

  registry_type& _registry;
  DomainGeometryStorage& _geometry;
};

}  // namespace eccdb
