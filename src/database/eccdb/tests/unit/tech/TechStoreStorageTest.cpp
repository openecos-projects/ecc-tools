// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "tech/TechStore.h"

namespace eccdb {
namespace {

static_assert(std::is_same_v<decltype(TechLayerInfo::name), std::string>);
static_assert(std::is_same_v<decltype(TechViaGeometry::bottom_geometry), GeometryHandle>);
static_assert(std::is_same_v<decltype(TechViaRuleCandidates::values), std::vector<TechViaMasterId>>);
static_assert(std::is_same_v<decltype(TechViaRuleProperty::name), std::string>);
static_assert(std::is_same_v<decltype(std::declval<const TechStore&>().techRootId()), TechRootId>);
static_assert(std::is_same_v<decltype(std::declval<const TechStore&>().globalStorage()), const TechGlobalStorage&>);
static_assert(std::is_same_v<decltype(std::declval<const TechGlobalStorage&>().tryGetUnits()), const TechGlobalUnits*>);
static_assert(std::is_same_v<decltype(std::declval<const TechGlobalStorage&>().getUnits()), const TechGlobalUnits&>);
static_assert(std::is_same_v<decltype(std::declval<const TechGlobalStorage&>().getManufacturingGrid()), const TechManufacturingGrid&>);
static_assert(std::is_same_v<decltype(std::declval<const TechGlobalStorage&>().getMaxViaStack()), const TechMaxViaStack&>);
static_assert(sizeof(TechRoutingLayerId) == sizeof(TechEntity));
static_assert(sizeof(TechCutSpacingRuleId) == sizeof(TechEntity));
static_assert(!std::is_same_v<TechRoutingLayerId, TechCutLayerId>);
static_assert(!std::is_same_v<TechRoutingSpacingRuleId, TechCutSpacingRuleId>);
static_assert(!std::is_convertible_v<TechRoutingLayerId, TechCutLayerId>);

class TechStoreStorageTest : public testing::Test
{
 protected:
  void SetUp() override
  {
    TechLayerInfo bottom_record{.name = "M1"};
    TechLayerInfo cut_record{.name = "V1"};
    TechLayerInfo top_record{.name = "M2"};

    bottom = database.createRoutingLayer(std::move(bottom_record), TechRoutingLayer{});
    cut = database.createCutLayer(std::move(cut_record), TechCutLayer{});
    top = database.createRoutingLayer(std::move(top_record), TechRoutingLayer{});
  }

  TechStore database;
  TechRoutingLayerId bottom;
  TechCutLayerId cut;
  TechRoutingLayerId top;
};

TEST(TechStoreOptionsTest, RectangularizesManhattanViaPolygons)
{
  const TechStoreOptions options{.geometry = GeometryPoolOptions{.polygon_mode = PolygonStorageMode::kRectangularized}};
  TechStore database(options);
  const auto bottom = database.createRoutingLayer(TechLayerInfo{.name = "M1"});
  const auto cut = database.createCutLayer(TechLayerInfo{.name = "V1"});
  const auto top = database.createRoutingLayer(TechLayerInfo{.name = "M2"});

  TechViaMasterShapeInput shapes{.bottom_layer = bottom,
                                 .cut_layer = cut,
                                 .cut_geometry = {.rects = {{.ll_x = 10, .ll_y = 10, .ur_x = 20, .ur_y = 20}}},
                                 .top_layer = top,
                                 .top_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 40, .ur_y = 40}}}};
  shapes.bottom_geometry.polygons.push_back(GeometryPolygonInput{.points = {{0, 0}, {40, 0}, {40, 10}, {10, 10}, {10, 40}, {0, 40}}});
  const auto via = database.viaMasterStorage().createFixedViaMaster(TechViaMaster{.name = "VIA12"}, std::move(shapes));

  const std::array expected_bottom{Rect{.ll_x = 0, .ll_y = 0, .ur_x = 10, .ur_y = 40}, Rect{.ll_x = 10, .ll_y = 0, .ur_x = 40, .ur_y = 10}};
  const auto bottom_rectangles = database.viaMasterStorage().bottomRects(via);
  ASSERT_EQ(bottom_rectangles.size(), expected_bottom.size());
  EXPECT_EQ(bottom_rectangles[0], expected_bottom[0]);
  EXPECT_EQ(bottom_rectangles[1], expected_bottom[1]);
  EXPECT_EQ(database.viaMasterStorage().bottomPolygonCount(via), 0u);
  EXPECT_EQ(database.geometryPool().polygonCount(), 0u);
  EXPECT_EQ(database.geometryPool().pointCount(), 0u);
}

