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

#include "geometry/DomainGeometryStorage.h"
#include "library/LibraryRegistry.h"
#include "library/cell_master/storage/CellMasterStorage.h"
#include "library/master_port/storage/MasterPortStorage.h"
#include "library/master_term/storage/MasterTermStorage.h"
#include "library/site/storage/SiteStorage.h"

namespace eccdb {

using LibraryGeometryStorage = DomainGeometryStorage;

class TechRegistry;

struct LibraryStoreOptions
{
};

// A LibraryStore is one loaded cell-library scope. It is a composition root
// for its registry-backed object storages, not itself an EnTT entity.
class LibraryStore
{
 public:
  // Technology outlives the library because PORT and OBS geometry reference
  // Tech layers and ViaMasters from a different registry domain.
  explicit LibraryStore(const TechRegistry& tech_registry, LibraryStoreOptions options = {});
  LibraryStore(const LibraryStore&) = delete;
  LibraryStore& operator=(const LibraryStore&) = delete;
  LibraryStore(LibraryStore&&) = delete;
  LibraryStore& operator=(LibraryStore&&) = delete;

  [[nodiscard]] LibraryRegistry& libraryRegistry() noexcept { return _registry; }
  [[nodiscard]] const LibraryRegistry& libraryRegistry() const noexcept { return _registry; }
  [[nodiscard]] LibraryGeometryStorage& geometryStorage() noexcept { return _geometry; }
  [[nodiscard]] const LibraryGeometryStorage& geometryStorage() const noexcept { return _geometry; }
  [[nodiscard]] GeometryRegistry& geometryRegistry() noexcept { return _geometry.geometryRegistry(); }
  [[nodiscard]] const GeometryRegistry& geometryRegistry() const noexcept { return _geometry.geometryRegistry(); }

  [[nodiscard]] LibrarySiteStorage& siteStorage() noexcept { return _sites; }
  [[nodiscard]] const LibrarySiteStorage& siteStorage() const noexcept { return _sites; }
  [[nodiscard]] LibraryCellMasterStorage& cellMasterStorage() noexcept { return _cell_masters; }
  [[nodiscard]] const LibraryCellMasterStorage& cellMasterStorage() const noexcept { return _cell_masters; }
  [[nodiscard]] LibraryMasterTermStorage& masterTermStorage() noexcept { return _master_terms; }
  [[nodiscard]] const LibraryMasterTermStorage& masterTermStorage() const noexcept { return _master_terms; }
  [[nodiscard]] LibraryMasterPortStorage& masterPortStorage() noexcept { return _master_ports; }
  [[nodiscard]] const LibraryMasterPortStorage& masterPortStorage() const noexcept { return _master_ports; }

  // Binary import invokes this after restoring both library registries.
  // Normal reads intentionally remain unchecked.
  void validateRestoredGeometry() const;

 private:
  const TechRegistry& _tech_registry;
  LibraryRegistry _registry;
  LibraryGeometryStorage _geometry;
  LibrarySiteStorage _sites;
  LibraryCellMasterStorage _cell_masters;
  LibraryMasterTermStorage _master_terms;
  LibraryMasterPortStorage _master_ports;
};

}  // namespace eccdb
