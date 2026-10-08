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