TEST_F(TechStoreStorageTest, CreatesOneTechRootInTheSharedRegistry)
{
  auto& registry = database.techRegistry().registry();
  const auto root = database.techRootId();

  ASSERT_TRUE(database.contains(root));
  EXPECT_TRUE((registry.all_of<TechRoot, TechLayerSequence>(root.entity())));
  EXPECT_EQ(&database.layerSequence(), &registry.get<TechLayerSequence>(root.entity()).layers);
  EXPECT_EQ(&registry, &database.routingLayerStorage().registry());
  EXPECT_EQ(&registry, &database.nonDefaultRuleStorage().registry());

  std::vector<TechEntity> roots;
  for (const auto entity : registry.view<const TechRoot>()) {
    roots.push_back(entity);
  }
  ASSERT_EQ(roots.size(), 1u);
  EXPECT_EQ(roots.front(), root.entity());

  const auto rule = database.nonDefaultRuleStorage().createNonDefaultRule(TechNonDefaultRule{.name = "WIDE"});
  EXPECT_NE(root.entity(), bottom.entity());
  EXPECT_NE(root.entity(), rule.entity());
  EXPECT_TRUE(registry.valid(bottom.entity()));
  EXPECT_TRUE(registry.valid(rule.entity()));
  EXPECT_TRUE(registry.valid(root.entity()));
}

TEST_F(TechStoreStorageTest, StoresLayerPropertiesOnTheLayerEntity)
{
  database.appendLayerProperty(TechLayerId{bottom.entity()}, TechProperty{.name = "TYPE", .value = "ROUTING"});

  const auto& properties = database.layerProperties(TechLayerId{bottom.entity()});
  ASSERT_EQ(properties.size(), 1u);
  EXPECT_EQ(properties.front().name, "TYPE");
  EXPECT_EQ(properties.front().value, "ROUTING");
  EXPECT_TRUE((database.techRegistry().registry().all_of<TechLayerProperties>(bottom.entity())));
}

TEST_F(TechStoreStorageTest, KeepsViaGeometryAndCandidatesInsideComponents)
{
  auto& vias = database.viaMasterStorage();
  const auto via
      = vias.createFixedViaMaster(TechViaMaster{.name = "VIA12"},
                                  TechViaMasterShapeInput{.bottom_layer = bottom,
                                                          .bottom_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}},
                                                          .cut_layer = cut,
                                                          .cut_geometry = {.rects = {{.ll_x = 5, .ll_y = 5, .ur_x = 15, .ur_y = 15}}},
                                                          .top_layer = top,
                                                          .top_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}}});

  EXPECT_EQ(vias.viaMaster(via).name, "VIA12");
  EXPECT_EQ(vias.bottomRects(via).size(), 1u);
  EXPECT_EQ(database.geometryPool().rectangleCount(), 3u);
  EXPECT_EQ(vias.shapeCount(via), 3u);

  auto& via_rules = database.viaRuleStorage();
  const auto rule = via_rules.createViaRule(TechViaRule{.name = "VR12"},
                                            TechViaRuleLowerLayer{.layer = bottom, .direction = RoutingDirection::kHorizontal},
                                            TechViaRuleUpperLayer{.layer = top, .direction = RoutingDirection::kVertical}, {via},
                                            {TechViaRuleProperty{.name = "SOURCE", .value = "LEF"}});

  ASSERT_EQ(via_rules.candidates(rule).size(), 1u);
  EXPECT_EQ(via_rules.candidates(rule).front(), via);
  ASSERT_EQ(via_rules.properties(rule).size(), 1u);
  EXPECT_EQ(via_rules.properties(rule).front().value, "LEF");
}

