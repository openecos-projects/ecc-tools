// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <entt/container/dense_map.hpp>

#include "library/LibraryStore.h"

namespace idb {
class IdbLayout;
class IdbSite;
}  // namespace idb

namespace eccdb {

class IdbTechImporter;

// Imports the library-owned part of a legacy IdbLayout. The hierarchy is
// materialized through Library storage APIs so both owner directions remain
// consistent: CellMaster -> Term -> Port and Port/Term -> owner.
class IdbLibraryImporter
{
 public:
  IdbLibraryImporter(LibraryStore& database, const IdbTechImporter& tech_importer) : _database(database), _tech_importer(tech_importer)
  {
  }

  void import(::idb::IdbLayout& source);

 private:
  LibraryStore& _database;
  const IdbTechImporter& _tech_importer;
  entt::dense_map<const ::idb::IdbSite*, LibrarySiteId> _site_ids;
  bool _imported = false;
};

}  // namespace eccdb
