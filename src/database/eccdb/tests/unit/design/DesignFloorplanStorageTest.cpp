// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "DesignTestFixture.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/global/model/DesignGlobalComponents.h"

namespace eccdb {
namespace {

static_assert(!std::is_copy_constructible_v<DesignStore>);
static_assert(!std::is_move_constructible_v<DesignStore>);
static_assert(std::is_same_v<DesignRegistry::entity_type, DesignEntity>);
static_assert(sizeof(DesignEntity) * 8 == ActiveDesignEntitySchema::storage_bits);
static_assert(entt::entt_traits<DesignEntity>::entity_mask == ActiveDesignEntitySchema::entity_mask);
static_assert(entt::entt_traits<DesignEntity>::version_mask == ActiveDesignEntitySchema::version_mask);

TEST_F(DesignStorageTest, StoresRootRowsTracksAndGCellsInOneRegistry)
{
  auto& global = design.globalStorage();
  auto& floorplan = design.floorplanStorage();

  global.setInfo(DesignInfo{.name = "simple", .database_units_per_micron = 1000});
  global.setDieArea(DesignDieArea{.boundary = {{0, 0}, {8000, 0}, {8000, 8000}, {0, 8000}}});

  EXPECT_EQ(global.info().name, "simple");
  EXPECT_EQ(global.dieBounds(), (Rect{0, 0, 8000, 8000}));
  EXPECT_EQ(floorplan.coreBounds(), global.dieBounds());

  const auto row0 = floorplan.createRow(DesignRow{.name = "ROW0",
                                                  .site = site,
                                                  .origin = {100, 200},
                                                  .orientation = DesignOrientation::kN,
                                                  .repeat_count_x = 3,
                                                  .repeat_count_y = 1,
                                                  .step_x = 20,
                                                  .step_y = 0});
  const auto row1 = floorplan.createRow(DesignRow{.name = "ROW1",
                                                  .site = site,
                                                  .origin = {100, 300},
                                                  .orientation = DesignOrientation::kE,
                                                  .repeat_count_x = 1,
                                                  .repeat_count_y = 2,
                                                  .step_x = 0,
                                                  .step_y = 20});
  const auto tracks = floorplan.createTrackGrid(DesignTrackGrid{.axis = DesignAxis::kX,
                                                                .start = 50,
                                                                .track_count = 100,
                                                                .step = 80,
                                                                .flags = DesignTrackGridFlag::kHasMask,
                                                                .mask = 1,
                                                                .layers = {routing_layer}});
  const auto gcells = floorplan.createGCellGrid(DesignGCellGrid{.axis = DesignAxis::kY, .start = 0, .line_count = 20, .step = 400});

  EXPECT_EQ(floorplan.rowBounds(row0), (Rect{100, 200, 160, 240}));
  EXPECT_EQ(floorplan.rowBounds(row1), (Rect{100, 300, 140, 340}));
  EXPECT_EQ(floorplan.coreBounds(), (Rect{100, 200, 160, 340}));
  EXPECT_EQ(floorplan.findRow("ROW0"), row0);
  EXPECT_EQ(floorplan.trackGrid(tracks).layers.front(), routing_layer);
  EXPECT_EQ(floorplan.trackGridsForLayer(routing_layer), (std::vector<DesignTrackGridId>{tracks}));
  EXPECT_EQ(floorplan.trackCoordinates(tracks).front(), 50);
  EXPECT_EQ(floorplan.trackCoordinates(tracks).back(), 7970);
  EXPECT_EQ(floorplan.gcellGrid(gcells).line_count, 20u);
  EXPECT_EQ(floorplan.rowCount(), 2u);
  EXPECT_EQ(floorplan.trackGridCount(), 1u);
  EXPECT_EQ(floorplan.gcellGridCount(), 1u);
  EXPECT_TRUE(floorplan.referencesSite(site));

  const auto& registry = design.designRegistry().registry();
  EXPECT_TRUE((registry.all_of<DesignRoot, DesignInfo, DesignDieArea>(design.rootId().entity())));
  EXPECT_TRUE(registry.all_of<DesignRow>(row0.entity()));
  EXPECT_TRUE(registry.all_of<DesignTrackGrid>(tracks.entity()));
  EXPECT_TRUE(registry.all_of<DesignGCellGrid>(gcells.entity()));
}

TEST_F(DesignStorageTest, ValidatesGlobalAndFloorplanReferences)
{
  auto& global = design.globalStorage();
  auto& floorplan = design.floorplanStorage();

  EXPECT_THROW(global.setInfo(DesignInfo{}), std::invalid_argument);
  EXPECT_NO_THROW(global.setDieArea(DesignDieArea{.boundary = {{0, 0}, {0, 10}}}));
  EXPECT_EQ(global.dieBounds(), (Rect{0, 0, 0, 10}));
  EXPECT_THROW(global.setDieArea(DesignDieArea{.boundary = {{10, 0}, {0, 10}}}), std::invalid_argument);
  EXPECT_THROW(global.setDieArea(DesignDieArea{.boundary = {{0, 0}, {10, 10}, {0, 20}, {-10, 10}}}), std::invalid_argument);
  EXPECT_THROW(global.setDieArea(DesignDieArea{.boundary = {{0, 0}, {40, 0}, {40, 40}, {10, 40}, {10, 10}, {30, 10}, {30, 30}, {0, 30}}}),
               std::invalid_argument);
  EXPECT_NO_THROW(global.setDieArea(DesignDieArea{.boundary = {{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}}}));
  EXPECT_EQ(global.dieBounds(), (Rect{0, 0, 100, 100}));

  global.setDieArea(DesignDieArea{.boundary = {{0, 0}, {100, 100}}});
  const auto row = floorplan.createRow(DesignRow{.name = "ROW0", .site = site, .repeat_count_x = 2, .repeat_count_y = 1, .step_x = 20});
  EXPECT_THROW((void) floorplan.createRow(DesignRow{.name = "ROW0", .site = site}), std::invalid_argument);
  const auto layerless_tracks = floorplan.createTrackGrid(DesignTrackGrid{.track_count = 1, .step = 1});
  EXPECT_TRUE(floorplan.contains(layerless_tracks));
  EXPECT_THROW((void) floorplan.createTrackGrid(DesignTrackGrid{.track_count = 0, .step = 1}), std::invalid_argument);
  EXPECT_THROW((void) floorplan.createTrackGrid(DesignTrackGrid{.axis = static_cast<DesignAxis>(255), .track_count = 1, .step = 1}),
               std::invalid_argument);
  EXPECT_THROW(
      (void) floorplan.createTrackGrid(DesignTrackGrid{.start = std::numeric_limits<int32_t>::max() - 1, .track_count = 2, .step = 2}),
      std::overflow_error);
  EXPECT_THROW((void) floorplan.createGCellGrid(DesignGCellGrid{.line_count = 1, .step = 0}), std::invalid_argument);

  EXPECT_TRUE(floorplan.destroyRow(row));
  EXPECT_FALSE(floorplan.destroyRow(row));
  EXPECT_EQ(floorplan.coreBounds(), global.dieBounds());
}

TEST_F(DesignStorageTest, DerivesCoreFromNonPadRowsAndSupportsExplicitCore)
{
  auto& floorplan = design.floorplanStorage();
  design.globalStorage().setDieArea(DesignDieArea{.boundary = {{0, 0}, {1000, 1000}}});
  const auto pad_site
      = library.siteStorage().createSite(LibrarySite{.name = "PAD", .width = 100, .height = 200, .site_class = LibrarySiteClass::kPad});

  const auto core_row = floorplan.createRow(DesignRow{
      .name = "CORE_ROW", .site = site, .origin = {100, 100}, .repeat_count_x = 3, .repeat_count_y = 1, .flags = DesignRowFlag::kHasDo});
  static_cast<void>(floorplan.createRow(DesignRow{.name = "PAD_ROW", .site = pad_site, .origin = {500, 500}}));

  EXPECT_EQ(floorplan.row(core_row).step_x, 20);
  EXPECT_EQ(floorplan.row(core_row).flags & DesignRowFlag::kHasStep, 0u);
  EXPECT_EQ(floorplan.coreBounds(), (Rect{100, 100, 160, 140}));

  floorplan.setCoreArea(DesignCoreArea{.boundary = {{50, 50}, {400, 50}, {400, 300}, {200, 300}, {200, 500}, {50, 500}}});
  EXPECT_TRUE(floorplan.hasCoreArea());
  EXPECT_EQ(floorplan.coreBounds(), (Rect{50, 50, 400, 500}));
  EXPECT_EQ(floorplan.coreArea().boundary.size(), 6u);
}

TEST_F(DesignStorageTest, ValidatesRowsAndProvidesLayerTrackQueries)
{
  auto& floorplan = design.floorplanStorage();
  EXPECT_THROW((void) floorplan.createRow(
                   DesignRow{.name = "GRID", .site = site, .repeat_count_x = 2, .repeat_count_y = 2, .flags = DesignRowFlag::kHasDo}),
               std::invalid_argument);

  const auto second_layer_entity = tech.registry().create();
  tech.registry().emplace<TechLayerInfo>(second_layer_entity, TechLayerInfo{.name = "M2", .mask_count = 3});
  tech.registry().emplace<TechRoutingLayer>(second_layer_entity, TechRoutingLayer{});
  const TechRoutingLayerId second_layer{second_layer_entity};

  const auto first = floorplan.createTrackGrid(
      DesignTrackGrid{.axis = DesignAxis::kX, .start = 10, .track_count = 3, .step = 10, .layers = {routing_layer, second_layer}});
  const auto second = floorplan.createTrackGrid(DesignTrackGrid{.axis = DesignAxis::kX,
                                                                .start = 20,
                                                                .track_count = 3,
                                                                .step = 10,
                                                                .flags = DesignTrackGridFlag::kHasMask,
                                                                .mask = 2,
                                                                .layers = {second_layer}});

  EXPECT_EQ(floorplan.trackGridsForLayer(second_layer), (std::vector<DesignTrackGridId>{first, second}));
  EXPECT_EQ(floorplan.trackCoordinates(second_layer, DesignAxis::kX), (std::vector<int32_t>{10, 20, 30, 40}));
  EXPECT_TRUE(floorplan.trackCoordinates(second_layer, DesignAxis::kY).empty());
  EXPECT_THROW((void) floorplan.createTrackGrid(DesignTrackGrid{.track_count = 1, .step = 10, .layers = {routing_layer, routing_layer}}),
               std::invalid_argument);
  EXPECT_THROW((void) floorplan.createTrackGrid(DesignTrackGrid{
                   .track_count = 1, .step = 10, .flags = DesignTrackGridFlag::kHasMask, .mask = 4, .layers = {second_layer}}),
               std::invalid_argument);
}

}  // namespace
}  // namespace eccdb