TEST_F(TechStoreStorageTest, StoresNdrClausesAsTypedComponentsOnOneEntity)
{
  auto& vias = database.viaMasterStorage();
  const auto via
      = vias.createFixedViaMaster(TechViaMaster{.name = "VIA12"},
                                  TechViaMasterShapeInput{.bottom_layer = bottom,
                                                          .bottom_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}},
                                                          .cut_layer = cut,
                                                          .cut_geometry = {.rects = {{.ll_x = 5, .ll_y = 5, .ur_x = 15, .ur_y = 15}}},
                                                          .top_layer = top,
                                                          .top_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}}});

  auto& ndrs = database.nonDefaultRuleStorage();
  const auto& registry = database.techRegistry().registry();
  const auto entity_count = registry.storage<TechEntity>()->size();
  const auto ndr = ndrs.createNonDefaultRule(TechNonDefaultRule{.name = "WIDE"});
  ndrs.setRoutingRule(ndr, TechNdrRoutingRule{.layer = bottom, .width = 160});
  ndrs.addUseVia(ndr, via);

  ASSERT_EQ(ndrs.routingRules(ndr).size(), 1u);
  EXPECT_EQ(ndrs.routingRules(ndr).front().layer, bottom);
  EXPECT_EQ(ndrs.routingRules(ndr).front().width, 160);
  EXPECT_EQ(ndrs.useVias(ndr), (std::vector<TechViaMasterId>{via}));
  EXPECT_EQ(registry.storage<TechEntity>()->size(), entity_count + 1u);
  EXPECT_TRUE((registry.all_of<TechNdrRoutingRules, TechNdrMinCutsRules, TechNdrUseVias, TechNdrUseViaRules, TechNdrProperties,
                               TechNdrViaDefinitions, TechNdrSameNetSpacingRules>(ndr.entity())));
}

TEST_F(TechStoreStorageTest, DestroysOnlyNdrOwnedViaEntitiesWithTheirOwner)
{
  auto& vias = database.viaMasterStorage();
  const auto via
      = vias.createFixedViaMaster(TechViaMaster{.name = "VIA12"},
                                  TechViaMasterShapeInput{.bottom_layer = bottom,
                                                          .bottom_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}},
                                                          .cut_layer = cut,
                                                          .cut_geometry = {.rects = {{.ll_x = 5, .ll_y = 5, .ur_x = 15, .ur_y = 15}}},
                                                          .top_layer = top,
                                                          .top_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}}});

  auto& ndrs = database.nonDefaultRuleStorage();
  const auto ndr = ndrs.createNonDefaultRule(TechNonDefaultRule{.name = "WIDE"});
  ndrs.setRoutingRule(ndr, TechNdrRoutingRule{.layer = bottom, .width = 160});
  ndrs.addUseVia(ndr, via);
  const auto local_via
      = ndrs.addFixedViaDefinition(ndr, TechViaMaster{.name = "NDR_VIA12"},
                                   TechViaMasterShapeInput{.bottom_layer = bottom,
                                                           .bottom_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}},
                                                           .cut_layer = cut,
                                                           .cut_geometry = {.rects = {{.ll_x = 5, .ll_y = 5, .ur_x = 15, .ur_y = 15}}},
                                                           .top_layer = top,
                                                           .top_geometry = {.rects = {{.ll_x = 0, .ll_y = 0, .ur_x = 20, .ur_y = 20}}}});

  EXPECT_TRUE(ndrs.containsViaDefinition(local_via));
  EXPECT_TRUE(vias.contains(local_via));

  EXPECT_TRUE(ndrs.destroyNonDefaultRule(ndr));
  EXPECT_FALSE(ndrs.contains(ndr));
  EXPECT_FALSE(ndrs.containsViaDefinition(local_via));
  EXPECT_TRUE(vias.contains(via));
}

