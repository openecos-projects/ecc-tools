// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include <type_traits>

#include <eccdb.h>

#include <connectivity/ConnectivityService.h>
#include <context/DatabaseContext.h>
#include <design/DesignStore.h>
#include <instance/InstanceService.h>
#include <library/LibraryStore.h>
#include <tech/TechStore.h>

int main()
{
  static_assert(!std::is_copy_constructible_v<eccdb::Database>);
  static_assert(std::is_trivially_copyable_v<eccdb::DesignNetId>);

  eccdb::TechStore technology;
  eccdb::LibraryStore library{technology.techRegistry()};
  eccdb::DesignStore design{technology.techRegistry(), library.libraryRegistry()};
  eccdb::DatabaseContext context{technology, library, design};
  eccdb::ConnectivityService connectivity{context};
  eccdb::InstanceService instances{context};

  const auto master = library.cellMasterStorage().createCellMaster(eccdb::LibraryCellMaster{.name = "BUF"});
  static_cast<void>(library.masterTermStorage().createMasterTerm(
      master, eccdb::LibraryMasterTerm{.name = "A", .direction = eccdb::LibraryMasterTermDirection::kInput}));
  const auto net = connectivity.createNet(eccdb::DesignNet{.name = "example"});
  const auto instance = instances.create(eccdb::DesignInstance{.name = "u0",
                                                                .master = master,
                                                                .origin = {10, 20},
                                                                .orientation = eccdb::DesignOrientation::kN,
                                                                .placement_status = eccdb::DesignPlacementStatus::kPlaced});
  const auto term = design.netlistStorage().findInstanceTerm(instance, "A");
  if (!term) {
    return 1;
  }
  const auto& registry = design.designRegistry().registry();
  if (!registry.valid(term.entity()) || !registry.all_of<eccdb::DesignInstanceTerm>(term.entity())
      || &registry.get<eccdb::DesignInstanceTerm>(term.entity()) != &design.netlistStorage().instanceTerm(term)) {
    return 1;
  }
  connectivity.connect(term, net);
  if (design.netlistStorage().instanceTerm(term).net != net) {
    return 1;
  }

  static_cast<void>(eccdb::Database::supportsLefDef());
  if (!instances.destroy(instance) || registry.valid(term.entity()) || !design.netlistStorage().instanceTerms(net).empty()) {
    return 1;
  }
  return 0;
}
