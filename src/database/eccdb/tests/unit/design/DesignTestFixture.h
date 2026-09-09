// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>

#include <string>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

class DesignStorageTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    const auto layer_entity = tech.registry().create();
    tech.registry().emplace<TechLayerInfo>(layer_entity, TechLayerInfo{.name = "M1"});
    tech.registry().emplace<TechRoutingLayer>(layer_entity, TechRoutingLayer{});
    routing_layer = TechRoutingLayerId{layer_entity};
    layer = TechLayerId{layer_entity};

    const auto via_entity = tech.registry().create();
    tech.registry().emplace<TechViaMaster>(via_entity, TechViaMaster{.name = "VIA12"});
    tech_via = TechViaMasterId{via_entity};

    const auto ndr_entity = tech.registry().create();
    tech.registry().emplace<TechNonDefaultRule>(ndr_entity, TechNonDefaultRule{.name = "WIDE"});
    non_default_rule = TechNonDefaultRuleId{ndr_entity};

    site = library.siteStorage().createSite(
        LibrarySite{.name = "CORE", .width = 20, .height = 40, .site_class = LibrarySiteClass::kCore, .symmetry_r90 = true});
    master = library.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "INVX1", .site = site, .width = 20, .height = 40});
    input_term = library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "A", .direction = LibraryMasterTermDirection::kInput});
    output_term = library.masterTermStorage().createMasterTerm(
        master, LibraryMasterTerm{.name = "Y", .direction = LibraryMasterTermDirection::kOutput});
  }

  [[nodiscard]] DesignInstance createInstance(std::string name, Point origin = {}) const
  {
    return DesignInstance{.name = std::move(name),
                          .master = master,
                          .origin = origin,
                          .orientation = DesignOrientation::kN,
                          .placement_status = DesignPlacementStatus::kPlaced};
  }

  TechRegistry tech;
  LibraryStore library{tech};
  DesignStore design{tech, library.libraryRegistry()};
  TechLayerId layer;
  TechRoutingLayerId routing_layer;
  TechViaMasterId tech_via;
  TechNonDefaultRuleId non_default_rule;
  LibrarySiteId site;
  LibraryCellMasterId master;
  LibraryMasterTermId input_term;
  LibraryMasterTermId output_term;
};

}  // namespace eccdb