TEST_F(TechStoreStorageTest, StoresGlobalsAsComponentsOnTheTechRoot)
{
  auto& registry = database.techRegistry().registry();
  auto& globals = database.globalStorage();
  const auto root = database.techRootId();
  const auto entity_count = registry.storage<TechEntity>().size();
  TechGlobalUnits units{.flags = TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron, .database_units_per_micron = 2000};

  EXPECT_TRUE(globals.containsRoot());
  EXPECT_EQ(&registry, &globals.registry());
  EXPECT_FALSE(globals.hasUnits());
  EXPECT_FALSE(globals.hasManufacturingGrid());
  EXPECT_FALSE(globals.hasMaxViaStack());
  EXPECT_EQ(globals.tryGetUnits(), nullptr);
  EXPECT_EQ(globals.tryGetManufacturingGrid(), nullptr);
  EXPECT_EQ(globals.tryGetMaxViaStack(), nullptr);
  EXPECT_THROW((void) globals.getUnits(), std::out_of_range);
  EXPECT_THROW((void) globals.getManufacturingGrid(), std::out_of_range);
  EXPECT_THROW((void) globals.getMaxViaStack(), std::out_of_range);

  TechGlobalUnits invalid_units{.flags = TechGlobalUnitsFlag::kHasNanoseconds};
  EXPECT_THROW(globals.setUnits(invalid_units), std::invalid_argument);
  EXPECT_THROW(globals.setUnits(TechGlobalUnits{.nanoseconds = 1}), std::invalid_argument);
  EXPECT_THROW(globals.setManufacturingGrid(0), std::invalid_argument);

  globals.setUnits(units);
  globals.setManufacturingGrid(5);
  globals.setMaxViaStack(TechMaxViaStack{.flags = TechMaxViaStackFlag::kNoSingle | TechMaxViaStackFlag::kHasRange,
                                         .max_stack_count = 2,
                                         .bottom_layer = bottom,
                                         .top_layer = top});

  EXPECT_TRUE(globals.hasUnits());
  EXPECT_TRUE(globals.hasManufacturingGrid());
  EXPECT_TRUE(globals.hasMaxViaStack());
  EXPECT_EQ(globals.getUnits().database_units_per_micron, 2000);
  EXPECT_NE((globals.getUnits().flags & TechGlobalUnitsFlag::kHasDatabaseUnitsPerMicron), 0u);
  EXPECT_EQ(globals.getManufacturingGrid().value, 5);
  EXPECT_EQ(globals.getMaxViaStack().bottom_layer, bottom);
  EXPECT_NE((globals.getMaxViaStack().flags & TechMaxViaStackFlag::kNoSingle), 0u);
  EXPECT_EQ(globals.tryGetUnits(), &registry.get<TechGlobalUnits>(root.entity()));
  EXPECT_EQ(globals.tryGetManufacturingGrid(), &registry.get<TechManufacturingGrid>(root.entity()));
  EXPECT_EQ(globals.tryGetMaxViaStack(), &registry.get<TechMaxViaStack>(root.entity()));
  EXPECT_EQ(&globals.getUnits(), &registry.get<TechGlobalUnits>(root.entity()));
  EXPECT_EQ(&globals.getManufacturingGrid(), &registry.get<TechManufacturingGrid>(root.entity()));
  EXPECT_EQ(&globals.getMaxViaStack(), &registry.get<TechMaxViaStack>(root.entity()));
  EXPECT_TRUE((registry.all_of<TechRoot, TechGlobalUnits, TechManufacturingGrid, TechMaxViaStack>(root.entity())));
  EXPECT_EQ(registry.storage<TechEntity>().size(), entity_count);

  TechGlobalUnits replacement_units{.flags = TechGlobalUnitsFlag::kHasMegahertz, .megahertz = 8};
  globals.setUnits(replacement_units);
  EXPECT_EQ(globals.getUnits().megahertz, 8);
  EXPECT_EQ(globals.getUnits().database_units_per_micron, 0);
  EXPECT_EQ(globals.getUnits().flags, TechGlobalUnitsFlag::kHasMegahertz);

  globals.setManufacturingGrid(10);
  EXPECT_EQ(globals.getManufacturingGrid().value, 10);

  globals.setMaxViaStack(TechMaxViaStack{.max_stack_count = 3});
  EXPECT_EQ(globals.getMaxViaStack().max_stack_count, 3u);
  EXPECT_EQ(globals.getMaxViaStack().flags & TechMaxViaStackFlag::kHasRange, 0u);
  EXPECT_EQ(registry.storage<TechEntity>().size(), entity_count);
}

