// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "library/LibraryStore.h"
#include "tech/TechRegistry.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {
namespace {

static_assert(std::is_same_v<decltype(LibrarySite::name), std::string>);
static_assert(std::is_same_v<decltype(LibraryCellMaster::site), std::optional<LibrarySiteId>>);
static_assert(std::is_same_v<decltype(LibraryCellMaster::terms), std::vector<LibraryMasterTermId>>);
static_assert(std::is_same_v<decltype(LibraryMasterTerm::master), LibraryCellMasterId>);
static_assert(std::is_same_v<decltype(LibraryMasterTerm::ports), std::vector<LibraryMasterPortId>>);
static_assert(std::is_same_v<decltype(LibraryMasterPort::layer_clauses), std::vector<LibraryPortLayerGeometry>>);
static_assert(std::is_same_v<decltype(LibraryMasterPort::term), LibraryMasterTermId>);
static_assert(std::is_same_v<decltype(LibraryMasterObs::layer_clauses), std::vector<LibraryObsLayerClause>>);
static_assert(!std::is_default_constructible_v<LibraryStore>);
static_assert(!std::is_copy_constructible_v<LibraryStore>);
static_assert(!std::is_move_constructible_v<LibraryStore>);

TechLayerId createTechLayer(TechRegistry& tech_registry, std::string name)
{
  const auto entity = tech_registry.registry().create();
  tech_registry.registry().emplace<TechLayerInfo>(entity, TechLayerInfo{.name = std::move(name)});
  return TechLayerId{entity};
}

TechViaMasterId createTechVia(TechRegistry& tech_registry, std::string name)
{
  const auto entity = tech_registry.registry().create();
  tech_registry.registry().emplace<TechViaMaster>(entity, TechViaMaster{.name = std::move(name)});
  tech_registry.registry().emplace<TechViaGeometry>(entity,
                                                    TechViaGeometry{.bounding_box = {.ll_x = -50, .ll_y = -5, .ur_x = 50, .ur_y = 5}});
  return TechViaMasterId{entity};
}

TEST(LibraryStoreTest, StoresSeparateSiteAndCellMasterNameDomains)
{
  TechRegistry tech_registry;
  LibraryStore database{tech_registry};
  auto& sites = database.siteStorage();
  auto& masters = database.cellMasterStorage();

  const auto site = sites.createSite(LibrarySite{.name = "CORE",
                                                 .width = 200,
                                                 .height = 400,
                                                 .site_class = LibrarySiteClass::kCore,
                                                 .symmetry_x = true,
                                                 .symmetry_y = true,
                                                 .symmetry_r90 = true});
  const auto master = masters.createCellMaster(LibraryCellMaster{.name = "CORE",
                                                                 .type = LibraryCellMasterType::kCore,
                                                                 .symmetry_x = true,
                                                                 .symmetry_y = true,
                                                                 .symmetry_r90 = true,
                                                                 .pad_filler = true,
                                                                 .site = std::optional<LibrarySiteId>{site},
                                                                 .origin_x = -10,
                                                                 .origin_y = 20,
                                                                 .width = 200,
                                                                 .height = 400});

  EXPECT_EQ(sites.findSite("CORE"), site);
  EXPECT_EQ(masters.findCellMaster("CORE"), master);
  EXPECT_EQ(sites.findSiteById(site.packed()), site);
  EXPECT_EQ(masters.findCellMasterById(master.packed()), master);
  EXPECT_EQ(sites.sites(), (std::vector<LibrarySiteId>{site}));
  EXPECT_EQ(masters.cellMasters(), (std::vector<LibraryCellMasterId>{master}));
  EXPECT_EQ(sites.siteCount(), 1u);
  EXPECT_EQ(masters.cellMasterCount(), 1u);

  const auto& stored_site = sites.site(site);
  EXPECT_EQ(stored_site.width, 200);
  EXPECT_EQ(stored_site.height, 400);
  EXPECT_TRUE(stored_site.symmetry_x);
  EXPECT_TRUE(stored_site.symmetry_y);
  EXPECT_TRUE(stored_site.symmetry_r90);

  const auto& stored_master = masters.cellMaster(master);
  ASSERT_TRUE(stored_master.site.has_value());
  EXPECT_EQ(*stored_master.site, site);
  EXPECT_EQ(stored_master.type, LibraryCellMasterType::kCore);
  EXPECT_TRUE(stored_master.symmetry_x);
  EXPECT_TRUE(stored_master.symmetry_y);
  EXPECT_TRUE(stored_master.symmetry_r90);
  EXPECT_TRUE(stored_master.pad_filler);
  EXPECT_EQ(stored_master.origin_x, -10);
  EXPECT_EQ(stored_master.origin_y, 20);
  EXPECT_EQ(stored_master.width, 200u);
  EXPECT_EQ(stored_master.height, 400u);
  EXPECT_TRUE((database.libraryRegistry().registry().all_of<LibrarySite>(site.entity())));
  EXPECT_TRUE((database.libraryRegistry().registry().all_of<LibraryCellMaster>(master.entity())));
}

TEST(LibraryStoreTest, UpdatesRecordsAndRejectsDuplicateNamesOrInvalidSiteReferences)
{
  TechRegistry tech_registry;
  LibraryStore database{tech_registry};
  auto& sites = database.siteStorage();
  auto& masters = database.cellMasterStorage();

  const auto core = sites.createSite(LibrarySite{.name = "CORE", .width = 200, .height = 400});
  const auto pad = sites.createSite(LibrarySite{.name = "PAD", .width = 600, .height = 600});
  const auto master = masters.createCellMaster(LibraryCellMaster{.name = "INVX1", .site = std::optional<LibrarySiteId>{core}});

  sites.updateSite(
      core,
      LibrarySite{
          .name = "CORE_NEW", .width = 220, .height = 440, .overlap = true, .site_class = LibrarySiteClass::kCore, .symmetry_x = true});
  masters.updateCellMaster(master, LibraryCellMaster{.name = "INVX2",
                                                     .type = LibraryCellMasterType::kCoreSpacer,
                                                     .core_filler = true,
                                                     .site = std::optional<LibrarySiteId>{pad},
                                                     .origin_x = 4,
                                                     .origin_y = -8,
                                                     .width = 220,
                                                     .height = 440});

  EXPECT_EQ(sites.findSite("CORE"), LibrarySiteId{});
  EXPECT_EQ(sites.findSite("CORE_NEW"), core);
  EXPECT_EQ(masters.findCellMaster("INVX1"), LibraryCellMasterId{});
  EXPECT_EQ(masters.findCellMaster("INVX2"), master);
  EXPECT_TRUE(sites.site(core).overlap);
  ASSERT_TRUE(masters.cellMaster(master).site.has_value());
  EXPECT_EQ(*masters.cellMaster(master).site, pad);
  EXPECT_TRUE(masters.cellMaster(master).core_filler);

  EXPECT_THROW((void) sites.createSite(LibrarySite{.name = "PAD"}), std::invalid_argument);
  EXPECT_THROW((void) masters.createCellMaster(LibraryCellMaster{.name = "INVX2"}), std::invalid_argument);
  EXPECT_THROW(sites.updateSite(core, LibrarySite{.name = "PAD"}), std::invalid_argument);
  EXPECT_THROW(masters.updateCellMaster(master, LibraryCellMaster{.name = "INVX2", .site = LibrarySiteId{}}), std::invalid_argument);

  const auto retired = sites.createSite(LibrarySite{.name = "RETIRED"});
  ASSERT_TRUE(sites.destroySite(retired));
  EXPECT_THROW((void) masters.createCellMaster(LibraryCellMaster{.name = "BROKEN", .site = std::optional<LibrarySiteId>{retired}}),
               std::invalid_argument);
}

TEST(LibraryStoreTest, RejectsSiteDestructionUntilCellMastersReleaseIt)
{
  TechRegistry tech_registry;
  LibraryStore database{tech_registry};
  auto& sites = database.siteStorage();
  auto& masters = database.cellMasterStorage();

  const auto site = sites.createSite(LibrarySite{.name = "CORE"});
  const auto master = masters.createCellMaster(LibraryCellMaster{.name = "INVX1", .site = std::optional<LibrarySiteId>{site}});

  EXPECT_FALSE(sites.destroySite(site));
  EXPECT_TRUE(sites.contains(site));
  EXPECT_TRUE(masters.destroyCellMaster(master));
  EXPECT_TRUE(sites.destroySite(site));
  EXPECT_FALSE(sites.contains(site));
}

TEST(LibraryStoreTest, HandlesStaleIdsAcrossAllCrudOperations)
{
  TechRegistry tech_registry;
  LibraryStore database{tech_registry};
  auto& sites = database.siteStorage();
  auto& masters = database.cellMasterStorage();

  const auto site = sites.createSite(LibrarySite{.name = "CORE"});
  const auto master = masters.createCellMaster(LibraryCellMaster{.name = "INVX1"});
  const auto site_packed = site.packed();
  const auto master_packed = master.packed();

  ASSERT_TRUE(sites.destroySite(site));
  ASSERT_TRUE(masters.destroyCellMaster(master));

  const auto replacement_site = sites.createSite(LibrarySite{.name = "NEW_CORE"});
  const auto replacement_master = masters.createCellMaster(LibraryCellMaster{.name = "NEW_INV"});

  EXPECT_FALSE(sites.contains(site));
  EXPECT_FALSE(masters.contains(master));
  EXPECT_EQ(sites.findSiteById(site_packed), LibrarySiteId{});
  EXPECT_EQ(masters.findCellMasterById(master_packed), LibraryCellMasterId{});
  EXPECT_NE(replacement_site, site);
  EXPECT_NE(replacement_master, master);
  EXPECT_EQ(sites.findSiteById(replacement_site.packed()), replacement_site);
  EXPECT_EQ(masters.findCellMasterById(replacement_master.packed()), replacement_master);
  EXPECT_FALSE(sites.destroySite(site));
  EXPECT_FALSE(masters.destroyCellMaster(master));
  EXPECT_THROW((void) sites.site(site), std::out_of_range);
  EXPECT_THROW((void) masters.cellMaster(master), std::out_of_range);
  EXPECT_THROW(sites.updateSite(site, LibrarySite{.name = "NEW_CORE"}), std::out_of_range);
  EXPECT_THROW(masters.updateCellMaster(master, LibraryCellMaster{.name = "NEW_INV"}), std::out_of_range);
}

TEST(LibraryStoreTest, EmbedsHierarchyInObjectComponentsAndPreservesItAcrossUpdates)
{
  TechRegistry tech_registry;
  LibraryStore database{tech_registry};

  const auto master = database.cellMasterStorage().createCellMaster(LibraryCellMaster{.name = "INVX1"});
  const auto term = database.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A"});
  const auto port = database.masterPortStorage().createMasterPort(term);

  EXPECT_EQ(database.cellMasterStorage().cellMaster(master).terms, (std::vector<LibraryMasterTermId>{term}));
  EXPECT_EQ(database.masterTermStorage().masterTerm(term).master, master);
  EXPECT_EQ(database.masterTermStorage().masterTerm(term).ports, (std::vector<LibraryMasterPortId>{port}));
  EXPECT_EQ(database.masterPortStorage().masterPort(port).term, term);

  database.cellMasterStorage().updateCellMaster(master, LibraryCellMaster{.name = "INVX2", .width = 200, .height = 400});
  database.masterTermStorage().updateMasterTerm(term,
                                                LibraryMasterTerm{.name = "A_RENAMED", .direction = LibraryMasterTermDirection::kInput});
  database.masterPortStorage().updateMasterPort(port, LibraryMasterPortInput{.port_class = LibraryMasterPortClass::kCore});

  EXPECT_EQ(database.cellMasterStorage().cellMaster(master).terms, (std::vector<LibraryMasterTermId>{term}));
  EXPECT_EQ(database.masterTermStorage().masterTerm(term).master, master);
  EXPECT_EQ(database.masterTermStorage().masterTerm(term).ports, (std::vector<LibraryMasterPortId>{port}));
  EXPECT_EQ(database.masterPortStorage().masterPort(port).term, term);

  EXPECT_TRUE(database.masterPortStorage().destroyMasterPort(port));
  EXPECT_TRUE(database.masterTermStorage().masterTerm(term).ports.empty());
  EXPECT_TRUE(database.masterTermStorage().destroyMasterTerm(term));
  EXPECT_TRUE(database.cellMasterStorage().cellMaster(master).terms.empty());
}

TEST(LibraryStoreTest, StoresMasterTermsPortsAndUnifiedObsWithTechReferences)
{
  TechRegistry tech_registry;
  const auto m1 = createTechLayer(tech_registry, "M1");
  const auto m2 = createTechLayer(tech_registry, "M2");
  const auto via1 = createTechVia(tech_registry, "V1");
  LibraryStore database{tech_registry};

  const auto master = database.cellMasterStorage().createCellMaster(
      LibraryCellMaster{.name = "INVX1", .type = LibraryCellMasterType::kCore, .width = 200, .height = 400});
  const auto term = database.masterTermStorage().createMasterTerm(master, LibraryMasterTerm{.name = "A",
                                                                                            .direction = LibraryMasterTermDirection::kInput,
                                                                                            .use = LibraryMasterTermUse::kSignal,
                                                                                            .shape = LibraryMasterTermShape::kAbutment});
  const auto port = database.masterPortStorage().createMasterPort(
      term, LibraryMasterPortInput{
                .port_class = LibraryMasterPortClass::kNone,
                .layer_clauses = {{.layer = m1, .geometry = {.rects = {{.ll_x = 0, .ll_y = 10, .ur_x = 20, .ur_y = 30}}}},
                                  {.layer = m2, .geometry = {.polygons = {{.points = {{10, 0}, {30, 0}, {30, 40}, {10, 40}}}}}}},
                .vias = {{.via = via1, .origin = {.x = 10, .y = 20}}}});
  const auto core_port = database.masterPortStorage().createMasterPort(
      term,
      LibraryMasterPortInput{.port_class = LibraryMasterPortClass::kCore,
                             .layer_clauses = {{.layer = m1, .geometry = {.rects = {{.ll_x = 100, .ll_y = 0, .ur_x = 120, .ur_y = 20}}}}}});

  database.cellMasterStorage().setObs(
      master, LibraryMasterObsInput{.layer_clauses = {{.layer = m1,
                                                       .geometry = {.rects = {{.ll_x = 40, .ll_y = 40, .ur_x = 80, .ur_y = 90}},
                                                                    .polygons = {{.points = {{40, 40}, {80, 40}, {80, 90}, {40, 90}}}}}},
                                                      {.layer = m1,
                                                       .flags = LibraryObsLayerFlag::kExceptPgNet | LibraryObsLayerFlag::kHasSpacing,
                                                       .spacing = 20,
                                                       .geometry = {.rects = {{.ll_x = 90, .ll_y = 40, .ur_x = 120, .ur_y = 90}}}},
                                                      {.layer = m2,
                                                       .flags = LibraryObsLayerFlag::kHasDesignRuleWidth,
                                                       .design_rule_width = 40,
                                                       .geometry = {.rects = {{.ll_x = 20, .ll_y = 100, .ur_x = 140, .ur_y = 140}}}}},
                                    .vias = {{.via = via1, .origin = {.x = 70, .y = 80}}}});

  EXPECT_EQ(database.masterTermStorage().masterTerms(master), (std::vector<LibraryMasterTermId>{term}));
  EXPECT_EQ(database.masterPortStorage().masterPorts(term), (std::vector<LibraryMasterPortId>{port, core_port}));
  EXPECT_EQ(database.masterTermStorage().owner(term), master);
  EXPECT_EQ(database.masterPortStorage().owner(port), term);
  EXPECT_EQ(database.masterTermStorage().findMasterTerm(master, "A"), term);
  EXPECT_EQ(database.masterPortStorage().masterPort(core_port).port_class, LibraryMasterPortClass::kCore);
  const auto& stored_port = database.masterPortStorage().masterPort(port);
  ASSERT_EQ(stored_port.layer_clauses.size(), 2u);
  EXPECT_EQ(stored_port.layer_clauses[0].layer, m1);
  EXPECT_EQ(database.geometryPool().rectangles(stored_port.layer_clauses[0].geometry).front(), (Rect{0, 10, 20, 30}));
  EXPECT_EQ(stored_port.layer_clauses[1].layer, m2);
  ASSERT_EQ(database.geometryPool().polygonCount(stored_port.layer_clauses[1].geometry), 1u);
  EXPECT_EQ(database.geometryPool().polygonPoints(stored_port.layer_clauses[1].geometry, 0).size(), 4u);
  EXPECT_EQ(stored_port.vias.front().via, via1);
  const std::optional<Rect> expected_port_bounds{Rect{.ll_x = -40, .ll_y = 0, .ur_x = 60, .ur_y = 40}};
  EXPECT_EQ(database.masterPortStorage().boundingBox(port), expected_port_bounds);
  ASSERT_TRUE(database.cellMasterStorage().hasObs(master));
  const auto& obs = database.cellMasterStorage().obs(master);
  ASSERT_EQ(obs.layer_clauses.size(), 3u);
  EXPECT_EQ(obs.layer_clauses[0].layer, m1);
  EXPECT_EQ(obs.layer_clauses[1].layer, m1);
  EXPECT_EQ(obs.layer_clauses[1].spacing, 20);
  EXPECT_NE(obs.layer_clauses[1].flags & LibraryObsLayerFlag::kExceptPgNet, 0u);
  EXPECT_EQ(obs.layer_clauses[2].layer, m2);
  EXPECT_EQ(obs.layer_clauses[2].design_rule_width, 40);
  ASSERT_EQ(database.geometryPool().polygonCount(obs.layer_clauses[0].geometry), 1u);
  EXPECT_EQ(database.geometryPool().polygonPoints(obs.layer_clauses[0].geometry, 0).front(), (Point{40, 40}));
  ASSERT_EQ(obs.vias.size(), 1u);
  EXPECT_EQ(obs.vias.front().via, via1);
  EXPECT_TRUE((database.libraryRegistry().registry().all_of<LibraryMasterObs>(master.entity())));

  EXPECT_THROW((void) database.masterPortStorage().createMasterPort(
                   term,
                   LibraryMasterPortInput{
                       .layer_clauses = {{.layer = TechLayerId{}, .geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 1, .ur_y = 1}}}}}}),
               std::invalid_argument);

  EXPECT_THROW(database.cellMasterStorage().setObs(master, LibraryMasterObsInput{}), std::invalid_argument);
  EXPECT_THROW(database.cellMasterStorage().setObs(
                   master, LibraryMasterObsInput{.layer_clauses
                                                 = {{.layer = m1,
                                                     .flags = LibraryObsLayerFlag::kHasSpacing | LibraryObsLayerFlag::kHasDesignRuleWidth,
                                                     .spacing = 20,
                                                     .design_rule_width = 40,
                                                     .geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 10, .ur_y = 10}}}}}}),
               std::invalid_argument);

  database.cellMasterStorage().clearObs(master);
  EXPECT_FALSE(database.cellMasterStorage().hasObs(master));
  EXPECT_THROW((void) database.cellMasterStorage().obs(master), std::out_of_range);

  EXPECT_TRUE(database.cellMasterStorage().destroyCellMaster(master));
  EXPECT_FALSE(database.masterTermStorage().contains(term));
  EXPECT_FALSE(database.masterPortStorage().contains(port));
  EXPECT_FALSE(database.masterPortStorage().contains(core_port));
}

}  // namespace
}  // namespace eccdb