TEST_F(TechStoreStorageTest, RemovesGlobalComponentsAndRepresentsAbsenceByMissingComponents)
{
  auto& registry = database.techRegistry().registry();
  auto& globals = database.globalStorage();
  const auto root = database.techRootId();

  EXPECT_FALSE((registry.all_of<TechGlobalUnits>(root.entity())));
  EXPECT_FALSE((registry.all_of<TechManufacturingGrid>(root.entity())));
  EXPECT_FALSE((registry.all_of<TechMaxViaStack>(root.entity())));

  globals.setUnits(TechGlobalUnits{});
  EXPECT_TRUE(globals.hasUnits());
  EXPECT_EQ(globals.getUnits().flags, 0u);
  EXPECT_TRUE((registry.all_of<TechGlobalUnits>(root.entity())));

  globals.setUnits(TechGlobalUnits{.flags = TechGlobalUnitsFlag::kHasNanoseconds, .nanoseconds = 1});
  globals.setManufacturingGrid(5);
  globals.setMaxViaStack(
      TechMaxViaStack{.flags = TechMaxViaStackFlag::kHasRange, .max_stack_count = 2, .bottom_layer = bottom, .top_layer = top});

  globals.removeUnits();
  globals.removeManufacturingGrid();
  globals.removeMaxViaStack();

  EXPECT_FALSE(globals.hasUnits());
  EXPECT_FALSE(globals.hasManufacturingGrid());
  EXPECT_FALSE(globals.hasMaxViaStack());
  EXPECT_EQ(globals.tryGetUnits(), nullptr);
  EXPECT_EQ(globals.tryGetManufacturingGrid(), nullptr);
  EXPECT_EQ(globals.tryGetMaxViaStack(), nullptr);
  EXPECT_FALSE((registry.all_of<TechGlobalUnits>(root.entity())));
  EXPECT_FALSE((registry.all_of<TechManufacturingGrid>(root.entity())));
  EXPECT_FALSE((registry.all_of<TechMaxViaStack>(root.entity())));
  EXPECT_TRUE((registry.all_of<TechRoot>(root.entity())));
}

TEST_F(TechStoreStorageTest, ValidatesMaxViaStackRangesAgainstCurrentRoutingLayers)
{
  auto& registry = database.techRegistry().registry();
  auto& globals = database.globalStorage();

  EXPECT_THROW(globals.setMaxViaStack(TechMaxViaStack{}), std::invalid_argument);
  EXPECT_THROW(globals.setMaxViaStack(TechMaxViaStack{.max_stack_count = 2, .bottom_layer = bottom}), std::invalid_argument);
  globals.setMaxViaStack(TechMaxViaStack{.flags = TechMaxViaStackFlag::kNoSingle | TechMaxViaStackFlag::kHasRange,
                                         .max_stack_count = 2,
                                         .bottom_layer = bottom,
                                         .top_layer = top});
  EXPECT_EQ(globals.getMaxViaStack().bottom_layer, bottom);

  EXPECT_THROW(globals.setMaxViaStack(TechMaxViaStack{
                   .flags = TechMaxViaStackFlag::kHasRange, .max_stack_count = 2, .bottom_layer = top, .top_layer = bottom}),
               std::invalid_argument);
  EXPECT_THROW(globals.setMaxViaStack(TechMaxViaStack{.flags = TechMaxViaStackFlag::kHasRange,
                                                      .max_stack_count = 2,
                                                      .bottom_layer = TechRoutingLayerId{cut.entity()},
                                                      .top_layer = top}),
               std::invalid_argument);

  EXPECT_THROW(globals.setMaxViaStack(TechMaxViaStack{
                   .flags = TechMaxViaStackFlag::kHasRange, .max_stack_count = 2, .bottom_layer = TechRoutingLayerId{}, .top_layer = top}),
               std::invalid_argument);

  TechStore other_database;
  static_cast<void>(other_database.createRoutingLayer(TechLayerInfo{.name = "N1"}, TechRoutingLayer{}));
  static_cast<void>(other_database.createRoutingLayer(TechLayerInfo{.name = "N2"}, TechRoutingLayer{}));
  static_cast<void>(other_database.createRoutingLayer(TechLayerInfo{.name = "N3"}, TechRoutingLayer{}));
  const auto foreign_layer = other_database.createRoutingLayer(TechLayerInfo{.name = "N4"}, TechRoutingLayer{});
  ASSERT_FALSE(database.techRegistry().registry().valid(foreign_layer.entity()));
  EXPECT_THROW(globals.setMaxViaStack(TechMaxViaStack{
                   .flags = TechMaxViaStackFlag::kHasRange, .max_stack_count = 2, .bottom_layer = bottom, .top_layer = foreign_layer}),
               std::invalid_argument);

  const auto stale_layer = database.createRoutingLayer(TechLayerInfo{.name = "M3"}, TechRoutingLayer{});
  registry.destroy(stale_layer.entity());
  EXPECT_FALSE(registry.valid(stale_layer.entity()));
  EXPECT_THROW(globals.setMaxViaStack(TechMaxViaStack{
                   .flags = TechMaxViaStackFlag::kHasRange, .max_stack_count = 2, .bottom_layer = bottom, .top_layer = stale_layer}),
               std::invalid_argument);
}

}  // namespace
}  // namespace eccdb
